#include "VoiceCascade.hpp"
#include "VoiceContext.hpp"
#include "VoiceLlm.hpp"
#include "VoiceText.hpp"

#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <vector>

namespace {
using nlohmann::json;
using Voice::ChatEvent;

void Check(bool condition, const char* description) {
  if (!condition) throw std::runtime_error(description);
}

struct FakeChat final : Voice::ChatBackend {
  std::vector<json> requests;
  std::vector<ChatEvent> pending;
  int cancels = 0;
  void Start(const std::string& url, const std::string& key, const json& body) override {
    Check(url == "https://chat.example/v1/chat/completions" && key == "chat-key", "chat uses the configured endpoint");
    requests.push_back(body);
  }
  void Cancel() override { ++cancels; pending.clear(); }
  std::vector<ChatEvent> Poll() override { auto events = std::move(pending); pending.clear(); return events; }
};

struct FakeSynthesizer final : Voice::Synthesizer {
  std::vector<std::pair<uint64_t, std::string>> spoken;
  std::vector<Output> pending;
  int cancels = 0;
  void Speak(uint64_t id, const std::string& text) override { spoken.emplace_back(id, text); }
  void Cancel() override { ++cancels; pending.clear(); }
  std::vector<Output> Poll() override { auto outputs = std::move(pending); pending.clear(); return outputs; }
};

std::string Tone(double seconds, double amplitude = 8000) {
  std::string pcm;
  for (size_t i = 0; i < size_t(seconds * 16000); ++i) {
    const auto sample = int16_t(amplitude * std::sin(2 * 3.14159265 * 220 * i / 16000));
    pcm.append(reinterpret_cast<const char*>(&sample), 2);
  }
  return pcm;
}

float LoudVad(const float* frame) {
  double energy = 0;
  for (size_t i = 0; i < Voice::SileroVad::Frame; ++i) energy += frame[i] * frame[i];
  return energy > 1e-4 ? 0.95f : 0.05f;
}

struct Fixture {
  FakeChat* chat = nullptr;
  FakeSynthesizer* synth = nullptr;
  std::vector<Voice::ToolCall> tools;
  std::vector<Voice::ConversationEvent> history;
  std::string played, state, message;
  bool playing = false;
  uint64_t heard = 0;
  int stops = 0;
  // What local recognition "hears" next, and the audio it was given.
  std::mutex mutex;
  std::string next;
  std::vector<size_t> transcribed;
  std::unique_ptr<Voice::Pipeline> pipeline;

