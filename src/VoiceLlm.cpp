#include "VoiceLlm.hpp"

#include <cpr/cpr.h>

#include <algorithm>
#include <cctype>

namespace Voice {
namespace {
std::string HttpError(long status) {
  if (status == 401 || status == 403) return "大模型认证失败，请检查 API Key 和模型权限";
  if (status == 404) return "大模型接口地址或模型名称不正确";
  if (status == 429) return "大模型请求过于频繁或额度不足，请稍后重试";
  return "大模型请求失败（HTTP " + std::to_string(status) + "）";
}

std::string ProviderError(const nlohmann::json& body) {
  const auto& error = body.contains("error") && body["error"].is_object() ? body["error"] : body;
  auto message = error.value("message", std::string{});
  if (message.size() > 200) message = message.substr(0, 200);
  const auto code = error.contains("code") && error["code"].is_string() ? error["code"].get<std::string>() : "";
  return "大模型返回错误" + (code.empty() ? std::string{} : "（" + code + "）") + (message.empty() ? "" : "：" + message);
}

// {base}{path}, accepting a base that already ends with /chat/completions.
std::string ChatApiUrl(const std::string& baseUrl, const std::string& path) {
  auto url = baseUrl;
  while (!url.empty() && url.back() == '/') url.pop_back();
  const std::string suffix = "/chat/completions";
  if (url.size() >= suffix.size() && url.compare(url.size() - suffix.size(), suffix.size(), suffix) == 0)
    url.erase(url.size() - suffix.size());
  return url + path;
}
}  // namespace

std::string ChatStreamParser::Feed(const std::string& bytes) {
  std::string text;
  if (raw_.size() < 64 * 1024) raw_ += bytes.substr(0, 64 * 1024 - raw_.size());
  pending_ += bytes;
  size_t newline;
  while ((newline = pending_.find('\n')) != std::string::npos) {
    auto line = pending_.substr(0, newline);
    pending_.erase(0, newline + 1);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.compare(0, 5, "data:") != 0) continue;
    sse_ = true;
    auto data = line.substr(5);
    data.erase(0, data.find_first_not_of(' '));
    if (data == "[DONE]") { done_ = true; continue; }
    Event(data, text);
  }
  return text;
}

void ChatStreamParser::Event(const std::string& data, std::string& text) {
  const auto event = nlohmann::json::parse(data, nullptr, false);
  if (!event.is_object()) return;
  if (event.contains("error")) { error_ = ProviderError(event); return; }
  if (!event.contains("choices") || !event["choices"].is_array() || event["choices"].empty()) return;
  const auto& choice = event["choices"][0];
  if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) finish_ = choice["finish_reason"];
  if (!choice.contains("delta") || !choice["delta"].is_object()) return;
  const auto& delta = choice["delta"];
  if (delta.contains("content") && delta["content"].is_string()) text += delta["content"].get<std::string>();
  if (!delta.contains("tool_calls") || !delta["tool_calls"].is_array()) return;
  for (const auto& part : delta["tool_calls"]) {
    if (!part.is_object()) continue;
    const size_t index = part.contains("index") && part["index"].is_number_unsigned() ? part["index"].get<size_t>() : calls_.size();
    if (index >= 16) continue;
    if (calls_.size() <= index) calls_.resize(index + 1);
    auto& call = calls_[index];
    if (part.contains("id") && part["id"].is_string() && !part["id"].get_ref<const std::string&>().empty()) call.id = part["id"];
    if (!part.contains("function") || !part["function"].is_object()) continue;
    const auto& function = part["function"];
    if (function.contains("name") && function["name"].is_string()) call.name += function["name"].get<std::string>();
    if (function.contains("arguments") && function["arguments"].is_string() && call.arguments.size() < 64 * 1024)
      call.arguments += function["arguments"].get<std::string>();
  }
}

void ChatStreamParser::Finish(long status) {
  if (status >= 200 && status < 300) {
    if (!sse_ && error_.empty()) error_ = "大模型没有返回流式结果，请检查接口地址";
    return;
  }
  const auto body = nlohmann::json::parse(raw_, nullptr, false);
  error_ = body.is_object() && (body.contains("error") || body.contains("message")) ? HttpError(status) + "；" + ProviderError(body)
                                                                                     : HttpError(status);
}

