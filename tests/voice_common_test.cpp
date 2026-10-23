#include "VoiceCommon.hpp"
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
}

int main() {
  try {
    Check(Voice::ValidWorkspace("123456") && Voice::ValidWorkspace("ws-abc123"), "valid workspace IDs");
    for (const auto& invalid : {"", "evil.example", "a/../../b", "x?model=other", "bad\nheader", "-id", "id-"})
      Check(!Voice::ValidWorkspace(invalid), "workspace cannot replace the credential destination");
    Check(!Voice::ValidWorkspace(std::string(64, 'a')), "workspace fits one DNS label");
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

    using Event = Voice::ConversationEvent;
    json saved = json::array();
    Voice::History persisted(saved, [&](const json& data) { saved = data; });
    persisted.Apply({Event::Type::Started, 1, ""});
    persisted.Apply({Event::Type::UserTranscript, 1, "重启前的问题"});
    persisted.Apply({Event::Type::AssistantTranscript, 1, "重启前的回复"});
    persisted.Apply({Event::Type::Completed, 1, ""});
    persisted.Apply({Event::Type::Started, 2, ""});
    Voice::History restored(json::parse(saved.dump()));
    auto history = TurnSnapshot(restored);
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

    Voice::EventQueue queue;
    queue.connections[Voice::SynthesisChannel] = 2;
    queue.Network(Voice::SynthesisChannel, 1, Voice::Event::Type::Error, "old connection");
    queue.Network(Voice::SynthesisChannel, 2, Voice::Event::Type::Message, "current connection");
    auto events = queue.Poll();
    queue.Network(Voice::SynthesisChannel, 2, Voice::Event::Type::Binary, "audio");
    queue.Clear(Voice::SynthesisChannel);
    Check(queue.Poll().empty(), "clearing a channel drops its queued events");
    Check(events.size() == 1 && events[0].data == "current connection" && events[0].channel == Voice::SynthesisChannel,
        "closed connections cannot poison a new session");
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
    std::cout << "PASS voice common: credentials, Base64, shortcut, history and bounded callbacks\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