  explicit Fixture(bool handsFree = true, bool threaded = false, bool voice = true, size_t contextWindow = 32768) {
    auto fake = std::make_unique<FakeChat>();
    chat = fake.get();
    auto fakeSynth = voice ? std::make_unique<FakeSynthesizer>() : nullptr;
    synth = fakeSynth.get();
    Voice::CascadeConfig config{"https://chat.example/v1/chat/completions", "chat-key", "deepseek-chat",
                                Voice::ChatRequestOptions("deepseek"), contextWindow};
    Voice::Pipeline::Callbacks callbacks{
      [this](const std::string& pcm) { played += pcm; playing = true; },
      [this] { return playing; },
      [this] { return heard; },
      [this] { ++stops; playing = false; heard = 0; },
      [this](const std::string& value, const std::string& text) { state = value; message = text; },
      [](const std::string&) {},
      [this](const std::vector<Voice::ToolCall>& calls) { tools.insert(tools.end(), calls.begin(), calls.end()); },
      [this](const Voice::ConversationEvent& event) { history.push_back(event); },
      [] {},
      [](const std::string&) {}};
    std::unique_ptr<Voice::TurnAnalyzer> analyzer;
    if (handsFree) analyzer = std::make_unique<Voice::TurnAnalyzer>(std::make_unique<Voice::TurnDetector>(
        LoudVad, [](const std::vector<int16_t>&) { return 0.9f; }), false);
    pipeline = std::make_unique<Voice::Pipeline>(config, callbacks, json::array({{{"type", "function"}, {"function", {{"name", "get_game_profile"}}}}}),
        std::move(analyzer), std::move(fake), std::move(fakeSynth), [this](const std::string& pcm) {
          std::lock_guard<std::mutex> lock(mutex);
          transcribed.push_back(pcm.size());
          return next;
        }, threaded);
  }
  void Hear(const std::string& text) { std::lock_guard<std::mutex> lock(mutex); next = text; }
  void Say(const std::string& pcm) {
    for (size_t offset = 0; offset < pcm.size(); offset += 640) { pipeline->AppendInput(pcm.substr(offset, 640)); pipeline->Tick(); }
  }
  // One spoken turn: silence, speech, then the pause that ends it.
  void Turn(const std::string& text) {
    Hear(text);
    Say(Tone(1.0));
    Say(Tone(0.4, 0));
  }
  // Finishes the sentence being synthesised: its audio, then the end.
  void Synthesize(size_t samples) {
    const auto id = synth->spoken.back().first;
    synth->pending.push_back({id, std::string(samples * 2, '\1')});
    synth->pending.push_back({id, {}, true});
    pipeline->Tick();
  }
  void Stream(std::vector<std::string> deltas, std::vector<Voice::ToolCall> calls = {}) {
    for (auto& delta : deltas) chat->pending.push_back({ChatEvent::Type::Text, delta, {}, {}});
    chat->pending.push_back({ChatEvent::Type::Done, {}, std::move(calls), "stop"});
    pipeline->Tick();
  }
};

void Text() {
  Voice::SentenceSplitter splitter;
  auto sentences = splitter.Push("好的，**今天**是晴天。明天");
  Check(sentences == std::vector<std::string>{"好的，今天是晴天。"}, "a short first clause waits for the sentence end");
  sentences = splitter.Push("可能会下雨，记得带伞哦！看看 https://example.com/a?b=1 吧");
  Check(sentences == std::vector<std::string>{"明天可能会下雨，记得带伞哦！"}, "later commas need a long clause");
  Check(splitter.Flush() == "看看 链接 吧", "links are not read aloud");
  splitter.Reset();
  Check(splitter.Push("我先帮你查一下，").size() == 1, "the first clause may end at a comma for a fast start");
  Check(splitter.Push("。").empty(), "punctuation alone is not spoken");
  Check(Voice::PrefixFraction("今天是晴天", 0.4) == "今天" && Voice::CodePoints("晴天ok") == 4, "UTF-8 aware prefixes");
  Check(Voice::LooksLikeEcho("今天是晴天", "好的，今天是个晴天。") && !Voice::LooksLikeEcho("等一下", "好的，今天是个晴天。"),
      "echo matches the reply text, not new speech");
  Check(Voice::SpokenNumbers("你有1250点经验，排第12名，音量30%，进度2.5倍") == "你有一千二百五十点经验，排第十二名，音量百分之三十，进度二点五倍",
        "numbers are written as they are read");
  Check(Voice::SpokenNumbers("1,250,000人，100000，10050，105，0，007") == "一百二十五万人，十万，一万零五十，一百零五，零，零零七",
        "groups of 万, inner zeros and digit strings");
  Check(Voice::SpokenNumbers("2026年8:05和12:30、9:00，Qwen3") == "二零二六年八点零五分和十二点三十分、九点，Qwen三",
        "years read digit by digit, clock times");
}

void Parser() {
  Voice::ChatStreamParser parser;
  auto text = parser.Feed("data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":\"你\"}}]}\n\ndata: {\"choices\":[{\"delta\":{\"con");
  text += parser.Feed("tent\":\"好\",\"tool_calls\":[{\"index\":0,\"id\":\"call_1\",\"function\":{\"name\":\"get_game_\",\"arguments\":\"{\\\"a\\\"\"}}]}}]}\n\n");
  text += parser.Feed("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"name\":\"profile\",\"arguments\":\":1}\"}}]},\"finish_reason\":\"tool_calls\"}]}\n\ndata: [DONE]\n\n");
  parser.Finish(200);
  const auto calls = parser.ToolCalls();
  Check(text == "你好" && parser.Done() && parser.Error().empty() && parser.FinishReason() == "tool_calls", "SSE text and finish reason");
  Check(calls.size() == 1 && calls[0].id == "call_1" && calls[0].name == "get_game_profile" && calls[0].arguments == "{\"a\":1}",
      "tool calls are assembled from fragments");
  Voice::ChatStreamParser failed;
  failed.Feed("{\"error\":{\"code\":\"InvalidApiKey\",\"message\":\"bad key\"}}");
  failed.Finish(401);
  Check(failed.Error().find("认证失败") != std::string::npos && failed.Error().find("InvalidApiKey") != std::string::npos, "HTTP errors are readable");
  Check(Voice::ChatCompletionsUrl("https://api.deepseek.com") == "https://api.deepseek.com/chat/completions" &&
        Voice::ChatCompletionsUrl("https://api.example.com/v1/") == "https://api.example.com/v1/chat/completions" &&
        Voice::ChatCompletionsUrl("https://api.example.com/v1/chat/completions") == "https://api.example.com/v1/chat/completions",
        "chat endpoints");
  Check(Voice::ValidChatBaseUrl("https://api.deepseek.com/v1") && Voice::ValidChatBaseUrl("http://127.0.0.1:11434/v1") &&
        !Voice::ValidChatBaseUrl("http://example.com/v1") && !Voice::ValidChatBaseUrl("https://user:pass@example.com") &&
        !Voice::ValidChatBaseUrl("https://example.com/v1#x") && !Voice::ValidChatBaseUrl("ftp://example.com"), "chat base URL validation");
  Check(Voice::ValidChatModel("deepseek-chat") && Voice::ValidChatModel("openai/gpt-4o:free") && Voice::ValidChatModel("@cf/meta/llama") &&
        !Voice::ValidChatModel("") && !Voice::ValidChatModel("bad model") && !Voice::ValidChatModel(std::string(129, 'a')), "model ids");
}

void Providers() {
  std::vector<std::string> ids;
  for (const auto& provider : Voice::ChatProviders()) ids.push_back(provider.id);
  Check(ids == std::vector<std::string>{"deepseek", "openrouter", "thirdparty"} && !Voice::FindChatProvider("bailian") &&
        std::string(Voice::FindChatProvider("deepseek")->baseUrl) == "https://api.deepseek.com" &&
        std::string(Voice::FindChatProvider("thirdparty")->baseUrl).empty(), "chat services");
  Check(Voice::ChatRequestOptions("deepseek") == json({{"thinking", {{"type", "disabled"}}}}) &&
        Voice::ChatRequestOptions("openrouter") == json({{"reasoning", {{"effort", "none"}}}}) &&
        Voice::ChatRequestOptions("thirdparty") == json::object(), "reasoning is turned off where the service allows it");

  const auto deepseek = Voice::ParseChatModels("deepseek", json::parse(R"({"object":"list","data":[
    {"id":"deepseek-v4-pro","object":"model","name":"DeepSeek-V4-Pro","input_modalities":["text","image"]},{"id":"deepseek-chat","object":"model"},
    {"id":"bad model"},{"object":"model"},"x"]})"));
  Check(deepseek.size() == 2 && deepseek[0].id == "deepseek-chat" && deepseek[0].name == "deepseek-chat" &&
        deepseek[1].name == "DeepSeek-V4-Pro", "models are named, sorted by name and invalid ids dropped");
  Check(!deepseek[0].vision && deepseek[1].vision == true, "image input is read from the model list when listed");
  const auto openrouter = Voice::ParseChatModels("openrouter", json::parse(R"({"data":[
    {"id":"a/tools","name":"A: Tools","supported_parameters":["tools","reasoning"],"reasoning":{"mandatory":false},
     "architecture":{"input_modalities":["text","image"]}},
    {"id":"b/plain","name":"B: Plain","supported_parameters":["tools"],"reasoning":null,"architecture":{"input_modalities":["text"]}},
    {"id":"c/reasoning-only","name":"C: Always thinks","supported_parameters":["tools"],"reasoning":{"mandatory":true}},
    {"id":"d/no-tools","name":"D: No tools","supported_parameters":["max_tokens"]}]})"));
  Check(openrouter.size() == 2 && openrouter[0].id == "a/tools" && openrouter[1].id == "b/plain" &&
        openrouter[0].vision == true && openrouter[1].vision == false,
        "OpenRouter offers models with tools whose reasoning can be turned off");
  Check(Voice::ParseChatModels("thirdparty", json::parse(R"({"data":[{"id":"local-model"}]})")).size() == 1 &&
        Voice::ParseChatModels("thirdparty", json::parse(R"({"error":"nope"})")).empty() &&
        Voice::ParseChatModels("thirdparty", json::array()).empty(), "third-party lists and malformed bodies");
}