std::vector<ToolCall> ChatStreamParser::ToolCalls() const {
  std::vector<ToolCall> calls;
  for (size_t i = 0; i < calls_.size(); ++i) {
    const auto& call = calls_[i];
    if (call.name.empty()) continue;
    // Some compatible servers omit ids; tool results still need a stable one.
    calls.push_back({call.id.empty() ? "call_" + std::to_string(i) : call.id, call.name,
        call.arguments.empty() ? "{}" : call.arguments});
  }
  return calls;
}

ChatClient::~ChatClient() {
  ++shared_->generation;
  for (auto& worker : workers_) if (worker.joinable()) worker.join();
}

void ChatClient::Cancel() {
  ++shared_->generation;
  active_ = false;
  std::lock_guard<std::mutex> lock(shared_->mutex);
  shared_->events.clear();
}

void ChatClient::Start(const std::string& url, const std::string& apiKey, const nlohmann::json& body) {
  Cancel();
  const auto generation = ++shared_->generation;
  active_ = true;
  const auto shared = shared_;
  const auto payload = body.dump();
  workers_.emplace_back([shared, generation, url, apiKey, payload] {
    ChatStreamParser parser;
    const auto live = [&] { return shared->generation == generation; };
    const auto push = [&](Event event) {
      std::lock_guard<std::mutex> lock(shared->mutex);
      if (live()) shared->events.emplace_back(generation, std::move(event));
    };
    const cpr::WriteCallback write{[&](std::string data, intptr_t) {
      if (!live()) return false;
      auto text = parser.Feed(data);
      if (!text.empty()) push({Event::Type::Text, std::move(text), {}, {}});
      return live();
    }};
    // Progress callbacks also run while waiting, so cancellation never waits for data.
    const cpr::ProgressCallback progress{[&](cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t) {
      return live();
    }};
    const auto response = cpr::Post(cpr::Url{url},
        cpr::Header{{"Authorization", "Bearer " + apiKey}, {"Content-Type", "application/json"}, {"Accept", "text/event-stream"}},
        cpr::Body{payload}, cpr::ConnectTimeout{5000}, cpr::Timeout{120000}, cpr::Redirect{false}, write, progress);
    if (!live()) return;
    if (response.error.code != cpr::ErrorCode::OK && !parser.Done()) {
      push({Event::Type::Failed, "大模型网络请求失败或超时，请检查网络后重试", {}, {}});
      return;
    }
    parser.Finish(response.status_code);
    if (!parser.Error().empty()) push({Event::Type::Failed, parser.Error(), {}, {}});
    else push({Event::Type::Done, {}, parser.ToolCalls(), parser.FinishReason()});
  });
}

std::vector<ChatClient::Event> ChatClient::Poll() {
  std::vector<Event> events;
  {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    for (auto& [generation, event] : shared_->events)
      if (generation == shared_->generation) events.push_back(std::move(event));
    shared_->events.clear();
  }
  for (const auto& event : events) if (event.type != Event::Type::Text) active_ = false;
  // Finished requests have returned; join them so threads do not accumulate.
  if (!active_ && workers_.size() > 1) {
    for (size_t i = 0; i + 1 < workers_.size(); ++i) if (workers_[i].joinable()) workers_[i].join();
    workers_.erase(workers_.begin(), workers_.end() - 1);
  }
  return events;
}

const std::vector<ChatProvider>& ChatProviders() {
  static const std::vector<ChatProvider> providers = {
    {"deepseek", "DeepSeek", "https://api.deepseek.com"},
    {"openrouter", "OpenRouter", "https://openrouter.ai/api/v1"},
    {"thirdparty", "第三方（OpenAI 兼容）", ""}};
  return providers;
}

const ChatProvider* FindChatProvider(const std::string& id) {
  for (const auto& provider : ChatProviders()) if (id == provider.id) return &provider;
  return nullptr;
}

nlohmann::json ChatRequestOptions(const std::string& provider) {
  if (provider == "deepseek") return {{"thinking", {{"type", "disabled"}}}};
  // Models whose reasoning is mandatory are not offered (see ParseChatModels).
  if (provider == "openrouter") return {{"reasoning", {{"effort", "none"}}}};
  return nlohmann::json::object();
}

