#include "VoiceTools.hpp"
#include "VoiceBilibili.hpp"
#include "VoiceSettingsQueue.hpp"
#include <chrono>
#include <atomic>
#include <future>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using nlohmann::json;
void Check(bool condition, const char* label) { if (!condition) throw std::runtime_error(label); }
Voice::ToolCall Call(const char* name, json args) { return {"c1", name, args.dump()}; }
}

int main() {
  try {
    const auto definitions = Voice::ToolDefinitions();
    Check(definitions.size() == 14, "focused queries and action tools registered within relay limit");
    for (const auto& definition : definitions) Check(definition["type"] == "function" &&
      definition["function"]["parameters"]["additionalProperties"] == false, "nested Qwen schema with closed arguments");
    int mutations = 0, queries = 0, external = 0, opens = 0;
    std::string openedUrl;
    json last;
    Voice::ToolDependencies dependencies{
      [&](const json& query) { ++queries; last = query; return json{{"ok", true}, {"profile", {{"starcnt", 3}}}}; },
      [&](const json& command) { ++mutations; last = command; return std::string{}; },
      [&](const json& query) { ++external; last = query; return json{{"ok", true}}; },
      [&](const json& query) { ++external; last = query; return json{{"ok", true}}; },
      [&](const json& query) { ++external; last = query; return json{{"ok", true}}; },
      [&](const std::string& url, std::string&) { ++opens; openedUrl = url; return true; }
    };
    const auto run = [&](const char* name, json args) { return Voice::ExecuteTool(Call(name, args), dependencies); };
    int settingsCalls = 0;
    dependencies.settings = [&](const json& request, const std::function<bool()>&) { ++settingsCalls; last = request; return json{{"ok", true}, {"settings", {{"audio", {{"volume", 30}}}}}}; };
    Check(run("jpet_settings", json::object())["ok"] == true && last == json{{"action", "get"}, {"section", "all"}}, "settings defaults to reading whitelisted values");
    for (const auto* section : {"audio", "display", "interaction", "notifications", "shortcuts", "clothes", "appearance"})
      Check(run("jpet_settings", {{"action", "get"}, {"section", section}})["ok"] == true && last["section"] == section, "each settings section can be read");
    for (const auto& request : {
      json{{"action", "update"}, {"section", "audio"}, {"settings", {{"volume", 30}}}},
      json{{"action", "update"}, {"section", "audio"}, {"settings", {{"volume", 0}, {"mute", true}, {"idle_audio", false}, {"touch_audio", false}}}},
      json{{"action", "update"}, {"section", "display"}, {"settings", {{"scale", 1.25}, {"green", true}, {"limit", true}}}},
      json{{"action", "update"}, {"section", "display"}, {"settings", {{"scale", 0}}}},
      json{{"action", "update"}, {"section", "interaction"}, {"settings", {{"track", false}, {"dropfile", false}}}},
      json{{"action", "update"}, {"section", "notifications"}, {"settings", {{"dynamic", false}, {"live", true}, {"update", false}}}},
      json{{"action", "update"}, {"section", "appearance"}, {"settings", {{"long_hair", true}, {"glasses", true}, {"mouth", 6}}}},
      json{{"action", "add_watch"}, {"uid", "123456"}}, json{{"action", "remove_watch"}, {"uid", "123456"}},
      json{{"action", "set_shortcut"}, {"direction", "up"}, {"shortcut_type", "disabled"}},
      json{{"action", "set_shortcut"}, {"direction", "right"}, {"shortcut_type", "settings"}},
      json{{"action", "set_shortcut"}, {"direction", "down"}, {"shortcut_type", "application"}, {"target", "/Applications/JPet.app"}},
      json{{"action", "set_shortcut"}, {"direction", "left"}, {"shortcut_type", "folder"}, {"target", "C:\\Users\\Test"}}
    }) Check(run("jpet_settings", request)["ok"] == true, "daily settings and local notification/shortcut actions accepted");
    Check(run("jpet_settings", {{"action", "set_shortcut"}, {"direction", "up"}, {"shortcut_type", "website"}, {"target", " HTTPS://example.com/a%2Fb?sig=x%2Fy "}})["ok"] == true &&
      last["target"] == "https://example.com/a%2Fb?sig=x%2Fy", "website shortcuts use the URL validator without rewriting query strings");
    Check(run("jpet_settings", {{"action", "update"}, {"settings", {{"volume", 10}}}})["ok"] == true && last["section"] == "audio" &&
      run("jpet_settings", {{"action", "update"}, {"settings", {{"tail", true}}}})["ok"] == true && last["section"] == "appearance",
      "update infers an omitted section from its unique field names");
    const auto mixed = run("jpet_settings", {{"action", "update"}, {"settings", {{"volume", 10}, {"tail", true}}}});
    Check(mixed["code"] == "invalid_arguments" && mixed["error"].get<std::string>().find("section") != std::string::npos,
      "fields from several sections ask for separate calls");
    const int beforeBadSettings = settingsCalls;
    for (const auto& request : {
      json{{"action", "get"}, {"section", "voice"}}, json{{"action", "get"}, {"section", "account"}},
      json{{"action", "get"}, {"api_key", "secret"}}, json{{"action", "logout"}},
      json{{"action", "update"}, {"section", "all"}, {"settings", {{"mute", true}}}},
      json{{"action", "update"}, {"section", "audio"}}, json{{"action", "update"}, {"section", "audio"}, {"settings", json::object()}},
      json{{"action", "update"}, {"section", "audio"}, {"settings", {{"volume", -1}}}},
      json{{"action", "update"}, {"section", "audio"}, {"settings", {{"volume", 101}}}},
      json{{"action", "update"}, {"section", "audio"}, {"settings", {{"volume", 20.5}}}},
      json{{"action", "update"}, {"section", "audio"}, {"settings", {{"mute", "false"}}}},
      json{{"action", "update"}, {"section", "audio"}, {"settings", {{"mute", 1}}}},
      json{{"action", "update"}, {"section", "audio"}, {"settings", {{"volume", 20}, {"provider", "jpet"}}}},
      json{{"action", "update"}, {"section", "audio"}, {"settings", {{"volume", 20}, {"scale", 2}}}},
      json{{"action", "update"}, {"section", "display"}, {"settings", {{"scale", 3.1}}}},
      json{{"action", "update"}, {"section", "display"}, {"settings", {{"scale", -0.1}}}},
      json{{"action", "update"}, {"section", "display"}, {"settings", {{"scale", nullptr}}}},
      json{{"action", "update"}, {"section", "notifications"}, {"settings", {{"cookies", "secret"}}}},
      json{{"action", "update"}, {"section", "appearance"}, {"settings", {{"glasses", "true"}}}},
      json{{"action", "update"}, {"section", "appearance"}, {"settings", {{"mouth", 0}}}},
      json{{"action", "update"}, {"section", "appearance"}, {"settings", {{"mouth", 7}}}},
      json{{"action", "update"}, {"section", "appearance"}, {"settings", {{"mouth", 1.5}}}},
      json{{"action", "update"}, {"section", "appearance"}, {"settings", {{"ParamCloth2", true}}}},
      json{{"action", "add_watch"}, {"uid", "0"}}, json{{"action", "add_watch"}, {"uid", "123x"}},
      json{{"action", "remove_watch"}, {"uid", 123}}, json{{"action", "add_watch"}, {"uid", "123\n"}, {"section", "all"}},
      json{{"action", "set_shortcut"}, {"direction", "up"}, {"shortcut_type", "website"}, {"target", "javascript:alert(1)"}},
      json{{"action", "set_shortcut"}, {"direction", "up"}, {"shortcut_type", "website"}, {"target", "https://user:password@example.com"}},
      json{{"action", "set_shortcut"}, {"direction", "up"}, {"shortcut_type", "application"}, {"target", "ls"}},
      json{{"action", "set_shortcut"}, {"direction", "up"}, {"shortcut_type", "folder"}},
      json{{"action", "set_shortcut"}, {"direction", "diagonal"}, {"shortcut_type", "settings"}},
      json{{"action", "set_shortcut"}, {"direction", "up"}, {"shortcut_type", "disabled"}, {"target", "ignored"}},
      json{{"action", "change_clothes"}, {"clothes_id", 3}}, json{{"action", "change_clothes"}, {"clothes_id", 1}, {"unlock", true}}
    }) Check(run("jpet_settings", request)["ok"] == false, "invalid and excluded settings rejected before dispatch");
    Check(settingsCalls == beforeBadSettings && mutations == 0, "invalid settings never reach the runtime or game command");
    Check(run("jpet_settings", {{"action", "change_clothes"}, {"clothes_id", 1}})["ok"] == true && mutations == 1, "settings clothing changes use the existing game command");
    dependencies.command = [&](const json& command) { ++mutations; Check(command == json{{"type", "clothes"}, {"id", 2}}, "clothing settings only submit an existing outfit ID"); return std::string("服装尚未解锁"); };
    const auto beforeLockedClothes = queries;
    Check(run("jpet_settings", {{"action", "change_clothes"}, {"clothes_id", 2}})["error"] == "服装尚未解锁" && queries == beforeLockedClothes, "locked clothing cannot claim success or bypass server conditions");
    dependencies.command = [&](const json& command) { ++mutations; last = command; return std::string{}; };
    dependencies.settings = [](const json&, const std::function<bool()>&) { return json{{"ok", false}, {"error", "轮盘入口的本机路径不存在或无法访问"}}; };
    Check(run("jpet_settings", {{"action", "set_shortcut"}, {"direction", "up"}, {"shortcut_type", "folder"}, {"target", "/missing"}})["ok"] == false, "runtime settings failures remain failures");
    dependencies.settings = {};
    Check(run("jpet_settings", json::object())["ok"] == false, "missing settings adapter does not claim success");

    {
      Voice::SettingsQueue settingsQueue;
      const auto mainThread = std::this_thread::get_id();
      std::thread::id applyingThread;
      auto applied = std::async(std::launch::async, [&] { return settingsQueue.Invoke([&] { applyingThread = std::this_thread::get_id(); return json{{"ok", true}}; }); });
      auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      while (applied.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready && std::chrono::steady_clock::now() < deadline) settingsQueue.Drain();
      settingsQueue.Cancel(true);
      Check(applied.get()["ok"] == true && applyingThread == mainThread, "settings are applied by the pumping main thread, not the tool worker");
      settingsQueue.Start();
      int appliedAfterCancel = 0;
      auto cancelled = std::async(std::launch::async, [&] { return settingsQueue.Invoke([&] { ++appliedAfterCancel; return json{{"ok", true}}; }); });
      deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      while (cancelled.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready && std::chrono::steady_clock::now() < deadline) settingsQueue.Cancel();
      settingsQueue.Cancel(true);
      Check(cancelled.get()["ok"] == false, "interruption releases a pending settings worker with failure");
      settingsQueue.Drain();
      Check(appliedAfterCancel == 0 && settingsQueue.Invoke([&] { ++appliedAfterCancel; return json{{"ok", true}}; })["ok"] == false, "cancelled settings cannot run later and shutdown rejects new requests");
      settingsQueue.Start();
      std::promise<void> dispatchStarted, releaseDispatch, dispatchFinished;
      auto started = dispatchStarted.get_future();
      auto release = releaseDispatch.get_future();
      auto finished = dispatchFinished.get_future();
      auto settingsDependencies = dependencies;
      settingsDependencies.settings = [&](const json&, const std::function<bool()>& cancelled) {
        dispatchStarted.set_value();
        release.wait(); // Simulate cancellation before the runtime queues its work.
        auto result = settingsQueue.Invoke([&] { ++appliedAfterCancel; return json{{"ok", true}}; }, cancelled);
        dispatchFinished.set_value();
        return result;
      };
      Voice::ToolExecutor settingsExecutor(settingsDependencies);
      settingsExecutor.Submit({Call("jpet_settings", {{"action", "update"}, {"section", "audio"}, {"settings", {{"mute", true}}}})});
      const bool entered = started.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
      settingsExecutor.Cancel();
      settingsQueue.Cancel();
      releaseDispatch.set_value();
      deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      while (finished.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready && std::chrono::steady_clock::now() < deadline) settingsQueue.Drain();
      settingsQueue.Cancel(true);
      Check(entered && finished.wait_for(std::chrono::seconds(1)) == std::future_status::ready && appliedAfterCancel == 0,
        "settings queued after interruption still check the original tool generation before mutating");
    }
    for (const auto& url : {"https://www.bilibili.com/", "http://127.0.0.1:8080/path?q=test#part", "https://[::1]:8443/", "https://example.com/中文?q=%E4%B8%AD#片段"}) {
      const auto result = run("open_url", {{"url", url}});
      Check(result["ok"] == true && result["url"] == url && openedUrl == url && result["browser"] == "system_default", "valid web URL reaches default browser unchanged");
    }
    Check(run("open_url", {{"url", "  HTTPS://Example.COM/a%2Fb?sig=x%2Fy&n=1#ABC  "}})["ok"] == true &&
      openedUrl == "https://Example.COM/a%2Fb?sig=x%2Fy&n=1#ABC", "scheme and surrounding whitespace normalize without changing signed URL parts");
    const auto beforeBadUrls = opens;
    for (const auto& url : {"", "www.bilibili.com", "//example.com", "file:///etc/passwd", "javascript:alert(1)",
      "data:text/html,test", "mailto:test@example.com", "bilibili://video/1", "https://", "https:///example.com", "https://?q=x", "https://#part",
      "https://user:password@example.com/", "https://@example.com/", "https://example.com:99999/", "https://example.com:abc/",
      "https://[::::]/", "https://example.com/path with space", "https://example.com/path\\file", "https://example.com/%ZZ",
      "https://example.com/%", "https://example.com/\nInjected", "https://example.com/\x7f"})
      Check(run("open_url", {{"url", url}})["ok"] == false, "non-web and malformed URLs rejected before platform launch");
    for (const auto& args : {json::object(), json{{"url", 123}}, json{{"url", "https://example.com"}, {"browser", "Chrome"}},
      json{{"url", std::string("https://example.com/\0file", 24)}}, json{{"url", "https://example.com/" + std::string(4096, 'x')}}})
      Check(run("open_url", args)["ok"] == false, "missing, extra, invalid and oversized browser arguments rejected");
    Check(opens == beforeBadUrls, "invalid URLs never invoke browser");
    dependencies.openUrl = [](const std::string&, std::string& error) { error = "默认浏览器无法打开"; return false; };
    Check(run("open_url", {{"url", "https://example.com"}})["error"] == "默认浏览器无法打开", "OS launch rejection is reported as failure");
    dependencies.openUrl = {};
    Check(run("open_url", {{"url", "https://example.com"}})["ok"] == false, "unavailable browser adapter does not claim success");
    const std::pair<const char*, const char*> queriesByTool[] = {
      {"get_game_profile", "profile"}, {"get_game_clothes", "clothes"}, {"get_task_catalog", "task_catalog"},
      {"get_current_task", "current_task"}, {"get_task_queue", "task_queue"}, {"get_task_history", "task_history"},
      {"get_game_achievements", "achievements"}, {"get_game_statistics", "statistics"}
    };
    for (const auto& [tool, section] : queriesByTool)
      Check(run(tool, json::object())["ok"] == true && last["section"] == section, "query dispatch selects only its own content");
    for (const auto* tool : {"get_task_catalog", "get_task_history", "get_game_achievements"}) {
      Check(run(tool, {{"offset", 5}, {"limit", 10}})["ok"] == true && last["offset"] == 5 && last["limit"] == 10, "pagination passed to adapter");
      Check(run(tool, {{"limit", 11}})["ok"] == false && run(tool, {{"offset", -1}})["ok"] == false, "pagination bounds checked before read");
    }
    Check(run("get_game_rank", json::object())["ok"] == false, "the removed leaderboard query is unknown");
    Check(run("get_game_profile", {{"section", "all"}})["ok"] == false, "focused tools reject extra content");
    Check(run("get_game_state", json::object())["ok"] == false && run("get_game_state", {{"section", "all"}})["ok"] == false, "legacy full-state query cannot exceed budget");
    Check(run("get_game_state", {{"section", "tasks"}})["ok"] == true && last["section"] == "task_catalog", "explicit legacy section migrates to paginated catalog");
    for (const auto& pair : {std::pair{"start_task", "task.start"}, {"queue_task", "task.queue"}, {"cancel_task", "task.cancel"}}) {
      dependencies.command = [&](const json& command) { ++mutations; Check(command["type"] == pair.second && command["id"] == 13, "task command has directory ID"); return std::string{}; };
      Check(run("game_action", {{"action", pair.first}, {"task_id", 13}})["ok"] == true, "task action succeeds");
      Check(last["section"] == "task_status", "task actions only read execution and queue state");
    }
    dependencies.command = [&](const json& command) { ++mutations; last = command; return std::string{}; };
    for (const auto& pair : {std::pair{"upgrade_attribute", "attr.buy"}, {"refund_attribute", "attr.refund"}}) {
      dependencies.command = [&](const json& command) { ++mutations; Check(command["type"] == pair.second && command["attr"] == "intellect", "attribute command uses server validation"); return std::string{}; };
      Check(run("game_action", {{"action", pair.first}, {"attribute", "intellect"}})["ok"] == true, "attribute action succeeds");
      Check(last["section"] == "attributes", "attribute operations omit task and achievement catalogs");
    }
    dependencies.command = [&](const json& command) { ++mutations; last = command; return std::string{}; };
    for (const auto& args : {json{{"action", "remove_queued_task"}, {"entry_id", 123456789012LL}},
      json{{"action", "move_queued_task"}, {"entry_id", 7}, {"direction", "down"}}, json{{"action", "upgrade_task_queue"}},
      json{{"action", "star_up"}}, json{{"action", "change_clothes"}, {"clothes_id", 2}}})
      Check(run("game_action", args)["ok"] == true, "remaining game operations supported");
    const int beforeInvalid = mutations;
    for (const auto& args : {json{{"action", "start_task"}}, json{{"action", "start_task"}, {"task_id", 1.5}},
      json{{"action", "queue_task"}, {"task_id", 14}}, json{{"action", "cancel_task"}, {"task_id", "1"}},
      json{{"action", "remove_queued_task"}, {"entry_id", -1}}, json{{"action", "move_queued_task"}, {"entry_id", 1}, {"direction", "left"}},
      json{{"action", "upgrade_attribute"}, {"attribute", "exp"}}, json{{"action", "upgrade_attribute"}, {"attribute", "speed"}, {"count", 100}},
      json{{"action", "star_up"}, {"task_id", 1}}, json{{"action", "change_clothes"}, {"clothes_id", 3}}, json{{"action", "delete_account"}}, json::array()})
      Check(run("game_action", args)["ok"] == false, "invalid mutation rejected before dispatch");
    Check(mutations == beforeInvalid, "invalid input never mutates game");
    dependencies.command = [](const json&) { return std::string("经验不足"); };
    const int beforeFailed = queries;
    Check(run("game_action", {{"action", "upgrade_attribute"}, {"attribute", "speed"}})["error"] == "经验不足" && queries == beforeFailed,
      "server rejection remains failure without success state");
    Check(run("view_desktop", json::object())["ok"] == true && last["display"] == 0, "desktop defaults to main monitor");
    Check(run("view_desktop", {{"display", 8}})["ok"] == false, "bounded monitor selection");
    Check(run("web_search", {{"query", "今天的天气"}})["ok"] == true && last["limit"] == 5, "web search dispatch");
    for (const auto* type : {"video", "user", "live", "article", "bangumi", "film"})
      Check(run("bilibili", {{"action", "search"}, {"query", "千问"}, {"type", type}})["ok"] == true && last["type"] == type, "six Bilibili search categories");
    Check(run("bilibili", {{"action", "search"}, {"query", "千问"}})["ok"] == true &&
      last == json{{"action", "search"}, {"query", "千问"}, {"type", "video"}, {"order", "relevance"}, {"page", 1}, {"limit", 5}},
      "search defaults are filled before dispatch");
    Check(run("bilibili", {{"action", "video_info"}, {"video", "https://www.bilibili.com/video/BV1GJ411x7h7/?p=2"}})["ok"] == true &&
      last == json{{"action", "video_info"}, {"bvid", "BV1GJ411x7h7"}}, "video links normalize to BV ids");
    for (const auto* video : {"av170001", "AV170001", "170001", "https://www.bilibili.com/video/av170001"})
      Check(run("bilibili", {{"action", "video_info"}, {"video", video}})["ok"] == true && last["aid"] == 170001, "av ids normalize to aid");
    Check(run("bilibili", {{"action", "live_room"}, {"room_id", "1024"}})["ok"] == true && last["room_id"] == 1024 && !last.contains("mid"),
      "digit-string IDs accepted");
    Check(run("bilibili", {{"action", "video_comments"}, {"video", "BV1GJ411x7h7"}})["ok"] == true &&
      last["order"] == "hot" && last["page"] == 1 && last["limit"] == 5, "comment defaults");
    Check(run("bilibili", {{"action", "following_feed"}})["ok"] == true && last["type"] == "all", "feed type defaults to all");
    Check(run("bilibili", {{"action", "bangumi_timeline"}})["ok"] == true && last["category"] == "bangumi" && last["days"] == 1, "timeline defaults");
    Check(run("bilibili", {{"action", "unread"}})["ok"] == true && last == json{{"action", "unread"}}, "parameterless account action");
    const auto beforeBadSearch = external;
    for (const auto& args : {json::object(), json{{"action", "shell"}}, json{{"action", "search"}},
      json{{"action", "search"}, {"query", ""}}, json{{"action", "search"}, {"query", "test\nCookie: secret"}},
      json{{"action", "search"}, {"query", "x"}, {"url", "https://evil.test"}}, json{{"action", "search"}, {"query", "x"}, {"page", 51}},
      json{{"action", "search"}, {"query", "x"}, {"type", "user"}, {"order", "latest"}},
      json{{"action", "search"}, {"query", "x"}, {"type", "all"}}, json{{"action", "following_feed"}, {"type", "user"}},
      json{{"action", "video_info"}}, json{{"action", "video_info"}, {"video", "hello"}}, json{{"action", "video_info"}, {"video", "av0"}},
      json{{"action", "video_info"}, {"video", "BV1GJ411x7h7"}, {"mid", 2}}, json{{"action", "user_info"}},
      json{{"action", "user_info"}, {"mid", "0"}}, json{{"action", "user_info"}, {"mid", -1}}, json{{"action", "user_info"}, {"mid", "2x"}},
      json{{"action", "live_room"}}, json{{"action", "live_room"}, {"mid", 1}, {"room_id", 2}},
      json{{"action", "ranking"}, {"category", "games"}}, json{{"action", "bangumi_timeline"}, {"category", "all"}},
      json{{"action", "bangumi_timeline"}, {"days", 8}}, json{{"action", "video_comments"}, {"video", "BV1GJ411x7h7"}, {"order", "relevance"}},
      json{{"action", "my_info"}, {"mid", 2}}, json{{"action", "favorites"}, {"folder_id", "abc"}}, json{{"action", "video_subtitle"}, {"video", "BV1GJ411x7h7"}, {"part", 0}}})
      Check(run("bilibili", args)["ok"] == false, "bad Bilibili arguments rejected");
    Check(run("bilibili", {{"action", "video_info"}, {"video", "BV1GJ411x7h7"}, {"mid", 2}})["code"] == "invalid_arguments",
      "per-action fields are reported as parameter errors");
    Check(run("bilibili_search", {{"query", "x"}})["ok"] == false, "the replaced search tool is unknown");
    Check(external == beforeBadSearch && Voice::ExecuteTool({"c", "game_action", "{"}, dependencies)["ok"] == false,
      "malformed JSON and search input have no effects");
    Check(run("shell", {{"command", "ls"}})["ok"] == false, "unknown tools cannot execute arbitrary code");

    auto snapshot = json{{"cookie", "secret-cookie"}, {"session", "secret-session"},
      {"save", {{"failcount", 2}, {"achievements", {{"metrics", {{"tasks", 3}}}, {"streak", 1}}}, {"private", "secret-private"}}}};
    json profile = {{"attributes", {{"exp", 50}}}, {"buffs", {"guard"}}, {"buycost", 11},
      {"clothes", {{"current", 0}, {"unlock", {true, true, false}}}}, {"private", {{"session", "secret-profile"}}}};
    json taskData = {{"current", {{"id", 13}, {"title", "current"}, {"remaining_seconds", 120}}},
      {"list", json::array()}, {"queue", {{{"entry_id", 42}, {"id", 3}, {"title", "queued"}, {"desc", std::string(20000, 'x')}}}},
      {"history", json::array()}, {"queue_capacity", 2}, {"queue_upgrade", {{"cost", 1}}}};
    for (int i = 1; i <= 12; ++i) taskData["list"].push_back({{"id", i}, {"title", "任务"}, {"rate", 75}, {"cost", 300},
      {"requirements", {{"intellect", 3}}}, {"rewards", {{"exp", 100}}}, {"desc", std::string(20000, 'x')}});
    for (int i = 0; i < 10; ++i) taskData["history"].push_back({{"id", i}, {"success", true}, {"rewards", {{"exp", 100}}}, {"desc", std::string(20000, 'x')}});
    json achievementData = {{"total", 50}, {"unlocked", 1}, {"list", json::array()}};
    for (int i = 0; i < 50; ++i) achievementData["list"].push_back({{"id", std::to_string(i)}, {"title", "成就"},
      {"description", "累计陪伴并完成指定任务"}, {"progress", i}, {"target", 50}, {"unlocked", false}, {"private", "secret-achievement"}});
    auto view = [&](const std::string& section, size_t offset = 0, size_t limit = 5) {
      auto game = snapshot;
      game["profile"] = profile;
      game["tasks"] = taskData;
      game["achievements"] = achievementData;
      return Voice::GameView(game, section, offset, limit);
    };
    Check(view("profile")["profile"]["buycost"] == 11 && !view("profile")["profile"].contains("clothes"), "profile excludes unrelated outfit state");
    Check(view("task_queue")["queue"][0]["entry_id"] == 42 && !view("task_queue").contains("tasks"), "queue retains actionable IDs without catalogs");
    Check(view("statistics")["statistics"]["failcount"] == 2, "statistics retain metrics without catalogs");
    Check(view("clothes")["clothes"]["unlock"][1] == true && !view("clothes").contains("profile"), "outfit query retains unlock conditions");
    for (const auto& [tool, section] : queriesByTool) {
      const auto result = view(section);
      Check(result["ok"] == true && result.dump().size() < 16000, "every query fits the relay limit with large unrelated data");
      Check(result.dump().find("secret-") == std::string::npos, "focused game queries exclude credentials and raw save/config");
    }
    json seen = json::array();
    size_t offset = 0;
    do {
      const auto page = view("task_catalog", offset, 5)["tasks"];
      Check(page["total"] == 13 && page["list"].size() <= 5, "catalog includes active task and respects page size");
      for (const auto& item : page["list"]) { seen.push_back(item["id"]); Check(!item.contains("desc") && item.contains("id"), "catalog omits flavor text but keeps IDs"); }
      if (!page["has_more"].get<bool>()) { Check(page["next_offset"].is_null(), "last page signals completion"); break; }
      offset = page["next_offset"];
    } while (true);
    Check(seen.size() == 13 && seen.back() == 13, "all catalog pages preserve the active task once");
    Check(view("task_catalog", 10000)["tasks"]["list"].empty() && view("task_catalog", 10000)["tasks"]["has_more"] == false, "past-end pagination terminates");
    Check(view("achievements", 10, 10)["achievements"]["list"][0]["id"] == "10" &&
      view("achievements", 10, 10)["achievements"]["unlocked"] == 1, "achievement page preserves progress and counts");
    Check(view("task_history", 5)["history"]["list"].size() == 5 && !view("task_history").contains("current"), "history pages exclude active tasks");
    achievementData["list"][0]["description"] = std::string(20000, 'x');
    const auto oversizedPage = view("achievements", 0, 1)["achievements"];
    Check(oversizedPage["list"][0]["id"] == "0" && oversizedPage["list"][0].contains("notice") &&
      oversizedPage["next_offset"] == 1 && oversizedPage.dump().size() < 16000,
      "oversized single record retains identity and pagination makes progress");
    Check(view("all")["ok"] == false, "raw view cannot reconstruct an oversized all query");
    auto focusedDependencies = dependencies;
    int successfulCommands = 0;
    focusedDependencies.command = [&](const json&) { ++successfulCommands; return std::string{}; };
    focusedDependencies.game = [&](const json& query) { return view(query.at("section")); };
    for (const auto& args : {json{{"action", "queue_task"}, {"task_id", 3}}, json{{"action", "start_task"}, {"task_id", 3}},
      json{{"action", "move_queued_task"}, {"entry_id", 42}, {"direction", "up"}}, json{{"action", "upgrade_task_queue"}},
      json{{"action", "upgrade_attribute"}, {"attribute", "speed"}}, json{{"action", "star_up"}}, json{{"action", "change_clothes"}, {"clothes_id", 1}}}) {
      const auto result = Voice::ExecuteTool(Call("game_action", args), focusedDependencies);
      Check(result["ok"] == true && result.dump().size() < 2000 && !result["state"].contains("achievements") && !result["state"].contains("tasks"),
        "successful mutations carry only relevant state even with oversized catalogs");
    }
    for (int failure = 0; failure < 3; ++failure) {
      focusedDependencies.game = [failure](const json&) -> json {
        if (failure == 0) throw std::runtime_error("secret-read-error");
        if (failure == 1) return {{"ok", false}, {"error", "secret-read-error"}};
        return {{"ok", true}, {"oversized", std::string(16000, 'x')}};
      };
      const auto before = successfulCommands;
      const auto result = Voice::ExecuteTool(Call("game_action", {{"action", "queue_task"}, {"task_id", 3}}), focusedDependencies);
      Check(result["ok"] == true && result.contains("notice") && !result.contains("state") && successfulCommands == before + 1,
        "state-read failure or oversize never reverses successful command acknowledgement");
      Check(result.dump().find("secret-") == std::string::npos, "post-command read exceptions stay private");
      Check(Voice::ExecuteTool(Call("jpet_settings", {{"action", "change_clothes"}, {"clothes_id", 1}}), focusedDependencies)["ok"] == true,
        "settings clothing changes preserve command success if state read fails");
    }
    auto bili = Voice::BilibiliSearchResults({{"code", 0}, {"data", {{"numResults", 10}, {"page", 2}, {"result", {
      {{"title", "<em class=\"keyword\">千问</em>&amp;教程"}, {"author", "作者"}, {"bvid", "BV1xx411c7mD"}, {"play", 30}},
      {{"title", "第二项"}, {"arcurl", "javascript:alert(1)"}}
    }}}}}, "video", 1);
    Check(bili["results"].size() == 1 && bili["results"][0]["title"] == "千问&教程" &&
      bili["results"][0]["url"] == "https://www.bilibili.com/video/BV1xx411c7mD", "Bilibili highlights normalized and links resolved");
    Check(Voice::BilibiliSearchResults({{"code", 0}, {"data", {{"result", nullptr}}}}, "video", 5)["results"].empty(), "empty Bilibili search is success");
    for (int code : {-101, -352, -412, -400}) Check(Voice::BilibiliSearchResults({{"code", code}}, "video", 5)["ok"] == false, "Bilibili login/risk errors are actionable");

    {
      // Offline Bilibili flows: a fake HTTP layer answers by longest URL prefix.
      const std::string api = "https://api.bilibili.com", live = "https://api.live.bilibili.com";
      std::map<std::string, Voice::BilibiliReply> routes;
      std::vector<std::string> urls;
      const Voice::BilibiliGet get = [&](const std::string& url, const std::string&) -> Voice::BilibiliReply {
        urls.push_back(url);
        const std::pair<const std::string, Voice::BilibiliReply>* match = nullptr;
        for (const auto& route : routes)
          if (url.compare(0, route.first.size(), route.first) == 0 && (!match || route.first.size() > match->first.size())) match = &route;
        return match ? match->second : Voice::BilibiliReply{{}, 404, ""};
      };
      const auto ok = [](json body) { return Voice::BilibiliReply{std::move(body), 200, ""}; };
      const auto bili = [&](const json& args, const std::string& uid = "42") {
        return Voice::RunBilibili(Voice::BilibiliRequest(args), uid, get, 1700000000);
      };
      const auto requested = [&](const std::string& part) {
        return std::any_of(urls.begin(), urls.end(), [&](const std::string& url) { return url.find(part) != std::string::npos; });
      };
      const json wbi = {{"img_url", "https://i0.hdslb.com/bfs/wbi/7cd084941338484aae1ad9425b84077c.png"},
        {"sub_url", "https://i0.hdslb.com/bfs/wbi/4932caff0ff746eab6f01bf08b70ac45.png"}};
      routes[api + "/x/web-interface/nav"] = ok({{"code", 0}, {"data", {{"isLogin", true}, {"mid", 42}, {"uname", "轴伊"},
        {"money", 12.5}, {"level_info", {{"current_level", 6}}}, {"vip_label", {{"text", ""}}}, {"wbi_img", wbi}}}});
      routes[api + "/x/web-interface/nav/stat"] = ok({{"code", 0}, {"data", {{"following", 3}, {"follower", 4}, {"dynamic_count", 5}}}});

      Check(bili({{"action", "history"}}, "")["error"] == "请先在JPet中登录B站账号" && urls.empty(), "account actions require a JPet login before any request");
      Check(bili({{"action", "history"}}, "7")["error"].get<std::string>().find("不一致") != std::string::npos, "cookies must belong to the JPet account");
      const auto account = bili({{"action", "my_info"}});
      Check(account["account"]["name"] == "轴伊" && account["account"]["coins"] == 12.5 && account["account"]["followers"] == 4 &&
        !account["account"].contains("vip"), "my_info merges nav and stat, dropping empty fields");

      json videos = json::array();
      for (int i = 0; i < 3; ++i) videos.push_back({{"title", "<em>视频</em>" + std::to_string(i)}, {"bvid", "BV1GJ411x7h7"}, {"aid", i},
        {"owner", {{"name", "UP"}, {"mid", 9}}}, {"stat", {{"view", 100}, {"like", 7}}}, {"desc", std::string(5000, 'x')}});
      routes[api + "/x/web-interface/popular"] = ok({{"code", 0}, {"data", {{"list", videos}, {"no_more", false}}}});
      urls.clear();
      const auto popular = bili({{"action", "popular"}, {"limit", 2}}, "");
      Check(popular["ok"] == true && popular["results"].size() == 2 && popular["results"][0]["title"] == "视频0" &&
        popular["results"][0]["author"] == "UP" && popular["results"][0]["views"] == 100 && !popular["results"][0].contains("desc") &&
        popular["results"][0]["url"] == "https://www.bilibili.com/video/BV1GJ411x7h7" && popular["has_more"] == true &&
        popular["external_content"] == true, "public lists work logged out and are compact");
      Check(urls.size() == 1 && urls[0].find("ps=2") != std::string::npos && urls[0].find("pn=1") != std::string::npos, "public calls skip nav");

      routes[api + "/x/web-interface/wbi/search/square"] = ok({{"code", 0}, {"data", {{"trending", {{"list", {{{"keyword", "k"}, {"show_name", "热词"}, {"heat_score", 9}}}}}}}}});
      urls.clear();
      Check(bili({{"action", "hot_search"}}, "")["results"][0] == json{{"keyword", "k"}, {"title", "热词"}, {"heat", 9}} &&
        requested("wts=1700000000") && requested("w_rid="), "WBI-signed endpoints fetch keys from nav");

      routes[api + "/x/web-interface/view"] = ok({{"code", 0}, {"data", {{"bvid", "BV1GJ411x7h7"}, {"aid", 80433022}, {"title", "MV"},
        {"cid", 11}, {"owner", {{"name", "索尼"}, {"mid", 99}}}, {"videos", 2}, {"desc", "简介"}, {"stat", {{"view", 1}, {"coin", 2}}},
        {"pages", {{{"cid", 11}, {"part", "上"}}, {{"cid", 12}, {"part", "下"}}}}}}});
      const auto info = bili({{"action", "video_info"}, {"video", "BV1GJ411x7h7"}}, "");
      Check(info["video"]["description"] == "简介" && info["video"]["stat"]["coin"] == 2 && info["video"]["part_titles"] == json{"上", "下"},
        "video_info returns description, stats and parts");

      routes[api + "/x/web-interface/view/conclusion/get"] = ok({{"code", 0}, {"data", {{"code", 0}, {"model_result", {{"summary", "总结"},
        {"outline", {{{"title", "开场"}, {"timestamp", 0}, {"part_outline", {{{"timestamp", 65}, {"content", "要点"}}}}}}}}}}}});
      urls.clear();
      const auto summary = bili({{"action", "video_summary"}, {"video", "BV1GJ411x7h7"}, {"part", 2}});
      Check(summary["summary"] == "总结" && summary["outline"][0]["points"][0] == "[1:05] 要点" && summary["video"]["title"] == "MV" &&
        requested("cid=12") && requested("up_mid=99"), "summary resolves the requested part and uploader");
      Check(bili({{"action", "video_summary"}, {"video", "BV1GJ411x7h7"}, {"part", 3}})["error"].get<std::string>().find("2 个分P") != std::string::npos,
        "missing parts are reported");
      routes[api + "/x/web-interface/view/conclusion/get"] = ok({{"code", 0}, {"data", {{"code", 1}, {"model_result", {{"summary", ""}, {"subtitle", json::array()}}}}}});
      const auto noSummary = bili({{"action", "video_summary"}, {"video", "BV1GJ411x7h7"}});
      Check(noSummary["ok"] == true && noSummary["summary"].is_null() && noSummary.contains("notice"), "videos without an AI summary say so");
      const auto noSubtitle = bili({{"action", "video_subtitle"}, {"video", "BV1GJ411x7h7"}});
      Check(noSubtitle["ok"] == true && noSubtitle["subtitle"].is_null() && noSubtitle.contains("notice"), "videos without AI subtitles say so");

      json lines = json::array();
      for (int i = 0; i < 400; ++i) lines.push_back({{"start_timestamp", i * 2.5}, {"end_timestamp", i * 2.5 + 2}, {"content", "第" + std::to_string(i) + "句字幕"}});
      json transcript = {{"code", 0}, {"model_result", {{"summary", "总结"}, {"subtitle", {{{"timestamp", 0}, {"part_subtitle", lines}}}}}}};
      routes[api + "/x/web-interface/view/conclusion/get"] = ok({{"code", 0}, {"data", transcript}});
      urls.clear();
      const auto subtitle = bili({{"action", "video_subtitle"}, {"video", "BV1GJ411x7h7"}, {"part", 2}});
      Check(subtitle["subtitle"].get<std::string>().rfind("[0:00] 第0句字幕\n[0:02] 第1句字幕\n", 0) == 0 &&
        subtitle["total_pages"].get<int>() >= 2 && subtitle["has_more"] == true && subtitle.dump().size() < 16000 && requested("cid=12"),
        "AI subtitles for the requested part carry timestamps and page under the result limit");
      const auto lastPage = bili({{"action", "video_subtitle"}, {"video", "BV1GJ411x7h7"}, {"page", subtitle["total_pages"]}});
      Check(lastPage["has_more"] == false && lastPage["subtitle"].get<std::string>().find("第399句字幕") != std::string::npos, "last subtitle page");
      Check(bili({{"action", "video_subtitle"}, {"video", "BV1GJ411x7h7"}, {"page", 50}})["ok"] == false, "subtitle page past the end");
      Check(bili({{"action", "video_subtitle"}, {"video", "BV1GJ411x7h7"}}, "")["ok"] == false, "subtitles need the JPet login");

      json replies = json::array();
      for (int i = 0; i < 10; ++i) replies.push_back({{"member", {{"uname", std::string(400, 'u')}}}, {"content", {{"message", std::string(2000, 'c')}}}, {"like", i}});
      routes[api + "/x/v2/reply"] = ok({{"code", 0}, {"data", {{"replies", replies}, {"page", {{"count", 30}}}}}});
      urls.clear();
      const auto comments = bili({{"action", "video_comments"}, {"video", "BV1GJ411x7h7"}, {"limit", 10}}, "");
      Check(comments["ok"] == true && comments.dump().size() < 16000 && requested("oid=80433022") && requested("sort=1") &&
        (comments["results"].size() == 10 || comments.contains("notice")), "comments resolve aid and stay within the result budget");

      routes[live + "/room/v1/Room/room_init"] = ok({{"code", 0}, {"data", {{"room_id", 1024}, {"uid", 2}}}});
      routes[live + "/room/v1/Room/get_status_info_by_uids"] = ok({{"code", 0}, {"data", {{"2", {{"room_id", 1024}, {"uid", 2}, {"uname", "碧诗"},
        {"title", "直播"}, {"live_status", 1}, {"online", 100}, {"live_time", 1699999000}, {"area_v2_name", "单机"}}}}}});
      urls.clear();
      const auto room = bili({{"action", "live_room"}, {"room_id", 1024}}, "");
      Check(room["room"]["status"] == "live" && room["room"]["online"] == 100 && room["room"]["url"] == "https://live.bilibili.com/1024" &&
        room["room"]["live_since"] == 1699999000 && requested("uids%5B%5D=2"), "room ids resolve to the streamer's live status");
      Check(bili({{"action", "live_room"}, {"mid", 3}}, "")["error"] == "该用户没有开通直播间", "users without rooms");

      json seasons = json::array();
      for (int i = 0; i < 5; ++i) seasons.push_back({{"title", "番" + std::to_string(i)}, {"season_id", 100 + i}, {"new_ep", {{"index_show", "更新至第1话"}}}});
      routes[api + "/pgc/web/rank/list"] = ok({{"code", 0}, {"result", {{"list", seasons}}}});
      const auto ranking = bili({{"action", "ranking"}, {"category", "bangumi"}, {"page", 2}, {"limit", 2}}, "");
      Check(ranking["results"][0]["rank"] == 3 && ranking["results"][0]["title"] == "番2" &&
        ranking["results"][0]["url"] == "https://www.bilibili.com/bangumi/play/ss102" && ranking["has_more"] == true && ranking["total"] == 5,
        "PGC rankings page locally with absolute ranks");

      routes[api + "/x/web-interface/history/cursor"] = ok({{"code", 0}, {"data", {{"list", {
        {{"title", "看完的"}, {"progress", -1}, {"history", {{"business", "archive"}, {"bvid", "BV1GJ411x7h7"}}}},
        {{"title", "直播"}, {"history", {{"business", "live"}, {"oid", 1024}}}}}}}}});
      const auto history = bili({{"action", "history"}});
      Check(history["results"][0]["finished"] == true && !history["results"][0].contains("progress") &&
        history["results"][0]["url"] == "https://www.bilibili.com/video/BV1GJ411x7h7" && history["results"][1]["url"] == "https://live.bilibili.com/1024",
        "history links each business type");

      json post = {{"id_str", "1245284537985925159"}, {"type", "DYNAMIC_TYPE_AV"}};
      post["modules"]["module_author"] = {{"name", "UP"}, {"pub_ts", 1699990000}};
      post["modules"]["module_dynamic"]["desc"]["text"] = "新视频";
      post["modules"]["module_dynamic"]["major"]["archive"] = {{"title", "标题"}, {"bvid", "BV1GJ411x7h7"}};
      routes[api + "/x/polymer/web-dynamic/v1/feed/space"] = ok({{"code", 0}, {"data", {{"items", json::array({post})}}}});
      const auto dynamics = bili({{"action", "user_dynamics"}, {"mid", 2}}, "");
      Check(dynamics["results"][0] == json{{"type", "av"}, {"author", "UP"}, {"published_at", 1699990000}, {"text", "新视频"},
        {"video", {{"title", "标题"}, {"bvid", "BV1GJ411x7h7"}, {"url", "https://www.bilibili.com/video/BV1GJ411x7h7"}}}, {"url", "https://t.bilibili.com/1245284537985925159"}},
        "dynamics reuse the notifier's text extraction and keep 19-digit IDs");

      routes[api + "/pgc/web/timeline"] = ok({{"code", 0}, {"result", {{{"date", "10-24"}, {"day_of_week", 6}, {"is_today", 1}, {"episodes", {
        {{"title", "已更新"}, {"pub_index", "第4话"}, {"published", 1}, {"episode_id", 7065884}}, {{"title", "待更新"}, {"published", 0}, {"episode_id", 0}}}}}}}});
      const auto timeline = bili({{"action", "bangumi_timeline"}}, "");
      Check(timeline["days"][0]["today"] == 1 && timeline["days"][0]["episodes"][0]["url"] == "https://www.bilibili.com/bangumi/play/ep7065884" &&
        !timeline["days"][0]["episodes"][1].contains("url") && requested("types=1") && requested("after=0"),
        "unpublished episodes get no placeholder link");

      routes[api + "/x/v3/fav/folder/created/list-all"] = ok({{"code", 0}, {"data", {{"list", {{{"id", 5542}, {"title", "默认收藏夹"}, {"media_count", 8}}}}}}});
      urls.clear();
      Check(bili({{"action", "favorites"}})["results"][0] == json{{"folder_id", 5542}, {"title", "默认收藏夹"}, {"count", 8}} && requested("up_mid=42"),
        "favorites list the JPet account's folders");

      routes[api + "/x/msgfeed/unread"] = ok({{"code", 0}, {"data", {{"reply", 2}, {"at", 1}, {"like", 0}, {"sys_msg", 3}, {"up", 0}}}});
      routes["https://api.vc.bilibili.com/"] = {{}, 412, ""};
      const auto unread = bili({{"action", "unread"}});
      Check(unread["ok"] == true && unread["unread"]["replies"] == 2 && unread["unread"]["system"] == 3 && unread.contains("notice"),
        "a failing private-message count does not hide the other counts");

      for (const auto& [reply, expected] : {std::pair{ok({{"code", -352}}), "访问限制"}, {Voice::BilibiliReply{{}, 412, ""}, "访问限制"},
          {ok({{"code", -404}}), "不存在"}, {ok({{"code", -101}}), "重新登录"}, {ok({{"code", 12345}, {"message", "<b>参数</b>错误"}}), "参数错误"},
          {Voice::BilibiliReply{{}, 0, "工具网络请求失败或超时，请稍后重试"}, "超时"}}) {
        routes[api + "/x/web-interface/card"] = reply;
        const auto failed = bili({{"action", "user_info"}, {"mid", 2}}, "");
        Check(failed["ok"] == false && failed["error"].get<std::string>().find(expected) != std::string::npos, "Bilibili errors are readable");
      }
      routes[api + "/x/web-interface/nav"] = ok({{"code", -101}, {"data", {{"isLogin", false}, {"wbi_img", wbi}}}});
      Check(bili({{"action", "history"}})["error"] == "B站登录已失效，请重新登录JPet" &&
        bili({{"action", "hot_search"}}, "")["ok"] == true, "expired logins block account actions but keep WBI keys for public ones");
    }
    auto web = Voice::WebSearchResults({{"output", {{"choices", {{{"message", {{"content", "结论[ref_1]"}}}}}},
      {"search_info", {{"search_results", {{{"index", 1}, {"title", "来源"}, {"url", "https://example.com/page"}}}}}}}}}, 5);
    Check(web["sources"][0]["url"] == "https://example.com/page" && web["summary"] == "结论[ref_1]", "web search retains citations and sources");
    Check(!Voice::WebSearchResults({{"output", {{"choices", {{{"message", {{"content", "没有来源"}}}}}}}}}, 5)["ok"].get<bool>(), "uncited generation is not represented as successful search");

    const auto bad = ExecuteTool({"c1", "jpet_settings", R"({"action":"update","section":"appearance","settings":{"volume":40}})"}, dependencies);
    Check(bad["ok"] == false && bad["code"] == "invalid_arguments" && bad["error"].get<std::string>().find("sweat") != std::string::npos,
        "parameter rejection returns actionable allowed fields rather than a generic network error");
    Check(ExecuteTool({"invalid-json", "jpet_settings", "{bad"}, dependencies)["code"] == "invalid_arguments", "invalid JSON is a tool parameter error");

    std::promise<void> started, release;
    auto waiting = release.get_future().share();
    std::atomic<int> executed{0};
    dependencies.game = [&](const json&) {
      if (++executed == 1) { started.set_value(); waiting.wait(); }
      return json{{"ok", true}};
    };
    Voice::ToolExecutor executor(dependencies);
    executor.Submit({Call("get_game_profile", json::object()), {"c2", "get_game_profile", "{}"}});
    Check(started.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready, "executor runs outside UI thread");
    executor.Cancel(); release.set_value();
    executor.Submit({{"new", "get_game_profile", "{}"}});
    std::vector<Voice::ToolExecutor::Result> results;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (results.empty() && std::chrono::steady_clock::now() < deadline) {
      results = executor.Poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(executed == 2 && results.size() == 1 && results.front().call.id == "new", "cancellation drops queued work and stale in-flight result");
    std::cout << "Voice tool dispatch, search parsing and cancellation tests passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