void Conversation() {
  Fixture f;
  f.pipeline->BeginInput(false);
  Check(f.synth->spoken.empty(), "opening the microphone starts nothing");
  f.Say(Tone(0.5, 0));
  Check(f.transcribed.empty(), "silence is not transcribed");
  f.Turn("今天天气怎么样");
  Check(f.transcribed.size() == 1 && f.transcribed[0] >= size_t(1.6 * 32000), "the whole utterance with pre-roll is transcribed once");
  Check(f.chat->requests.size() == 1, "a transcript starts the chat request");
  const auto& body = f.chat->requests[0];
  Check(body["stream"] == true && body["model"] == "deepseek-chat" && body["thinking"]["type"] == "disabled" && body["tools"].size() == 1 &&
        body["messages"][0]["role"] == "system" && body["messages"][1] == json({{"role", "user"}, {"content", "今天天气怎么样"}}),
        "the chat request carries the prompt, tools and the user's words");
  const auto prompt = body["messages"][0]["content"].get<std::string>();
  Check(prompt.find(Voice::Instructions()) == 0 && prompt.find("性格：") != std::string::npos &&
        prompt.find("说话习惯：") != std::string::npos && prompt.find("语音表达：") != std::string::npos &&
        prompt.find("使用正常中文和标点来断句") != std::string::npos &&
        prompt.find("不用‘8’代替‘不’") != std::string::npos && prompt.find("文字表达：") == std::string::npos,
        "voice requests share the persona but use pronunciation-safe speech rules");

  f.Stream({"好的，今天", "是晴天。明天", "可能下雨。"});
  Check(f.synth->spoken.size() == 1 && f.synth->spoken[0].second == "好的，今天是晴天。",
        "the first complete sentence is synthesised right away, alone");
  f.Synthesize(4800);
  Check(f.played.size() == 9600 && f.state == "speaking", "the first sentence plays as soon as it is synthesised");
  Check(f.synth->spoken.size() == 2 && f.synth->spoken[1].second == "明天可能下雨。",
        "the next sentence starts after the previous one finishes");
  f.Synthesize(2400);
  f.pipeline->Tick();
  Check(f.pipeline->WaitingForReply() == false && f.pipeline->Busy(), "the reply stays active while audio plays");
  f.playing = false;
  f.pipeline->Tick();
  Check(f.history.back().type == Voice::ConversationEvent::Type::Completed, "history records completion");

  // Speech over a reply: our own echo is ignored, a real interruption stops it.
  f.Turn("讲个故事");
  f.Stream({"从前有一座山，山里有座庙。", "庙里有个老和尚。"});
  f.Synthesize(4800);
  f.heard = 2400;  // Half of the first sentence was heard.
  const int cancels = f.synth->cancels;
  const int before = f.stops;
  f.Hear("从前有一座山");
  f.Say(Tone(0.8));
  Check(f.stops == before, "speech matching the reply is treated as echo and does not interrupt");
  f.Hear("等一下");
  f.Say(Tone(0.6));
  Check(f.stops == before + 1 && f.chat->cancels > 0 && f.synth->cancels == cancels + 1,
        "a partial transcript that is not echo interrupts playback, chat and synthesis");
  const auto played = f.played.size();
  f.synth->pending.push_back({f.synth->spoken.back().first, std::string(960, '\1')});
  f.pipeline->Tick();
  Check(f.played.size() == played, "late audio of the interrupted sentence is not played");
  f.Say(Tone(0.4, 0));
  const auto& messages = f.chat->requests.back()["messages"];
  // The first segment "从前有一座山，" was half played: three of its seven characters.
  Check(messages[messages.size() - 2] == json({{"role", "assistant"}, {"content", "从前有"}}) &&
        messages.back()["content"] == "等一下", "the context keeps only the spoken part of the interrupted reply");

  // A whole turn that only repeats the reply is dropped.
  f.Stream({"好的，我在听。"});
  f.Synthesize(4800);
  const size_t requests = f.chat->requests.size();
  const int stops = f.stops;
  f.Turn("好的我在听");
  Check(f.chat->requests.size() == requests && f.stops == stops, "a turn that repeats the reply is treated as echo");
  f.playing = false;
  f.pipeline->Tick();

  // Tool round: results go back to the model before it answers.
  f.Turn("我现在多少经验");
  f.Stream({"我查一下。"}, {{"call_9", "get_game_profile", "{}"}});
  Check(f.tools.size() == 1 && f.tools[0].id == "call_9" && f.state == "tool", "tool calls are executed");
  f.pipeline->CompleteTool("call_9", {{"ok", true}, {"exp", 42}});
  const auto& round = f.chat->requests.back()["messages"];
  Check(round[round.size() - 2]["tool_calls"][0]["id"] == "call_9" && round.back()["role"] == "tool" &&
        round.back()["tool_call_id"] == "call_9", "the follow-up request includes the call and its result");
}

