#include "VoiceSession.hpp"
#include "VoiceEventQueue.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using nlohmann::json;
void Check(bool condition, const char* description) {
  if (!condition) throw std::runtime_error(description);
}
struct Fixture {
  std::vector<json> sent;
  std::string played, state, error, transcript;
  bool playing = false;
  Voice::Session session{{
    [this](const json& event) { sent.push_back(event); },
    [this](const std::string& pcm) { played += pcm; playing = true; },
    [this] { return playing; },
    [this] { playing = false; },
    [this](const std::string& value, const std::string& message) { state = value; error = message; },
    [this](const std::string& value) { transcript = value; }
  }};
  void Connect() {
    session.Receive({{"type", "session.created"}});
    session.Receive({{"type", "session.updated"}});
  }
  size_t Count(const std::string& type) const {
    size_t count = 0;
    for (const auto& event : sent) if (event["type"] == type) ++count;
    return count;
  }
  void Turn() {
    session.BeginInput();
    session.AppendInput(std::string(6400, '\0'));
    session.EndInput();
  }
  void Reply(const std::string& id = "r1", const std::string& item = "i1") {
    session.Receive({{"type", "response.created"}, {"response", {{"id", id}}}});
    session.Receive({{"type", "response.output_item.added"}, {"response_id", id},
        {"item", {{"id", item}, {"type", "message"}, {"role", "assistant"}}}});
  }
};
}

