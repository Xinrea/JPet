#include "VoiceCommon.hpp"
#include "VoiceEventQueue.hpp"
#include "VoiceShortcut.hpp"
#include "VoiceHistory.hpp"
#include "VoiceIndicatorStyle.hpp"
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
    Check(Voice::Trim(" \n sk-test \r") == "sk-test" && !Voice::ValidApiKey("sk-test\r\nHeader: injected"), "credential header validation");
    Check(Voice::Indicator::ShortcutHint("Ctrl", false) == "按 Ctrl 开启/关闭收音" &&
          Voice::Indicator::ShortcutHint("Ctrl", true) == "按住 Ctrl 开始收音" &&
          Voice::Indicator::ShortcutHint("Option", false) == "按 Option 开启/关闭收音" &&
          Voice::Indicator::ShortcutHint("Option", true) == "按住 Option 开始收音", "indicator shortcut hint follows input mode and platform");
    // Recorded shortcuts: KeyboardEvent.code combinations, normalized to modifiers first.
    std::string shortcutError;
    const auto parse = [&](const std::string& text) { shortcutError.clear(); return Voice::ParseShortcut(text, shortcutError); };
#ifdef __APPLE__
    Check(Voice::DefaultShortcut().Code() == "Alt" && Voice::DefaultShortcut().Name() == "Option", "Option stays the default on macOS");
    Check(parse("Space+MetaRight")->Name() == "右 Command + 空格", "macOS key names");
    Check(!parse("Pause") && !parse("F21"), "keys missing from Mac keyboards are rejected");
#else
    Check(Voice::DefaultShortcut().Code() == "Control" && Voice::DefaultShortcut().Name() == "Ctrl", "Ctrl stays the default on Windows");
    Check(parse("Space+MetaRight")->Name() == "右 Win + 空格" && parse("Pause") && parse("F24"), "Windows key names and keys");