void SynthesisFailure() {
  Fixture f;
  f.pipeline->BeginInput(false);
  f.Turn("你好");
  f.Stream({"你好呀。我是轴伊。"});
  f.synth->pending.push_back({f.synth->spoken.back().first, {}, true, "轴伊的声音暂时无法合成，请稍后重试"});
  f.pipeline->Tick();
  Check(f.history.back().type == Voice::ConversationEvent::Type::Failed && f.synth->spoken.size() == 1 &&
        f.state == "listening" && f.message == "轴伊的声音暂时无法合成，请稍后重试",
        "a synthesis failure ends the reply with a readable message");
  Check(!f.pipeline->WaitingForReply(), "a failed reply waits for nothing");
}

void TypedMessages() {
  Fixture f(false, false, false);  // Text-only: no synthesizer, no microphone.
  f.pipeline->SetContext(json::array({{{"role", "user"}, {"content", "早上好"}}, {{"role", "assistant"}, {"content", "早呀"}}}));
  f.pipeline->SubmitText("  帮我看看今天的任务\n");
  Check(f.chat->requests.size() == 1 && f.state == "thinking", "a typed message starts the chat request");
  const auto& body = f.chat->requests.back();
  Check(body["messages"][0]["content"].get<std::string>().find("打字") != std::string::npos &&
        body["messages"][1]["content"] == "早上好" && body["messages"].back() == json({{"role", "user"}, {"content", "帮我看看今天的任务"}}),
        "typed messages continue the carried context with text-reply instructions");
  const auto prompt = body["messages"][0]["content"].get<std::string>();
  Check(prompt.find(Voice::Instructions()) == 0 && prompt.find("性格：") != std::string::npos &&
        prompt.find("说话习惯：") != std::string::npos && prompt.find("文字表达：") != std::string::npos &&
        prompt.find("偶尔用‘8’代替‘不’") != std::string::npos &&
        prompt.find("链接、代码、数据和工具参数保持原样") != std::string::npos && prompt.find("语音表达：") == std::string::npos,
        "typed requests share the persona and allow chat mannerisms without corrupting literal data");
  Check(f.history[0].type == Voice::ConversationEvent::Type::Started && f.history[0].data == json({{"input", "text"}}) &&
        f.history[1].text == "帮我看看今天的任务", "history marks the message as typed");
  f.Stream({"今天有三个任务。", "要我帮你安排吗？"});
  Check(f.played.empty() && f.state == "idle" && !f.pipeline->Replying(), "a typed reply finishes without speech");
  Check(f.pipeline->Context().back() == json({{"role", "assistant"}, {"content", "今天有三个任务。要我帮你安排吗？"}}),
        "the shown reply stays in the context");

  f.pipeline->SubmitText("讲个长故事");
  f.chat->pending.push_back({ChatEvent::Type::Text, "从前有座山，", {}, {}});
  f.pipeline->Tick();
  Check(f.state == "replying" && f.pipeline->Replying(), "streaming text is a reply in progress");
  f.pipeline->StopReply();
  Check(!f.pipeline->Replying() && f.chat->cancels > 0 && f.state == "idle" &&
        f.history.back().type == Voice::ConversationEvent::Type::Interrupted, "stop cancels the reply");
  Check(f.pipeline->Context().back() == json({{"role", "assistant"}, {"content", "从前有座山，"}}),
        "a stopped typed reply keeps what was shown");

  f.pipeline->SubmitText("我有多少经验");
  f.Stream({}, {{"call_3", "get_game_profile", "{}"}});
  Check(f.tools.size() == 1 && f.pipeline->Replying(), "typed messages may call tools");
  f.pipeline->Reset();
  const auto context = f.pipeline->Context();
  Check(context.back()["role"] == "tool" && context.back()["tool_call_id"] == "call_3",
        "a reset keeps every tool call paired with a result");
  f.pipeline->SubmitText("   ");
  Check(f.chat->requests.size() == 3, "blank messages are ignored");
}

