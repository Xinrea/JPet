#include "VoiceSession.hpp"
#include "VoiceEventQueue.hpp"
#include "VoiceShortcut.hpp"
#include "VoiceHistory.hpp"
#include "voice_history_helpers.hpp"

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
  Voice::History history;
  Voice::Session session{{
    [this](const json& event) { sent.push_back(event); },
    [this](const std::string& pcm) { played += pcm; playing = true; },
    [this] { return playing; },
    [this] { playing = false; },
    [this](const std::string& value, const std::string& message) { state = value; error = message; },
    [this](const std::string& value) { transcript = value; },
    {},
    [this](const Voice::ConversationEvent& event) { history.Apply(event); }
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
  void Speech(const std::string& item = "u1") {
    session.Receive({{"type", "input_audio_buffer.speech_started"}, {"item_id", item}});
    session.Receive({{"type", "input_audio_buffer.speech_stopped"}, {"item_id", item}});
    session.Receive({{"type", "input_audio_buffer.committed"}, {"item_id", item}});
  }
  void Turn(const std::string& item = "u1") {
    session.BeginInput();
    session.AppendInput(std::string(6400, '\0'));
    session.EndInput();
    Speech(item);
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
    Check(first.Count("session.update") == 1 && first.Count("input_audio_buffer.commit") == 0 && first.Count("response.create") == 0, "release before connection uploads audio without forcing a reply");
    const auto setup = first.sent.front()["session"];
    Check(setup["turn_detection"]["type"] == "semantic_vad" && setup["turn_detection"]["silence_duration_ms"] == Voice::InputSilenceMs && setup["audio"]["input"]["format"]["sample_rate"] == 16000 &&
        setup["audio"]["output"]["format"]["sample_rate"] == 24000, "server semantic VAD and PCM protocol");
    Check(setup["input_audio_transcription"]["model"] == "qwen3-asr-flash-realtime", "user speech transcription is enabled");
    Check(TurnSnapshot(first.history).empty(), "upload alone does not create a history entry");
    std::string uploaded;
    for (const auto& event : first.sent) if (event["type"] == "input_audio_buffer.append") uploaded += Voice::DecodeBase64(event["audio"]);
    Check(uploaded == std::string(6400, '\x12') + std::string(Voice::InputTailBytes, '\0'), "all first-turn samples are uploaded in order");
    const auto firstChunks = first.Count("input_audio_buffer.append");
    first.session.AppendInput(std::string(6400, '\x34'));
    Check(first.Count("input_audio_buffer.append") == firstChunks, "microphone is gated after release");

    Fixture tap;
    tap.Connect();
    tap.session.BeginInput();
    tap.session.AppendInput(std::string(640, '\0'));
    tap.session.EndInput();
    Check(tap.Count("response.create") == 0 && tap.Count("input_audio_buffer.clear") == 0, "short audio is left to server VAD without clearing its buffer");
    Check(TurnSnapshot(tap.history).empty(), "accidental taps do not create history");

    using Action = Voice::ShortcutControl::Action;
    Voice::ShortcutControl toggleKey, holdKey;
    Check(toggleKey.Update(true, false, false) == Action::Begin &&
        toggleKey.Update(true, false, true) == Action::None &&
        toggleKey.Update(false, false, true) == Action::None &&
        toggleKey.Update(true, false, true) == Action::End,
        "toggle starts on press, ignores holding/release, and stops on the next press");
    Check(holdKey.Update(true, true, false) == Action::Begin &&
        holdKey.Update(true, true, true) == Action::None &&
        holdKey.Update(false, true, true) == Action::End &&
        holdKey.Update(false, true, false) == Action::None,
        "hold starts once on press and stops once on release");
    holdKey.BlockUntilRelease(true);
    Check(holdKey.Update(true, true, false) == Action::None &&
        holdKey.Update(false, true, false) == Action::None &&
        holdKey.Update(true, true, false) == Action::Begin,
        "settings reset while held requires release before starting again");
    holdKey.BlockUntilRelease();
    Check(holdKey.Update(true, true, false) == Action::None &&
        holdKey.Update(false, true, false) == Action::None &&
        holdKey.Update(true, false, false) == Action::Begin,
        "capture failure does not repeatedly restart while held and recovery can switch modes");

    Fixture hold;
    hold.Connect(); hold.Turn("prior-input"); hold.Reply("prior-reply");
    const auto audio = [](const std::string& id, const std::string& pcm) {
      return json{{"type", "response.audio.delta"}, {"response_id", id}, {"delta", Voice::EncodeBase64(pcm)}};
    };
    hold.session.Receive(audio("prior-reply", "old!"));
    Check(hold.playing, "previous reply is playing before push-to-talk");
    hold.session.BeginInput(true);
    Check(!hold.playing && hold.session.Recording(), "hold press immediately interrupts playback without waiting for speech");
    hold.session.Receive(audio("prior-reply", "late"));
    Check(hold.played == "old!", "late audio from the interrupted reply is discarded");
    hold.played.clear();
    hold.session.AppendInput("pcm!"); hold.Speech("held-input"); hold.Reply("held-reply");
    hold.session.Receive(audio("held-reply", "one!"));
    hold.session.Receive(audio("held-reply", "two!"));
    hold.session.Receive({{"type", "response.done"}, {"response", {{"id", "held-reply"}, {"status", "completed"}}}});
    Check(!hold.playing && hold.played.empty() && hold.session.Recording(), "even a completed reply stays silent until release");
    hold.session.EndInput();
    Check(hold.playing && hold.played == "one!two!" && !hold.session.Recording(), "release stops recording and plays buffered audio in order");
    const auto holdUploads = hold.Count("input_audio_buffer.append");
    hold.session.AppendInput("echo");
    Check(hold.Count("input_audio_buffer.append") == holdUploads && hold.Count("response.cancel") == 0 && hold.Count("response.create") == 0,
        "reply playback cannot upload microphone audio in hold mode and VAD still owns replies");
    hold.session.BeginInput(true);
    hold.Speech("pause-input"); hold.Reply("pause-reply");
    hold.session.Receive(audio("pause-reply", "drop"));
    hold.Speech("resumed-input"); hold.Reply("resumed-reply");
    hold.session.Receive(audio("resumed-reply", "keep"));
    hold.session.EndInput();
    Check(hold.played == "one!two!keep", "resuming speech while held discards the reply from the earlier pause");
    hold.session.Receive(audio("resumed-reply", "tail"));
    Check(hold.played == "one!two!keeptail", "remaining response audio streams normally after release");
    hold.session.BeginInput(true);
    hold.Speech("reset-input"); hold.Reply("reset-reply");
    hold.session.Receive(audio("reset-reply", "lost"));
    hold.session.Reset(); hold.session.EndInput();
    Check(hold.played == "one!two!keeptail" && !hold.playing, "connection reset clears buffered half-duplex output");

    Fixture emptyHold;
    emptyHold.Connect(); emptyHold.session.BeginInput(true); emptyHold.session.EndInput();
    Check(emptyHold.Count("input_audio_buffer.append") == 0 && TurnSnapshot(emptyHold.history).empty(), "empty hold does not force a reply or create a turn");

    first.Speech(); first.Reply();
    Check(TurnSnapshot(first.history).size() == 1, "server commit starts one history turn");

    Fixture buffered;
    buffered.session.BeginInput(); buffered.session.AppendInput(std::string(6400, '\1')); buffered.session.EndInput();
    buffered.session.BeginInput(); buffered.session.AppendInput(std::string(6400, '\2')); buffered.session.EndInput();
    buffered.Connect();
    std::string queued;
    for (const auto& event : buffered.sent) if (event["type"] == "input_audio_buffer.append") queued += Voice::DecodeBase64(event["audio"]);
    Check(queued == std::string(6400, '\1') + std::string(Voice::InputTailBytes, '\0') +
        std::string(6400, '\2') + std::string(Voice::InputTailBytes, '\0'), "repeated presses during connection preserve all samples");

    Fixture continuous;
    continuous.Connect(); continuous.session.BeginInput();
    for (int i = 0; i < 650; ++i) continuous.session.AppendInput(std::string(3200, '\0'));
    Check(continuous.Count("input_audio_buffer.append") == 650 && continuous.session.Recording(),
        "microphone stays on and uploads beyond one minute without another key press");
    Check(!continuous.session.WaitingForReply(), "quiet open microphone is not a stalled reply");
    for (int i = 1; i <= 3; ++i) {
      continuous.Speech("continuous-user-" + std::to_string(i));
      Check(continuous.session.WaitingForReply(), "reply timeout also applies with microphone open");
      const auto id = "continuous-reply-" + std::to_string(i);
      continuous.Reply(id, "continuous-item-" + std::to_string(i));
      continuous.session.Receive({{"type", "response.done"}, {"response", {{"id", id}, {"status", "completed"}}}});
      Check(continuous.session.Recording() && !continuous.session.WaitingForReply() && continuous.state == "listening",
          "completed reply returns to listening while microphone stays open");
    }
    continuous.session.EndInput();
    const auto stoppedChunks = continuous.Count("input_audio_buffer.append");
    continuous.session.AppendInput(std::string(3200, '\1'));
    Check(!continuous.session.Recording() && continuous.Count("input_audio_buffer.append") == stoppedChunks,
        "explicit microphone off stops capture uploads");
    Fixture parallelText;
    parallelText.Connect(); parallelText.Turn(); parallelText.Reply();
    parallelText.session.Receive({{"type", "response.text.delta"}, {"response_id", "r1"}, {"delta", "同一段回复"}});
    parallelText.session.Receive({{"type", "response.audio_transcript.delta"}, {"response_id", "r1"}, {"delta", "同一段回复"}});
    parallelText.session.Receive({{"type", "response.text.done"}, {"response_id", "r1"}, {"text", "同一段回复"}});
    Check(parallelText.transcript == "同一段回复" && parallelText.history.Snapshot()["list"].size() == 2,
        "text and audio transcript streams cannot duplicate a reply or its message");

    Fixture interrupt;
    interrupt.Connect(); interrupt.Turn(); interrupt.Reply();
    interrupt.session.Receive({{"type", "response.audio.delta"}, {"delta", Voice::EncodeBase64(std::string(4800, '\0'))}});
    const auto sentBeforePress = interrupt.sent.size();
    interrupt.session.BeginInput();
    Check(interrupt.playing && interrupt.sent.size() == sentBeforePress, "press leaves playback and generation untouched");
    interrupt.session.Receive({{"type", "response.audio.delta"}, {"delta", Voice::EncodeBase64(std::string(4800, '\1'))}});
    interrupt.session.Receive({{"type", "response.audio_transcript.delta"}, {"delta", "继续回复"}});
    Check(interrupt.played.size() == 9600 && interrupt.transcript == "继续回复", "reply streams while the microphone is open");
    interrupt.session.AppendInput(std::string(640, '\0')); interrupt.session.EndInput();
    Check(interrupt.playing && interrupt.Count("response.cancel") == 0 && interrupt.Count("response.create") == 0 &&
        interrupt.Count("input_audio_buffer.commit") == 0 && interrupt.Count("input_audio_buffer.clear") == 0,
        "silent press and release do not affect the reply or force a new turn");
    Check(TurnSnapshot(interrupt.history).size() == 1 && TurnSnapshot(interrupt.history)[0]["state"] == "pending",
        "silent key press does not interrupt history");
    interrupt.session.BeginInput(); interrupt.session.AppendInput(std::string(6400, '\2'));
    interrupt.session.Receive({{"type", "input_audio_buffer.speech_started"}, {"item_id", "u2"}});
    Check(!interrupt.playing && interrupt.Count("response.cancel") == 0, "server speech event stops playback without client cancellation");
    const auto played = interrupt.played;
    interrupt.session.Receive({{"type", "response.audio.delta"}, {"response_id", "r1"}, {"delta", Voice::EncodeBase64(std::string(4800, '\1'))}});
    Check(interrupt.played == played, "audio from a server-interrupted reply is ignored");
    interrupt.session.Receive({{"type", "input_audio_buffer.speech_stopped"}, {"item_id", "u2"}});
    interrupt.session.Receive({{"type", "input_audio_buffer.committed"}, {"item_id", "u2"}});
    interrupt.Reply("r2", "i2");
    interrupt.session.Receive({{"type", "response.done"}, {"response", {{"id", "r1"}, {"status", "cancelled"}}}});
    Check(interrupt.session.Busy() && interrupt.Count("session.update") == 1, "late old completion cannot finish the new server response");
    interrupt.session.Receive({{"type", "response.audio_transcript.done"}, {"response_id", "r2"}, {"transcript", "你好"}});
    Check(interrupt.transcript == "你好", "complete response transcript is visible");
    interrupt.session.Receive({{"type", "response.done"}, {"response", {{"id", "r2"}, {"status", "completed"}}}});
    Check(interrupt.session.Recording() && TurnSnapshot(interrupt.history)[0]["state"] == "completed", "server can complete a turn while the microphone is on");
    interrupt.Speech("u3"); interrupt.Reply("r3", "i3"); interrupt.session.EndInput();
    Check(TurnSnapshot(interrupt.history).size() == 3 && interrupt.Count("response.create") == 0,
        "multiple server turns can share one key hold");

    Fixture conversation;
    conversation.Connect(); conversation.Turn(); conversation.Reply();
    conversation.session.Receive({{"type", "input_audio_buffer.committed"}, {"item_id", "u1"}});
    conversation.session.Receive({{"type", "response.audio_transcript.delta"}, {"response_id", "r1"}, {"delta", "第一轮回复"}});
    Check(TurnSnapshot(conversation.history)[0]["assistant"] == "第一轮回复", "streaming reply is visible in history");
    conversation.session.BeginInput();
    conversation.session.AppendInput(std::string(6400, '\0')); conversation.session.EndInput();
    conversation.Speech("u2");
    conversation.session.Receive({{"type", "response.done"}, {"response", {{"id", "r1"}, {"status", "cancelled"}}}});
    conversation.Reply("r2", "i2");
    conversation.session.Receive({{"type", "input_audio_buffer.committed"}, {"item_id", "u2"}});
    conversation.session.Receive({{"type", "conversation.item.input_audio_transcription.completed"}, {"item_id", "u2"}, {"transcript", "第二轮问题"}});
    conversation.session.Receive({{"type", "conversation.item.input_audio_transcription.completed"}, {"item_id", "u1"}, {"transcript", "第一轮问题"}});
    conversation.session.Receive({{"type", "conversation.item.input_audio_transcription.completed"}, {"item_id", "u1"}, {"transcript", "重复事件"}});
    conversation.session.Receive({{"type", "response.audio_transcript.delta"}, {"response_id", "r1"}, {"delta", "过期回复"}});
    conversation.session.Receive({{"type", "response.audio_transcript.done"}, {"response_id", "r2"}, {"transcript", "第二轮回复"}});
    conversation.session.Receive({{"type", "response.done"}, {"response", {{"id", "r2"}, {"status", "completed"}}}});
    auto history = TurnSnapshot(conversation.history);
    Check(history.size() == 2 && history[0]["user"] == "第二轮问题" && history[0]["assistant"] == "第二轮回复" &&
        history[0]["state"] == "completed", "latest round contains the correct question and answer");
    Check(history[1]["user"] == "第一轮问题" && history[1]["assistant"] == "第一轮回复" &&
        history[1]["state"] == "interrupted", "late ASR and canceled replies cannot corrupt adjacent rounds");

    conversation.session.Receive({{"type", "input_audio_buffer.committed"}, {"item_id", "u2"}});
    conversation.session.Receive({{"type", "input_audio_buffer.speech_stopped"}, {"item_id", "u2"}});
    Check(TurnSnapshot(conversation.history).size() == 2 && !conversation.session.Busy(),
        "duplicate server input events after ASR cannot create ghost turns or keep the session busy");

    conversation.Turn("u3"); conversation.Reply("r3", "i3");
    conversation.session.Receive({{"type", "input_audio_buffer.committed"}, {"item_id", "u3"}});
    conversation.session.Receive({{"type", "conversation.item.input_audio_transcription.failed"}, {"item_id", "u3"},
        {"error", {{"message", "private upstream data"}}}});
    conversation.session.Receive({{"type", "response.audio_transcript.done"}, {"response_id", "r3"}, {"transcript", "部分回复"}});
    conversation.session.Reset(true);
    history = TurnSnapshot(conversation.history);
    Check(history[0]["state"] == "failed" && history[0]["assistant"] == "部分回复" && history[0]["user"] == "" &&
        history.dump().find("private upstream data") == std::string::npos, "failed rounds keep partial text without raw server errors");

    using Event = Voice::ConversationEvent;
    json saved = json::array();
    Voice::History persisted(saved, [&](const json& data) { saved = data; });
    persisted.Apply({Event::Type::Started, 1, ""});
    persisted.Apply({Event::Type::UserTranscript, 1, "重启前的问题"});
    persisted.Apply({Event::Type::AssistantTranscript, 1, "重启前的回复"});
    persisted.Apply({Event::Type::Completed, 1, ""});
    persisted.Apply({Event::Type::Started, 2, ""});
    Voice::History restored(json::parse(saved.dump()));
    history = TurnSnapshot(restored);
    Check(history[0]["state"] == "interrupted" && history[1]["user"] == "重启前的问题" &&
        history[1]["assistant"] == "重启前的回复", "restart restores text and finalizes unfinished rounds");
    restored.Apply({Event::Type::Started, 1, ""});
    Check(TurnSnapshot(restored)[0]["id"] == 3, "IDs remain unique across restarts and session counters");
    for (uint64_t turn = 3; turn < 205; ++turn) persisted.Apply({Event::Type::Started, turn, ""});
    Check(TurnSnapshot(persisted).size() == Voice::History::Limit && saved.size() == Voice::History::Limit,
        "both memory and persisted history are capped");
    persisted.Apply({Event::Type::UserTranscript, 1, "已淘汰的旧轮次"});
    Check(TurnSnapshot(persisted).dump().find("已淘汰的旧轮次") == std::string::npos, "evicted turns cannot reappear");
    Voice::History malformed(json::array({nullptr, "invalid", json{{"id", 1}}}));
    Check(TurnSnapshot(malformed).empty(), "malformed saved history is ignored");
    Voice::History migrated(json::array({json{{"id", 8}, {"created_at", 123}, {"user", "旧问题"}, {"assistant", "旧回复"}, {"state", "completed"}},
        json{{"id", 9}, {"created_at", 124}, {"user", "未完成的问题"}, {"assistant", "部分回复"}, {"state", "pending"}}}));
    auto flat = migrated.Snapshot()["list"];
    Check(flat.size() == 4 && flat[0]["role"] == "user" && flat[1]["role"] == "assistant" && flat[1]["text"] == "旧回复" && flat[3]["state"] == "interrupted",
        "legacy pairs migrate into chronological independent messages");
    json messageSave;
    Voice::History timeline(json::array(), [&](const json& data) { messageSave = data; });
    timeline.Apply({Event::Type::Started, 1, ""});
    timeline.Apply({Event::Type::AssistantTranscript, 1, "我先看看。", "response-1"});
    timeline.Apply({Event::Type::ToolStarted, 1, "jpet_settings", "call-1", {{"action", "update"}, {"section", "audio"}, {"settings", {{"volume", 40}, {"api_key", "private-key"}}}, {"credentials", "private-key"}}});
    timeline.Apply({Event::Type::ToolCompleted, 1, "", "call-1", {{"ok", true}, {"settings", {{"audio", {{"volume", 40}, {"token", "private-key"}}}}}, {"audio", "raw-audio"}, {"image", "raw-image"}, {"raw", "private-raw"}}});
    timeline.Apply({Event::Type::AssistantTranscript, 1, "音量已经调整好了。", "response-2"});
    timeline.Apply({Event::Type::Completed, 1, ""});
    timeline.Apply({Event::Type::UserTranscript, 1, "音量调到四十"});
    flat = timeline.Snapshot()["list"];
    Check(flat.size() == 4 && flat[0]["text"] == "音量调到四十" && flat[1]["text"] == "我先看看。" && flat[2]["role"] == "tool" && flat[3]["text"] == "音量已经调整好了。",
        "late ASR fills the original user message and tool continuations are separate ordered messages");
    Check(flat[2]["arguments"]["settings"] == json{{"volume", 40}} && flat[2]["result"]["settings"]["audio"] == json{{"volume", 40}} &&
        messageSave.dump().find("private-") == std::string::npos && messageSave.dump().find("raw-audio") == std::string::npos && messageSave.dump().find("raw-image") == std::string::npos,
        "only allowlisted tool summaries are persisted");
    Voice::History messageRestore(json::parse(messageSave.dump()));
    Check(messageRestore.Snapshot()["list"] == flat, "flat message history restores without changing ordering or summaries");
    messageRestore.Apply({Event::Type::Started, 1, ""});
    Check(messageRestore.Snapshot()["list"].back()["id"] > flat.back()["id"], "new message IDs remain unique after restart");
    auto huge = json::object();
    for (const auto* field : {"action", "section", "query", "question", "target", "url"}) huge[field] = std::string(1500, 'x');
    messageRestore.Apply({Event::Type::ToolStarted, 1, "web_search", "huge", huge});
    messageRestore.Apply({Event::Type::ToolCompleted, 1, "", "huge", huge});
    const auto compact = messageRestore.Snapshot()["list"].back();
    Check(compact["arguments"].dump().size() <= 4096 && compact["result"].dump().size() <= 4096,
        "invalid fields and saved summaries are bounded by total bytes, not just per-field size");
    bool rejectSave = true;
    Voice::History unavailable(json::array(), [&](const json&) { if (rejectSave) throw std::runtime_error("disk failure"); });
    unavailable.Apply({Event::Type::Started, 1, ""});
    Check(!unavailable.Snapshot()["error"].get<std::string>().empty(), "storage failure is visible without breaking conversation");
    rejectSave = false;
    unavailable.Apply({Event::Type::Completed, 1, ""});
    Check(unavailable.Snapshot()["error"] == "", "successful save clears the storage error");
    json clearedSave = json::array({1});
    Voice::History clearing(json::parse(messageSave.dump()), [&](const json& data) { clearedSave = data; });
    clearing.Apply({Event::Type::Started, 7, ""});
    Check(clearing.Clear() && clearing.Snapshot()["list"].empty() && clearedSave.empty(), "clearing removes memory and persisted history");
    clearing.Apply({Event::Type::UserTranscript, 7, "清空前开始的问题"});
    Check(clearing.Snapshot()["list"].empty(), "events from a turn started before clearing are dropped");
    clearing.Apply({Event::Type::Started, 8, ""});
    Check(clearing.Snapshot()["list"].size() == 1 && clearing.Snapshot()["list"][0]["id"].get<uint64_t>() > flat.back()["id"].get<uint64_t>() + 1,
        "IDs keep increasing after clearing");
    rejectSave = true;
    Check(!unavailable.Clear() && unavailable.Snapshot()["list"].empty(), "clearing reports storage failures");

    Fixture early;
    early.Connect(); early.Turn();
    early.session.BeginInput(); early.session.BeginInput(); early.Reply();
    early.session.Receive({{"type", "response.audio.delta"}, {"delta", Voice::EncodeBase64(std::string(4800, '\0'))}});
    Check(early.playing && early.Count("response.cancel") == 0, "press before response creation does not queue a cancellation");

    Fixture failed;
    failed.Connect(); failed.Turn(); failed.Reply();
    failed.session.Receive({{"type", "response.done"}, {"response", {{"status", "failed"}}}});
    Check(failed.state == "idle" && failed.session.Ready(), "failed response keeps the connection ready for another turn");
    failed.Turn("after-failure"); failed.Reply("after-failure-response");
    failed.session.Receive({{"type", "response.done"}, {"response", {{"id", "after-failure-response"}, {"status", "completed"}}}});
    Check(failed.session.Ready() && !failed.session.WaitingForReply(), "another VAD turn can finish after a failed response");
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
    std::cout << "PASS voice protocol: first connection, release, multi-turn context, server VAD, credentials and bounded callbacks\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