#endif
    Check(parse("")->Code() == Voice::DefaultShortcut().Code(), "an empty shortcut is the default");
    Check(parse("Space+ControlLeft")->Code() == "ControlLeft+Space" && parse("AltRight")->Code() == "AltRight" &&
          parse("F13")->Code() == "F13" && parse("ShiftLeft+F8")->Code() == "ShiftLeft+F8" &&
          parse("KeyK+AltLeft+KeyK")->Code() == "AltLeft+KeyK", "shortcuts are normalized and deduplicated");
    Check(parse("ControlRight")->ModifiersOnly() && !parse("ControlRight+KeyK")->ModifiersOnly(), "modifier-only shortcuts");
    for (const auto* typing : {"KeyA", "Space", "Digit1", "ShiftLeft", "ShiftLeft+KeyA", "Shift+Slash", "Enter", "ArrowUp"})
      Check(!parse(typing) && shortcutError.find("打字") != std::string::npos, "keys that type text need a modifier");
    for (const auto* invalid : {"Fn", "CapsLock", "Control+", "+Control", "control", "ControlLeft+AltLeft+MetaLeft+ShiftLeft+KeyK"})
      Check(!parse(invalid) && !shortcutError.empty(), "unknown keys and more than four keys are rejected");
    Check(Voice::SavedShortcut("KeyA").Code() == Voice::DefaultShortcut().Code() &&
          Voice::SavedShortcut("AltRight").Code() == "AltRight", "an unusable saved shortcut falls back to the default");
    for (const auto& key : Voice::Keys)
      Check(Voice::FindKey(key.code) == &key && (key.windows != Voice::NoKey || key.mac != Voice::NoKey), "key table codes are unique");
    Check(Voice::Indicator::BodyText("这是回复", "正在思考…", false) == "这是回复" &&
          Voice::Indicator::BodyText("这是回复", "麦克风不可用", true) == "麦克风不可用" &&
          Voice::Indicator::BodyText("", "正在思考…", false) == "正在思考…", "indicator prioritizes errors and falls back to status");
    Check(Voice::Indicator::HeightForText(16) == Voice::Indicator::MinHeight &&
          Voice::Indicator::HeightForText(48) == 94 &&
          Voice::Indicator::HeightForText(64) == 110 &&
          Voice::Indicator::HeightForText(1000) == Voice::Indicator::MaxHeight,
          "indicator height follows wrapped text within compact and maximum bounds");
    const std::string longReply = std::string(1024, 'a') + "\n下一段回复";
    Check(Voice::Indicator::BodyText(longReply, "正在回复…", false) == longReply,
          "indicator passes full streaming text and line breaks to native wrapping");
    Check(Voice::ValidAudioDeviceId("") && Voice::ValidAudioDeviceId("{0.0.1.00000000}.{6f1e7a3c-0b2d-4e5f-9a8b-1c2d3e4f5a6b}") &&
          Voice::ValidAudioDeviceId("AppleUSBAudioEngine:Logitech:StreamCam:1:2"), "audio device ids");
    Check(!Voice::ValidAudioDeviceId("id\nother") && !Voice::ValidAudioDeviceId(std::string(257, 'a')), "audio device id limits");
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
    using Ms = std::chrono::milliseconds;
    const auto t0 = Voice::ShortcutControl::Clock::time_point{};
    const auto at = [&](int ms) { return t0 + Ms(ms); };
    Voice::ShortcutControl toggleKey, holdKey;
    Check(toggleKey.Update(true, false, false, at(0)) == Action::Begin &&
        toggleKey.Update(true, false, true, at(16)) == Action::None &&
        toggleKey.Update(false, false, true, at(32)) == Action::None &&
        toggleKey.Update(true, false, true, at(48)) == Action::End,
        "toggle starts on press, ignores holding/release, and stops on the next press");
    Check(holdKey.Update(true, true, false, at(0)) == Action::Prepare &&
        holdKey.Update(true, true, false, at(199)) == Action::None &&
        holdKey.Update(true, true, false, at(200)) == Action::Begin &&
        holdKey.Update(true, true, true, at(216)) == Action::None &&
        holdKey.Update(false, true, true, at(500)) == Action::End &&
        holdKey.Update(false, true, false, at(516)) == Action::None,
        "hold prepares on press, begins after the hold delay and stops once on release");
    Check(holdKey.Update(true, true, false, at(1000)) == Action::Prepare && holdKey.Preparing() &&
        holdKey.Update(false, true, false, at(1150)) == Action::Cancel && !holdKey.Preparing() &&
        holdKey.Update(false, true, false, at(1400)) == Action::None,
        "a hold shorter than the delay (an accidental Ctrl+C) is cancelled without starting");
    Check(holdKey.Update(true, true, false, at(2000)) == Action::Prepare &&
        holdKey.Update(true, false, false, at(2050)) == Action::Cancel,
        "switching to toggle mode while a hold is prepared cancels it");
    holdKey.Update(false, true, false, at(2100));
    holdKey.BlockUntilRelease(true);
    Check(holdKey.Update(true, true, false, at(3000)) == Action::None &&
        holdKey.Update(false, true, false, at(3016)) == Action::None &&
        holdKey.Update(true, true, false, at(3032)) == Action::Prepare &&
        holdKey.Update(true, true, false, at(3232)) == Action::Begin,
        "settings reset while held requires release before starting again");
    holdKey.Update(false, true, true, at(3300));
    holdKey.Update(true, true, false, at(3400));
    holdKey.BlockUntilRelease();  // The microphone failed while the hold was prepared.
    Check(!holdKey.Preparing() && holdKey.Update(true, true, false, at(3700)) == Action::None &&
        holdKey.Update(false, true, false, at(3716)) == Action::None &&
        holdKey.Update(true, false, false, at(3732)) == Action::Begin,
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
    json typedSave;
    Voice::History typed(json::array(), [&](const json& data) { typedSave = data; });
    typed.Apply({Event::Type::Started, 4, "", "", {{"input", "text"}}});
    typed.Apply({Event::Type::UserTranscript, 4, "你好"});
    Check(typed.Snapshot()["list"][0]["input"] == "text" && Voice::History(typedSave).Snapshot()["list"][0]["input"] == "text" &&
          !flat[0].contains("input"), "typed messages are marked apart from voice transcripts and restored");
    auto huge = json::object();
    for (const auto* field : {"action", "section", "query", "question", "target", "url"}) huge[field] = std::string(1500, 'x');
    messageRestore.Apply({Event::Type::ToolStarted, 1, "web_search", "huge", huge});
    messageRestore.Apply({Event::Type::ToolCompleted, 1, "", "huge", huge});
    const auto compact = messageRestore.Snapshot()["list"].back();
    Check(compact["arguments"].dump().size() <= 4096 && compact["result"].dump().size() <= 4096,
        "invalid fields and saved summaries are bounded by total bytes, not just per-field size");
    messageRestore.Apply({Event::Type::ToolStarted, 1, "web_fetch", "fetch", {{"url", "https://example.com"}}});
    messageRestore.Apply({Event::Type::ToolCompleted, 1, "", "fetch", {{"ok", true}, {"provider", "deepseek"},
      {"url", "https://example.com"}, {"title", "Example"}, {"format", "markdown"}, {"truncated", true},
      {"content", "private page body"}, {"api_key", "private credential"}}});
    const auto fetched = messageRestore.Snapshot()["list"].back();
    Check(fetched["name"] == "web_fetch" && fetched["result"]["provider"] == "deepseek" && fetched["result"]["truncated"] == true &&
      !fetched["result"].contains("content") && !fetched["result"].contains("api_key"), "fetch history retains only bounded metadata, never page body or credentials");
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
    queue.capture = 2;
    queue.capturing = true;
    queue.Captured(1, Voice::Event::Type::Microphone, "old recording");
    queue.Captured(1, Voice::Event::Type::Error, "old failure");
    queue.Captured(2, Voice::Event::Type::Microphone, "current recording");
    auto events = queue.Poll();
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