void SpokenTypedMessages() {
  Fixture f(false);  // A voice pipeline, microphone closed.
  f.pipeline->SubmitText("今天天气怎么样", true);
  const auto prompt = f.chat->requests.back()["messages"][0]["content"].get<std::string>();
  Check(prompt.find("语音表达：") != std::string::npos && prompt.find("文字表达：") == std::string::npos,
        "a typed message read aloud uses the pronunciation-safe speech rules");
  Check(f.history[0].data == json({{"input", "text"}}), "a spoken reply to a typed message is still marked as typed");
  f.Stream({"今天是晴天。明天", "可能下雨。"});
  Check(f.synth->spoken.size() == 1 && f.synth->spoken[0].second == "今天是晴天。", "the typed reply is synthesised");
  f.Synthesize(4800);
  Check(!f.played.empty() && f.state == "speaking", "the typed reply plays");
  f.heard = 2400;
  f.pipeline->StopReply();
  Check(f.pipeline->Context().back() == json({{"role", "assistant"}, {"content", "今天是晴天。明天可能下雨。"}}),
        "a stopped spoken reply to a typed message keeps all the text that was shown");

  const auto spoken = f.synth->spoken.size();
  f.pipeline->SubmitText("谢谢", false);
  Check(f.chat->requests.back()["messages"][0]["content"].get<std::string>().find("文字表达：") != std::string::npos,
        "with speech off, a voice pipeline answers typed messages in text");
  f.Stream({"不客气。"});
  Check(f.synth->spoken.size() == spoken && f.state == "idle", "a reply with speech off is not spoken");
}