std::vector<ChatModel> ParseChatModels(const std::string& provider, const nlohmann::json& body) {
  std::vector<ChatModel> models;
  if (!body.is_object() || !body.contains("data") || !body["data"].is_array()) return models;
  for (const auto& item : body["data"]) {
    if (!item.is_object() || !item.contains("id") || !item["id"].is_string()) continue;
    const auto id = item["id"].get<std::string>();
    if (!ValidChatModel(id)) continue;
    if (provider == "openrouter") {
      const auto& parameters = item.contains("supported_parameters") ? item["supported_parameters"] : nlohmann::json();
      if (!parameters.is_array() || std::find(parameters.begin(), parameters.end(), "tools") == parameters.end()) continue;
      if (item.contains("reasoning") && item["reasoning"].is_object() && item["reasoning"].value("mandatory", false)) continue;
    }
    auto name = item.contains("name") && item["name"].is_string() ? item["name"].get<std::string>() : id;
    if (name.empty() || name.size() > 128) name = id;
    // DeepSeek lists input_modalities on the model, OpenRouter under architecture.
    const auto& architecture = item.contains("architecture") && item["architecture"].is_object() ? item["architecture"] : item;
    std::optional<bool> vision;
    for (const auto* source : {&item, &architecture})
      if (source->contains("input_modalities") && (*source)["input_modalities"].is_array()) {
        const auto& inputs = (*source)["input_modalities"];
        vision = std::find(inputs.begin(), inputs.end(), "image") != inputs.end();
      }
    models.push_back({id, name, vision});
    if (models.size() >= 2000) break;
  }
  const auto lower = [](std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
  };
  std::stable_sort(models.begin(), models.end(), [&](const ChatModel& a, const ChatModel& b) { return lower(a.name) < lower(b.name); });
  return models;
}

std::vector<ChatModel> FetchChatModels(const std::string& provider, const std::string& baseUrl,
                                       const std::string& apiKey, std::string& error) {
  cpr::Header header{{"Accept", "application/json"}};
  if (!apiKey.empty()) header["Authorization"] = "Bearer " + apiKey;
  cpr::Parameters parameters;
  if (provider == "openrouter") parameters.Add({"supported_parameters", "tools"});
  const auto response = cpr::Get(cpr::Url{ChatApiUrl(baseUrl, "/models")}, header, parameters,
      cpr::ConnectTimeout{5000}, cpr::Timeout{20000}, cpr::Redirect{false});
  if (response.error.code != cpr::ErrorCode::OK) { error = "无法获取模型列表，请检查网络和接口地址"; return {}; }
  if (response.status_code == 401 || response.status_code == 403) { error = "获取模型列表失败：API Key 无效或没有权限"; return {}; }
  if (response.status_code == 404) { error = "获取模型列表失败：接口地址不正确，或该服务不提供模型列表"; return {}; }
  if (response.status_code < 200 || response.status_code >= 300) {
    error = "获取模型列表失败（HTTP " + std::to_string(response.status_code) + "）";
    return {};
  }
  const auto body = nlohmann::json::parse(response.text, nullptr, false);
  auto models = ParseChatModels(provider, body);
  if (models.empty()) error = body.is_object() && body.contains("data") ? "该服务没有可用的模型" : "模型列表格式不正确，请检查接口地址";
  return models;
}

std::string ChatCompletionsUrl(const std::string& baseUrl) { return ChatApiUrl(baseUrl, "/chat/completions"); }

bool ValidChatModel(const std::string& model) {
  return !model.empty() && model.size() <= 128 && std::all_of(model.begin(), model.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == ':' || c == '/' || c == '@' || c == '~';
  });
}

bool ValidChatBaseUrl(const std::string& url) {
  if (url.size() > 512 || std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= 32 || c >= 127; }) ||
      url.find_first_of("@#") != std::string::npos) return false;
  std::string rest;
  if (url.compare(0, 8, "https://") == 0) rest = url.substr(8);
  else if (url.compare(0, 7, "http://") == 0) {
    rest = url.substr(7);
    const auto host = rest.substr(0, rest.find_first_of("/:"));
    if (host != "localhost" && host != "127.0.0.1" && rest.compare(0, 5, "[::1]") != 0) return false;
  } else return false;
  return !rest.empty() && rest[0] != '/' && rest[0] != ':';
}
}  // namespace Voice