int main() {
  try {
    Check(Voice::ValidWorkspace("123456") && Voice::ValidWorkspace("ws-abc123"), "valid workspace IDs");
    for (const auto& invalid : {"", "evil.example", "a/../../b", "x?model=other", "bad\nheader", "-id", "id-"})
      Check(!Voice::ValidWorkspace(invalid), "workspace cannot replace the credential destination");
    Check(!Voice::ValidWorkspace(std::string(64, 'a')), "workspace fits one DNS label");
    Check(Voice::ConnectionUrl("ws-123") == "wss://ws-123.cn-beijing.maas.aliyuncs.com/api-ws/v1/realtime?model=qwen3.8-omni-flash-realtime", "workspace endpoint");
    Check(Voice::Trim(" \n sk-test \r") == "sk-test" && !Voice::ValidApiKey("sk-test\r\nHeader: injected"), "credential header validation");
    for (size_t count = 0; count < 300; ++count) {
      std::string bytes;
      for (size_t i = 0; i < count; ++i) bytes += static_cast<char>((i * 73) & 255);
      Check(Voice::DecodeBase64(Voice::EncodeBase64(bytes)) == bytes, "binary PCM round trip");
    }
    for (const auto& invalid : {"a", "====", "AA=A", "AA==AAAA", "##AA"}) {
      bool threw = false;
      try { Voice::DecodeBase64(invalid); } catch (const std::invalid_argument&) { threw = true; }
      Check(threw, "malformed Base64 is rejected");
    }

    Fixture first;
    first.session.BeginInput();
    first.session.AppendInput(std::string(6400, '\x12'));
    first.session.EndInput();
    Check(first.sent.empty(), "no audio before session is configured");
    first.Connect();
    Check(first.Count("session.update") == 1 && first.Count("input_audio_buffer.commit") == 1 && first.Count("response.create") == 1, "first turn survives release before connect");
    const auto setup = first.sent.front()["session"];
    Check(setup["turn_detection"].is_null() && setup["audio"]["input"]["format"]["sample_rate"] == 16000 &&
        setup["audio"]["output"]["format"]["sample_rate"] == 24000, "manual PCM protocol");
    std::string uploaded;
    for (const auto& event : first.sent) if (event["type"] == "input_audio_buffer.append") uploaded += Voice::DecodeBase64(event["audio"]);
    Check(uploaded == std::string(6400, '\x12'), "all first-turn samples are uploaded in order");
    first.session.AppendInput(std::string(6400, '\x34'));
    Check(first.Count("input_audio_buffer.append") == 2, "microphone is gated after release");

    Fixture tap;
    tap.Connect();
    tap.session.BeginInput();
    tap.session.AppendInput(std::string(640, '\0'));
    tap.session.EndInput();
    Check(tap.Count("response.create") == 0 && tap.Count("input_audio_buffer.clear") == 1, "short accidental tap does not submit a turn");

    Fixture interrupt;
    interrupt.Connect(); interrupt.Turn(); interrupt.Reply();
    interrupt.session.Receive({{"type", "response.audio.delta"}, {"delta", Voice::EncodeBase64(std::string(4800, '\0'))}});
    interrupt.session.BeginInput();
    Check(!interrupt.playing && interrupt.Count("response.cancel") == 1, "press stops speaker and cancels generation");
    Check(interrupt.sent.back()["type"] == "response.cancel" && interrupt.sent.back().size() == 2,
        "cancellation uses the documented Qwen event fields");
    const auto played = interrupt.played;
    interrupt.session.Receive({{"type", "response.audio.delta"}, {"delta", Voice::EncodeBase64(std::string(4800, '\1'))}});
    Check(interrupt.played == played, "late canceled speech is not played");
    interrupt.session.AppendInput(std::string(6400, '\0'));
    interrupt.session.EndInput();
    Check(interrupt.Count("response.create") == 1, "new response waits for old cancellation acknowledgement");
    interrupt.session.Receive({{"type", "response.done"}, {"response", {{"id", "r1"}, {"status", "cancelled"}}}});
    Check(interrupt.Count("response.create") == 2 && interrupt.Count("input_audio_buffer.commit") == 2, "pending next turn starts exactly once");
    Check(interrupt.Count("session.update") == 1, "successive turns reuse the conversation");
    interrupt.Reply("r2", "i2");
    interrupt.session.Receive({{"type", "response.audio.delta"}, {"response_id", "r1"},
        {"delta", Voice::EncodeBase64(std::string(4800, '\1'))}});
    Check(interrupt.played == played, "old response audio cannot enter a new reply");
    interrupt.session.Receive({{"type", "response.done"}, {"response", {{"id", "r1"}, {"status", "completed"}}}});
    Check(interrupt.session.Busy(), "old response completion cannot finish a new reply");
    interrupt.session.Receive({{"type", "response.audio_transcript.done"}, {"response_id", "r2"}, {"transcript", "你好"}});
    Check(interrupt.transcript == "你好", "complete response transcript is visible");

    Fixture early;
    early.Connect(); early.Turn();
    early.session.BeginInput();
    Check(early.Count("response.cancel") == 0, "early cancellation waits for response creation");
    early.Reply();
    Check(early.Count("response.cancel") == 1, "pending creation is canceled exactly once after ID arrives");
    early.session.BeginInput();
    Check(early.Count("response.cancel") == 1, "repeated presses do not send duplicate cancellations");

    Fixture failed;
    failed.Connect(); failed.Turn(); failed.Reply();
    failed.session.Receive({{"type", "response.done"}, {"response", {{"status", "failed"}}}});
    Check(failed.state == "error", "failed response remains an error");
    failed.session.Receive({{"type", "error"}, {"error", {{"code", "invalid_api_key"}}}});
    Check(!failed.session.Ready() && !failed.session.Busy() && failed.state == "error" && failed.error.find("认证失败") != std::string::npos, "authentication error resets the session");

    Voice::EventQueue queue;
    queue.connection = 2;
    queue.Network(1, Voice::Event::Type::Error, "old connection");
    queue.Network(2, Voice::Event::Type::Message, "current connection");
    auto events = queue.Poll();
    Check(events.size() == 1 && events[0].data == "current connection", "closed connections cannot poison a new session");
    queue.capture = 2;
    queue.capturing = true;
    queue.Captured(1, Voice::Event::Type::Microphone, "old recording");
    queue.Captured(1, Voice::Event::Type::Error, "old failure");
    queue.Captured(2, Voice::Event::Type::Microphone, "current recording");
    events = queue.Poll();
    Check(events.size() == 1 && events[0].data == "current recording" && queue.capturing,
        "old audio callbacks cannot stop or feed a new recording");
    queue.capturing = false;
    queue.Captured(2, Voice::Event::Type::Microphone, "released recording");
    Check(queue.Poll().empty(), "audio callbacks after release are ignored");
    queue.Push(Voice::Event::Type::Microphone, std::string(8 * 1024 * 1024 + 1, '\0'));
    events = queue.Poll();
    Check(events.size() == 1 && events[0].type == Voice::Event::Type::Error, "audio callback backlog is bounded");
    std::cout << "PASS voice protocol: first connection, release, multi-turn context, cancellation, credentials and bounded callbacks\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