void DesktopImage() {
  Fixture f(false, false, false);
  f.pipeline->SubmitText("看看我的桌面");
  f.Stream({}, {{"call_d", "view_desktop", "{}"}});
  f.pipeline->CompleteTool("call_d", {{"ok", true}, {"width", 1920}, {Voice::ToolImageField, "SlBFRw=="}});
  const auto follow = f.chat->requests.back()["messages"];
  const auto& tool = follow[follow.size() - 2];
  Check(tool["role"] == "tool" && tool["content"].get<std::string>().find("SlBFRw==") == std::string::npos &&
        json::parse(tool["content"].get<std::string>()).value("width", 0) == 1920, "the tool result carries no image data");
  Check(follow.back()["role"] == "user" && follow.back()["content"][1]["type"] == "image_url" &&
        follow.back()["content"][1]["image_url"]["url"] == "data:image/jpeg;base64,SlBFRw==",
        "the screenshot follows the tool result as an image for the model");
  f.Stream({"你在看视频。"});
  Check(f.pipeline->Context().dump().find("SlBFRw==") == std::string::npos &&
        f.pipeline->Context().back() == json({{"role", "assistant"}, {"content", "你在看视频。"}}),
        "the screenshot stays out of the context");
  f.pipeline->SubmitText("谢谢");
  Check(f.chat->requests.back()["messages"].dump().find("image_url") == std::string::npos, "the next reply sends no screenshot");
  bool historyClean = true;
  for (const auto& event : f.history) historyClean = historyClean && event.data.dump().find("SlBFRw==") == std::string::npos;
  Check(historyClean, "the screenshot is not recorded in the history");
}

json LongContext() {
  return json::array({{{"role", "user"}, {"content", "记住我的偏好"}},
      {{"role", "assistant"}, {"content", std::string(18000, 'x')}},
      {{"role", "user"}, {"content", "最近的话题"}},
      {{"role", "assistant"}, {"content", "最近的回答"}}});
}

size_t CompactionWindow(const json& context) {
  Fixture probe(false, false, false, 1000000);
  probe.pipeline->SetContext(context);
  probe.pipeline->SubmitText("继续");
  const auto tokens = Voice::EstimateContextTokens(probe.chat->requests.back()) + 1024;
  // ceil(tokens / 0.8): floor(window * 0.8) is exactly tokens.
  return (tokens * 5 + 3) / 4;
}

void ContextCompaction() {
  const auto context = LongContext();
  const auto window = CompactionWindow(context);
  Fixture below(false, false, false, window + 2);
  below.pipeline->SetContext(context);
  below.pipeline->SubmitText("继续");
  Check(below.chat->requests.back().contains("tools"), "one token below 80% does not compact");

  Fixture f(false, false, false, window);
  f.pipeline->SetContext(context);
  f.pipeline->SubmitText("继续");
  const auto before = f.pipeline->Context();
  const auto& summaryRequest = f.chat->requests.back();
  Check(!summaryRequest.contains("tools") && summaryRequest["model"] == "deepseek-chat" &&
        summaryRequest["thinking"]["type"] == "disabled" && f.pipeline->WaitingForReply(),
        "at 80% a private, tool-free summary request uses the same model and provider options");
  Check(Voice::EstimateContextTokens(summaryRequest["messages"]) + 1024 < window,
        "summary input leaves room for its output");
  f.chat->pending.push_back({ChatEvent::Type::Text, "用户喜欢橘子。", {}, {}});
  f.pipeline->Tick();
  Check(f.pipeline->Context() == before && f.history.size() == 2 && f.played.empty(),
        "partial summary is neither committed nor shown nor spoken");
  f.chat->pending.push_back({ChatEvent::Type::Done, {}, {}, "stop"});
  f.pipeline->Tick();
  Check(f.chat->requests.size() == 2 && f.chat->requests.back().contains("tools") &&
        f.pipeline->Context()[0]["content"].get<std::string>().find("用户喜欢橘子") != std::string::npos &&
        f.pipeline->Context()[1]["content"] == "最近的话题" && f.pipeline->Context().back()["content"] == "继续",
        "successful compaction replaces old history with a summary, retains recent turns and resumes");
  f.Stream({"好的"});
  Check(!f.pipeline->Replying() && f.history.back().type == Voice::ConversationEvent::Type::Completed,
        "the resumed reply completes normally");
  Fixture restored(false, false, false, window);
  restored.pipeline->SetContext(f.pipeline->Context());
  restored.pipeline->SubmitText("还有呢");
  Check(restored.chat->requests.back()["messages"][1] == f.pipeline->Context()[0],
        "the summary survives pipeline recreation");

  auto many = json::array();
  for (size_t i = 0; i < 60; ++i) many.push_back({{"role", i % 2 ? "assistant" : "user"}, {"content", "你好"}});
  Fixture shortTurns(false, false, false);
  shortTurns.pipeline->SetContext(many);
  shortTurns.pipeline->SubmitText("继续");
  Check(shortTurns.pipeline->Context().size() == 61 && shortTurns.chat->requests.back().contains("tools"),
        "short conversations are no longer silently discarded after 40 messages");
  Check(Voice::EstimateContextTokens(json("中文")) > Voice::EstimateContextTokens(json("abcd")),
        "token estimation accounts for Chinese, not only byte-count divided by four");
}

