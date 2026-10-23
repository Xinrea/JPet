#include "VoiceCascade.hpp"
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
using Voice::Event;

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
  std::vector<std::pair<int, std::string>> connects;
  std::vector<int> disconnects;
  std::vector<std::pair<int, json>> sent;
  std::vector<Voice::ToolCall> tools;
  std::vector<Voice::ConversationEvent> history;
  std::string played, state;
  bool playing = false;
  uint64_t heard = 0;
  int stops = 0;
  // What local recognition "hears" next, and the audio it was given.
  std::mutex mutex;
  std::string next;
  std::vector<size_t> transcribed;
  std::unique_ptr<Voice::Pipeline> pipeline;

  explicit Fixture(bool handsFree = true, bool threaded = false) {
    auto fake = std::make_unique<FakeChat>();
    chat = fake.get();
    Voice::CascadeConfig config{"ws-1", "bailian-key", "https://chat.example/v1/chat/completions", "chat-key", "qwen-plus",
                                true, "cosyvoice-v3-flash", "longanhuan"};
    Voice::Pipeline::Callbacks callbacks{
      [this](int channel, const std::string& url, const std::string& key) {
        Check(key == "bailian-key", "synthesis uses the Bailian key");
        connects.emplace_back(channel, url);
      },
      [this](int channel) { disconnects.push_back(channel); },
      [this](int channel, const json& message) { sent.emplace_back(channel, message); },
      [this](const std::string& pcm) { played += pcm; playing = true; },
      [this] { return playing; },
      [this] { return heard; },
      [this] { ++stops; playing = false; heard = 0; },
      [this](const std::string& value, const std::string&) { state = value; },
      [](const std::string&) {},
      [this](const std::vector<Voice::ToolCall>& calls) { tools.insert(tools.end(), calls.begin(), calls.end()); },
      [this](const Voice::ConversationEvent& event) { history.push_back(event); },
      [] {},
      [](const std::string&) {}};
    std::unique_ptr<Voice::TurnAnalyzer> analyzer;
    if (handsFree) analyzer = std::make_unique<Voice::TurnAnalyzer>(std::make_unique<Voice::TurnDetector>(
        LoudVad, [](const std::vector<int16_t>&) { return 0.9f; }), false);
    pipeline = std::make_unique<Voice::Pipeline>(config, callbacks, json::array({{{"type", "function"}, {"function", {{"name", "get_game_profile"}}}}}),
        std::move(analyzer), std::move(fake), [this](const std::string& pcm) {
          std::lock_guard<std::mutex> lock(mutex);
          transcribed.push_back(pcm.size());
          return next;
        }, threaded);
  }
  void Hear(const std::string& text) { std::lock_guard<std::mutex> lock(mutex); next = text; }
  void Receive(int channel, const json& message) { pipeline->Receive({Event::Type::Message, message.dump(), channel}); pipeline->Tick(); }
  void Audio(size_t samples) { pipeline->Receive({Event::Type::Binary, std::string(samples * 2, '\1'), Voice::SynthesisChannel}); }
  void Say(const std::string& pcm) {
    for (size_t offset = 0; offset < pcm.size(); offset += 640) { pipeline->AppendInput(pcm.substr(offset, 640)); pipeline->Tick(); }
  }
  // One spoken turn: silence, speech, then the pause that ends it.
  void Turn(const std::string& text) {
    Hear(text);
    Say(Tone(1.0));
    Say(Tone(0.4, 0));
  }
  size_t Count(int channel, const std::string& action) const {
    size_t count = 0;
    for (const auto& [c, message] : sent)
      if (c == channel && message.contains("header") && message["header"].value("action", std::string{}) == action) ++count;
    return count;
  }
  json Last(int channel, const std::string& action) const {
    for (auto it = sent.rbegin(); it != sent.rend(); ++it)
      if (it->first == channel && it->second.contains("header") && it->second["header"]["action"] == action) return it->second;
    return nullptr;
  }
  // Runs one synthesis task: started, audio, finished.
  void Synthesize(size_t samples) {
    const auto task = Last(Voice::SynthesisChannel, "run-task")["header"]["task_id"];
    Receive(Voice::SynthesisChannel, {{"header", {{"event", "task-started"}, {"task_id", task}}}});
    Audio(samples);
    Receive(Voice::SynthesisChannel, {{"header", {{"event", "task-finished"}, {"task_id", task}}}});
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
  Check(Voice::ChatCompletionsUrl("", "ws-1") == "https://ws-1.cn-beijing.maas.aliyuncs.com/compatible-mode/v1/chat/completions" &&
        Voice::ChatCompletionsUrl("https://api.deepseek.com/v1/", "ws-1") == "https://api.deepseek.com/v1/chat/completions", "chat endpoints");
  Check(Voice::ValidChatBaseUrl("https://api.deepseek.com/v1") && Voice::ValidChatBaseUrl("http://127.0.0.1:11434/v1") &&
        !Voice::ValidChatBaseUrl("http://example.com/v1") && !Voice::ValidChatBaseUrl("https://user:pass@example.com") &&
        !Voice::ValidChatBaseUrl("https://example.com/v1#x") && !Voice::ValidChatBaseUrl("ftp://example.com"), "chat base URL validation");
}