void CompactionFailures() {
  const auto context = LongContext();
  const auto window = CompactionWindow(context);
  for (int mode = 0; mode < 6; ++mode) {
    Fixture f(false, false, true, window);
    // Use typed input even with a synthesizer: no summary may reach speech.
    f.pipeline->SetContext(context);
    f.pipeline->SubmitText("继续");
    const auto before = f.pipeline->Context();
    if (mode == 0) f.chat->pending.push_back({ChatEvent::Type::Failed, "network error", {}, {}});
    else {
      if (mode != 1) f.chat->pending.push_back({ChatEvent::Type::Text, mode == 4 ? std::string(16000, '"') : "摘要", {}, {}});
      f.chat->pending.push_back({ChatEvent::Type::Done, {}, mode == 3 ?
          std::vector<Voice::ToolCall>{{"bad", "game_action", "{}"}} : std::vector<Voice::ToolCall>{}, mode == 2 ? "length" : mode == 5 ? "" : "stop"});
    }
    f.pipeline->Tick();
    Check(f.pipeline->Context() == before && !f.pipeline->Replying() && f.chat->requests.size() == 1 &&
          f.tools.empty() && f.synth->spoken.empty() && f.message.find("压缩失败") != std::string::npos,
          "failed, empty, truncated, tool-calling, non-shrinking or unterminated summaries retain original history and stop safely");
  }
  for (bool reset : {false, true}) {
    Fixture f(false, false, false, window);
    f.pipeline->SetContext(context);
    f.pipeline->SubmitText("继续");
    const auto before = f.pipeline->Context();
    f.chat->pending.push_back({ChatEvent::Type::Text, "不能保存的部分摘要", {}, {}});
    f.pipeline->Tick();
    if (reset) f.pipeline->Reset(); else f.pipeline->StopReply();
    f.Stream({"迟到的摘要"});
    Check(f.pipeline->Context() == before && !f.pipeline->Replying(),
          "stop and reset discard partial/late summaries without altering context");
    f.pipeline->SubmitText("新的问题");
    Check(!f.chat->requests.back().contains("tools"), "an interrupted compaction can be retried by the next turn");
  }
  Fixture huge(false, false, false, 10000);
  huge.pipeline->SubmitText(std::string(60000, 'x'));
  Check(huge.chat->requests.empty() && huge.pipeline->Context().size() == 1 && !huge.pipeline->Replying(),
        "a single uncompressible oversized user input is not sent or discarded");
}

void ToolContextCompaction() {
  Fixture f(false, false, false, 16000);
  f.pipeline->SubmitText("查询数据再继续");
  f.Stream({}, {{"a", "get_game_profile", "{}"}, {"b", "get_game_profile", "{}"}});
  f.pipeline->CompleteTool("a", {{"ok", true}, {"data", std::string(16000, 'x')}});
  Check(f.chat->requests.size() == 1, "compaction waits for all results of parallel tool calls");
  f.pipeline->CompleteTool("b", {{"ok", true}, {"data", std::string(16000, 'x')}});
  Check(f.chat->requests.size() == 2 && !f.chat->requests.back().contains("tools"),
        "tool results crossing 80% trigger compaction within the same turn");
  const auto prefix = json::parse(f.chat->requests.back()["messages"][1]["content"].get<std::string>());
  Check(prefix.size() == 4 && prefix[1]["tool_calls"].size() == 2 && prefix[2]["tool_call_id"] == "a" &&
        prefix[3]["tool_call_id"] == "b", "compaction never splits tool calls from their results");
  f.Stream({"两个查询均已成功，结果已记录。"});
  Check(f.chat->requests.back().contains("tools") && f.pipeline->Context().size() == 2 &&
        f.pipeline->Context().back()["content"] == "查询数据再继续", "even when a whole tool round is summarized the latest user request is pinned");
  f.Stream({"查好了"});
  Check(!f.pipeline->Replying(), "tool-round compaction resumes and completes");

  Fixture longUser(false, false, false, 20000);
  const auto userText = std::string(18000, 'u');
  longUser.pipeline->SubmitText(userText);
  longUser.Stream({}, {{"long-a", "get_game_profile", "{}"}, {"long-b", "get_game_profile", "{}"}});
  longUser.pipeline->CompleteTool("long-a", {{"ok", true}, {"data", std::string(16000, 'x')}});
  longUser.pipeline->CompleteTool("long-b", {{"ok", true}, {"data", std::string(16000, 'x')}});
  Check(!longUser.chat->requests.back().contains("tools") &&
        json::parse(longUser.chat->requests.back()["messages"][1]["content"].get<std::string>()).size() == 4,
        "a long pinned user is not incorrectly counted as removable history");
  longUser.Stream({"查询已成功"});
  Check(longUser.chat->requests.back().contains("tools") && longUser.pipeline->Context().back()["content"] == userText,
        "long user input survives tool compaction without a pointless user-only summary");

  Fixture voiceInterrupt(false, false, true, 16000);
  voiceInterrupt.pipeline->BeginInput(true);
  voiceInterrupt.Hear("查询数据");
  voiceInterrupt.Say(Tone(0.5));
  voiceInterrupt.pipeline->EndInput();
  voiceInterrupt.pipeline->Tick();
  voiceInterrupt.Stream({"我先查询。"}, {{"voice-a", "get_game_profile", "{}"}, {"voice-b", "get_game_profile", "{}"}});
  voiceInterrupt.Synthesize(4800);
  voiceInterrupt.heard = 4800;
  voiceInterrupt.pipeline->CompleteTool("voice-a", {{"ok", true}, {"data", std::string(16000, 'x')}});
  voiceInterrupt.pipeline->CompleteTool("voice-b", {{"ok", true}, {"data", std::string(16000, 'x')}});
  const auto original = voiceInterrupt.pipeline->Context();
  voiceInterrupt.pipeline->StopReply();
  Check(voiceInterrupt.pipeline->Context() == original,
        "interrupting compaction while an earlier tool preamble plays does not duplicate that preamble in context");

  Fixture chunks(false, false, false, 20000);
  auto many = json::array();
  for (int i = 0; i < 6; ++i) {
    many.push_back({{"role", "user"}, {"content", "旧问题"}});
    many.push_back({{"role", "assistant"}, {"content", std::string(18000, 'x')}});
  }
  chunks.pipeline->SetContext(many);
  chunks.pipeline->SubmitText("继续");
  size_t summaries = 0;
  while (chunks.pipeline->Replying() && !chunks.chat->requests.back().contains("tools") && summaries < 8) {
    Check(Voice::EstimateContextTokens(chunks.chat->requests.back()["messages"]) + 1024 < 20000,
          "restored oversized history is summarized in requests that each fit");
    chunks.Stream({"合并后的历史摘要"});
    ++summaries;
  }
  Check(summaries > 1 && chunks.chat->requests.back().contains("tools") && chunks.pipeline->Context().back()["content"] == "继续",
        "bounded incremental compaction handles history larger than the selected model window");
}

void WorkerThread() {
  Fixture f(true, true);
  const auto settle = [&](const std::function<bool()>& done) {
    for (int i = 0; i < 200 && !done(); ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); f.pipeline->Tick(); }
    return done();
  };
  f.pipeline->BeginInput(false);
  f.Turn("今天天气怎么样");
  Check(settle([&] { return !f.chat->requests.empty(); }), "recognition on the worker thread starts the chat request");
}

void PushToTalk() {
  Fixture f(false);
  f.pipeline->BeginInput(false);  // Without local turn models every press is push-to-talk.
  f.Hear("你好");
  f.Say(Tone(0.5));
  f.pipeline->EndInput();
  f.pipeline->Tick();
  Check(f.transcribed.size() == 1 && f.transcribed[0] == 16000 && f.chat->requests.size() == 1,
        "push-to-talk transcribes everything said while the key was down");
  f.pipeline->BeginInput(true);
  f.pipeline->EndInput();
  f.pipeline->Tick();
  Check(f.transcribed.size() == 1, "a click without audio is not transcribed");
}
}  // namespace

int main() {
  try {
    Text();
    Parser();
    Providers();
    Conversation();
    SynthesisFailure();
    TypedMessages();
    SpokenTypedMessages();
    DesktopImage();
    ContextCompaction();
    CompactionFailures();
    ToolContextCompaction();
    WorkerThread();
    PushToTalk();
    std::cout << "PASS voice cascade pipeline\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