void Conversation() {
  Fixture f;
  f.pipeline->BeginInput(false);
  Check(f.connects.empty(), "opening the microphone connects nothing: recognition is local");
  f.Say(Tone(0.5, 0));
  Check(f.transcribed.empty(), "silence is not transcribed");
  f.Turn("今天天气怎么样");
  Check(f.transcribed.size() == 1 && f.transcribed[0] >= size_t(1.6 * 32000), "the whole utterance with pre-roll is transcribed once");
  Check(f.chat->requests.size() == 1, "a transcript starts the chat request");
  const auto& body = f.chat->requests[0];
  Check(body["stream"] == true && body["enable_thinking"] == false && body["tools"].size() == 1 &&
        body["messages"][0]["role"] == "system" && body["messages"][1] == json({{"role", "user"}, {"content", "今天天气怎么样"}}),
        "the chat request carries the prompt, tools and the user's words");
  Check(f.connects.size() == 1 && f.connects[0].first == Voice::SynthesisChannel &&
        f.connects[0].second == "wss://ws-1.cn-beijing.maas.aliyuncs.com/api-ws/v1/inference", "synthesis connects with the reply");

  f.Stream({"好的，今天", "是晴天。明天", "可能下雨。"});
  auto run = f.Last(Voice::SynthesisChannel, "run-task");
  Check(run["payload"]["model"] == "cosyvoice-v3-flash" && run["payload"]["parameters"]["voice"] == "longanhuan" &&
        run["payload"]["parameters"]["format"] == "pcm" && run["payload"]["parameters"]["sample_rate"] == 24000,
        "sentences become CosyVoice tasks");
  f.Synthesize(4800);
  Check(f.Last(Voice::SynthesisChannel, "continue-task")["payload"]["input"]["text"] == "好的，今天是晴天。" &&
        f.played.size() == 9600 && f.state == "speaking", "the first sentence plays as soon as it is synthesised");
  Check(f.Count(Voice::SynthesisChannel, "run-task") == 2, "the next sentence starts after the previous task finishes");
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
  const auto disconnects = f.disconnects.size();
  const int before = f.stops;
  f.Hear("从前有一座山");
  f.Say(Tone(0.8));
  Check(f.stops == before, "speech matching the reply is treated as echo and does not interrupt");
  f.Hear("等一下");
  f.Say(Tone(0.6));
  Check(f.stops == before + 1 && f.chat->cancels > 0 && f.disconnects.size() == disconnects + 1 &&
        f.disconnects.back() == Voice::SynthesisChannel, "a partial transcript that is not echo interrupts playback, chat and synthesis");
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
    Conversation();
    WorkerThread();
    PushToTalk();
    std::cout << "PASS voice cascade pipeline\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
