#include "VoiceTools.hpp"
#include "VoiceHistory.hpp"
#include "voice_history_helpers.hpp"
#include "VoiceSettingsQueue.hpp"
#include <chrono>
#include <atomic>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
using nlohmann::json;
void Check(bool condition, const char* label) { if (!condition) throw std::runtime_error(label); }
Voice::ToolCall Call(const char* name, json args) { return {"c1", name, args.dump()}; }
struct SessionFixture {
  std::vector<json> sent;
  std::vector<Voice::ToolCall> calls;
  std::string state;
  size_t interruptions = 0;
  Voice::History history;
  Voice::Session session{{
    [this](const json& value) { sent.push_back(value); }, [](const std::string&) {}, [] { return false; }, [] {},
    [this](const std::string& value, const std::string&) { state = value; }, [](const std::string&) {},
    [this](const std::vector<Voice::ToolCall>& value) { calls.insert(calls.end(), value.begin(), value.end()); },
    [this](const Voice::ConversationEvent& event) { history.Apply(event); },
    [this] { ++interruptions; }
  }, Voice::ToolDefinitions()};
  SessionFixture() {
    session.Receive({{"type", "session.created"}});
    session.Receive({{"type", "session.updated"}});
    session.BeginInput(); session.AppendInput(std::string(6400, '\0')); session.EndInput();
    session.Receive({{"type", "input_audio_buffer.committed"}, {"item_id", "u1"}});
    Created("r1");
  }
  void Created(const char* id) { session.Receive({{"type", "response.created"}, {"response", {{"id", id}}}}); }
  json Item(const char* id = "c1") {
    return {{"type", "function_call"}, {"call_id", id}, {"name", "get_game_profile"}, {"arguments", "{}"}};
  }
  void Arguments(const char* id = "c1", const char* response = "r1") {
    auto event = Item(id); event["type"] = "response.function_call_arguments.done"; event["response_id"] = response;
    session.Receive(event);
  }
  void Done(json output = json::array(), const char* status = "completed", const char* id = "r1") {
    session.Receive({{"type", "response.done"}, {"response", {{"id", id}, {"status", status}, {"output", output}}}});
  }
  size_t Count(const char* type) { size_t count = 0; for (const auto& value : sent) if (value["type"] == type) ++count; return count; }
};
}

int main() {
  try {
    const auto definitions = Voice::ToolDefinitions();
    Check(definitions.size() == 15, "focused queries and action tools registered within relay limit");
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
    Check(settingsCalls == beforeBadSettings && mutations == 0, "invalid settings never reach the runtime or cloud command");
    Check(run("jpet_settings", {{"action", "change_clothes"}, {"clothes_id", 1}})["ok"] == true && mutations == 1, "settings clothing changes use the existing cloud command");
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
      {"get_game_achievements", "achievements"}, {"get_game_statistics", "statistics"}, {"get_game_rank", "rank"}
    };
    for (const auto& [tool, section] : queriesByTool)
      Check(run(tool, json::object())["ok"] == true && last["section"] == section, "query dispatch selects only its own content");
    for (const auto* tool : {"get_task_catalog", "get_task_history", "get_game_achievements", "get_game_rank"}) {
      Check(run(tool, {{"offset", 5}, {"limit", 10}})["ok"] == true && last["offset"] == 5 && last["limit"] == 10, "pagination passed to adapter");
      Check(run(tool, {{"limit", 11}})["ok"] == false && run(tool, {{"offset", -1}})["ok"] == false, "pagination bounds checked before read");
    }
    Check(run("get_game_rank", {{"metric", "exp"}})["ok"] == true && last["metric"] == "exp", "rank metric forwarded");
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
      Check(run("bilibili_search", {{"query", "千问"}, {"type", type}})["ok"] == true, "six Bilibili categories");
    const auto beforeBadSearch = external;
    for (const auto& args : {json{{"query", ""}}, json{{"query", "test\nCookie: secret"}},
      json{{"query", "x"}, {"url", "https://evil.test"}}, json{{"query", "x"}, {"page", 51}},
      json{{"query", "x"}, {"type", "user"}, {"order", "latest"}}})
      Check(run("bilibili_search", args)["ok"] == false, "bad search arguments rejected");
    Check(external == beforeBadSearch && Voice::ExecuteTool({"c", "game_action", "{"}, dependencies)["ok"] == false,
      "malformed JSON and search input have no effects");
    Check(run("shell", {{"command", "ls"}})["ok"] == false, "unknown tools cannot execute arbitrary code");

    auto snapshot = json{{"revision", 9}, {"server_time", 1000}, {"cookie", "secret-cookie"}, {"session", "secret-session"},
      {"save", {{"failcount", 2}, {"achievements", {{"metrics", {{"tasks", 3}}}, {"streak", 1}}}, {"private", "secret-private"}}}};
    json profile = {{"attributes", {{"exp", 50}}}, {"buffs", {"guard"}}, {"buycost", 11},
      {"clothes", {{"current", 0}, {"unlock", {true, true, false}}}}, {"cloud", {{"session", "secret-cloud"}}}};
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
      return Voice::GameView(snapshot, profile, taskData, achievementData, {{"online", true}},
        {{"uid", "123"}, {"name", "测试"}, {"cookies", "secret-account"}}, section, offset, limit);
    };
    Check(view("profile")["profile"]["buycost"] == 11 && !view("profile")["profile"].contains("clothes"), "profile excludes unrelated outfit state");
    Check(view("task_queue")["queue"][0]["entry_id"] == 42 && !view("task_queue").contains("tasks"), "queue retains actionable IDs without catalogs");
    Check(view("statistics")["statistics"]["failcount"] == 2, "statistics retain metrics without catalogs");
    Check(view("clothes")["clothes"]["unlock"][1] == true && !view("clothes").contains("profile"), "outfit query retains unlock conditions");
    for (const auto& [tool, section] : queriesByTool) if (std::string(section) != "rank") {
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
    auto web = Voice::WebSearchResults({{"output", {{"choices", {{{"message", {{"content", "结论[ref_1]"}}}}}},
      {"search_info", {{"search_results", {{{"index", 1}, {"title", "来源"}, {"url", "https://example.com/page"}}}}}}}}}, 5);
    Check(web["sources"][0]["url"] == "https://example.com/page" && web["summary"] == "结论[ref_1]", "web search retains citations and sources");
    Check(!Voice::WebSearchResults({{"output", {{"choices", {{{"message", {{"content", "没有来源"}}}}}}}}}, 5)["ok"].get<bool>(), "uncited generation is not represented as successful search");

    SessionFixture first;
    Check(first.sent.front()["session"].dump().size() < 16000, "session instructions and fifteen tools fit the relay handshake limit");
    Check(first.sent.front()["session"]["tools"] == definitions && !first.sent.front()["session"].contains("enable_search"), "custom tools configured without conflicting built-in search");
    first.session.Receive({{"type", "response.audio_transcript.done"}, {"response_id", "r1"}, {"transcript", "我先查询一下。"}});
    first.Arguments();
    first.session.Receive({{"type", "response.output_item.done"}, {"response_id", "r1"}, {"item", first.Item()}});
    Check(first.calls.empty(), "tool waits for complete response before execution");
    first.Done(json::array({first.Item()}));
    Check(first.calls.size() == 1 && first.session.Busy() && first.state == "tool", "three duplicate delivery forms execute once");
    Check(TurnSnapshot(first.history)[0]["state"] == "pending", "tool invocation does not finish the history turn");
    first.session.BeginInput();
    const auto openMicChunks = first.Count("input_audio_buffer.append");
    first.session.CompleteTool("c1", {{"ok", true}});
    Check(first.Count("input_audio_buffer.append") == openMicChunks, "open microphone needs no synthetic continuation audio");
    Check(first.Count("conversation.item.create") == 1 && first.Count("response.create") == 1, "tool output precedes one continuation");
    Check(first.sent.back()["response"]["modalities"] == json::array({"text", "audio"}),
        "tool continuation explicitly requests a spoken reply as in the provider example");
    first.session.CompleteTool("c1", {{"ok", true}});
    Check(first.Count("response.create") == 1, "duplicate tool completion ignored");
    first.Created("r2");
    first.session.Receive({{"type", "response.audio_transcript.delta"}, {"response_id", "r2"}, {"delta", "结果是"}});
    first.session.Receive({{"type", "response.audio_transcript.done"}, {"response_id", "r2"}, {"transcript", "结果是 100 经验。"}});
    first.Done(json::array(), "completed", "r2");
    const auto firstHistory = TurnSnapshot(first.history);
    Check(firstHistory.size() == 1 && firstHistory[0]["state"] == "completed" &&
        firstHistory[0]["assistant"] == "我先查询一下。\n\n结果是 100 经验。", "tool continuations retain all reply text in one history round");
    SessionFixture multiple;
    multiple.Arguments("c1"); multiple.Arguments("c2"); multiple.Done();
    multiple.session.CompleteTool("c2", {{"ok", false}, {"error", "失败"}});
    Check(multiple.Count("response.create") == 0 && multiple.session.Busy(), "all tool results required before continuation");
    multiple.session.CompleteTool("c1", {{"ok", true}});
    Check(multiple.Count("response.create") == 1, "one continuation for multiple calls");
    std::string continuationSilence;
    bool afterCreate = false;
    for (const auto& event : multiple.sent) {
      if (event["type"] == "response.create") afterCreate = true;
      if (afterCreate && event["type"] == "input_audio_buffer.append") continuationSilence += Voice::DecodeBase64(event["audio"]);
    }
    Check(!multiple.session.Recording() && continuationSilence == std::string(Voice::InputBytesPerSecond, '\0'),
        "closed microphone supplies one second of zero PCM after tool continuation without reopening capture");
    const auto completedChunks = multiple.Count("input_audio_buffer.append");
    multiple.session.CompleteTool("c1", {{"ok", true}});
    Check(multiple.Count("input_audio_buffer.append") == completedChunks, "duplicate tool completion cannot add more silence");
    multiple.Created("r2"); multiple.Arguments("c3", "r2"); multiple.Done(json::array(), "completed", "r2");
    Check(multiple.calls.size() == 3, "subsequent tool round supported");
    multiple.session.CompleteTool("c3", {{"ok", true}});
    multiple.Arguments("old-delayed", "r1");
    multiple.Created("r1");
    multiple.Created("r3");
    multiple.Done(json::array(), "completed", "r3");
    Check(multiple.calls.size() == 3, "retired response cannot issue delayed calls or steal next response ID");
    SessionFixture cancelled;
    cancelled.Arguments(); cancelled.Done(json::array(), "cancelled");
    Check(cancelled.calls.empty() && cancelled.Count("conversation.item.create") == 0, "cancelled draft cannot invoke mutation or send an invalid output");
    // Replay the 22:06:16 log sequence: a draft in a cancelled response, then
    // another VAD turn. No orphan function output may be sent to the provider.
    cancelled.session.Receive({{"type", "input_audio_buffer.speech_started"}, {"item_id", "u2"}});
    cancelled.session.Receive({{"type", "input_audio_buffer.committed"}, {"item_id", "u2"}});
    cancelled.Created("r2"); cancelled.Arguments("valid-after-cancel", "r2"); cancelled.Done(json::array(), "completed", "r2");
    cancelled.session.CompleteTool("valid-after-cancel", {{"ok", true}});
    Check(cancelled.calls.size() == 1 && cancelled.Count("conversation.item.create") == 1 && cancelled.Count("response.create") == 1,
        "a cancelled draft does not poison the next tool turn");
    for (const auto* outcome : {"cancelled", "incomplete", "failed"}) {
      SessionFixture aborted;
      aborted.Arguments(); aborted.Done(json::array({aborted.Item()}), outcome);
      Check(aborted.calls.empty() && aborted.Count("conversation.item.create") == 0 && aborted.session.Ready() && !aborted.session.Busy(),
          "aborted final output is discarded without closing the connection or leaving pending drafts");
    }
    SessionFixture draftInterrupt;
    draftInterrupt.Arguments();
    draftInterrupt.session.Receive({{"type", "input_audio_buffer.speech_started"}, {"item_id", "u2"}});
    draftInterrupt.Done(json::array({draftInterrupt.Item()}), "cancelled");
    Check(draftInterrupt.Count("conversation.item.create") == 0 && draftInterrupt.calls.empty(), "speech before response completion drops drafts without returning a cancellation output");

    SessionFixture badArguments;
    auto invalid = badArguments.Item(); invalid["name"] = "jpet_settings"; invalid["arguments"] = R"({"action":"update","section":"appearance","settings":{"volume":40}})";
    badArguments.Done(json::array({invalid}));
    const auto bad = ExecuteTool(badArguments.calls.back(), dependencies);
    Check(bad["ok"] == false && bad["code"] == "invalid_arguments" && bad["error"].get<std::string>().find("sweat") != std::string::npos,
        "parameter rejection returns actionable allowed fields rather than a generic network error");
    badArguments.session.CompleteTool("c1", bad);
    badArguments.Created("r2");
    auto corrected = badArguments.Item("c2"); corrected["name"] = "jpet_settings"; corrected["arguments"] = R"({"action":"update","section":"appearance","settings":{"sweat":false}})";
    badArguments.Done(json::array({corrected}), "completed", "r2");
    badArguments.session.CompleteTool("c2", {{"ok", true}, {"settings", {{"appearance", {{"sweat", false}}}}}});
    badArguments.Created("r3");
    badArguments.session.Receive({{"type", "response.audio_transcript.done"}, {"response_id", "r3"}, {"transcript", "已经去掉汗了。"}});
    badArguments.Done(json::array(), "completed", "r3");
    Check(badArguments.Count("response.create") == 2 && badArguments.session.Ready() && !badArguments.session.Busy(), "a corrected tool call and spoken continuation finish after parameter error");
    const auto messages = badArguments.history.Snapshot()["list"];
    Check(messages.size() == 4 && messages[1]["role"] == "tool" && messages[1]["state"] == "failed" &&
        messages[2]["role"] == "tool" && messages[2]["state"] == "completed" && messages[3]["text"] == "已经去掉汗了。",
        "failed and corrected tool calls remain separate ordered messages");
    Check(ExecuteTool({"invalid-json", "jpet_settings", "{bad"}, dependencies)["code"] == "invalid_arguments", "invalid JSON is a tool parameter error");

    SessionFixture slot;
    slot.Arguments(); slot.Done(); slot.session.CompleteTool("c1", {{"ok", false}, {"error", "参数错误"}});
    std::string continuationId;
    for (const auto& event : slot.sent) if (event["type"] == "response.create") continuationId = event["event_id"];
    slot.session.Receive({{"type", "error"}, {"error", {{"code", "invalid_request_error"}, {"message", "Another response is in progress"}, {"event_id", continuationId}}}});
    Check(slot.session.Ready() && slot.session.WaitingForReply() && slot.Count("response.create") == 1, "response slot refusal waits without disconnecting or immediately retrying");
    slot.Done(json::array(), "completed", "r1");
    Check(slot.Count("response.create") == 1, "a duplicate old completion cannot release the occupied slot");
    slot.Done(json::array(), "cancelled", "remote-slot");
    Check(slot.Count("response.create") == 2 && slot.Count("conversation.item.create") == 1 && slot.calls.size() == 1,
        "remote completion resumes speech without repeating tools or outputs");
    slot.Created("r2"); slot.Done(json::array(), "completed", "r2");
    Check(!slot.session.Busy() && slot.session.Ready(), "slot recovery completes normally");

    SessionFixture superseded;
    superseded.Arguments(); superseded.Done(); superseded.session.CompleteTool("c1", {{"ok", true}});
    std::string staleRequest;
    for (const auto& event : superseded.sent) if (event["type"] == "response.create") staleRequest = event["event_id"];
    superseded.session.BeginInput();
    superseded.session.Receive({{"type", "input_audio_buffer.speech_started"}, {"item_id", "u2"}});
    superseded.session.Receive({{"type", "error"}, {"error", {{"message", "User is speaking"}, {"event_id", staleRequest}}}});
    Check(superseded.session.Ready() && superseded.session.Recording(), "late rejection of an interrupted continuation cannot close the current microphone");
    SessionFixture interrupted;
    interrupted.Arguments(); interrupted.Done(); interrupted.session.BeginInput();
    Check(interrupted.Count("conversation.item.create") == 0 && interrupted.interruptions == 0, "press does not cancel pending tools");
    interrupted.session.Receive({{"type", "input_audio_buffer.speech_started"}, {"item_id", "u2"}});
    Check(interrupted.interruptions == 1, "server speech event cancels the tool executor");
    const auto sent = interrupted.sent.size();
    interrupted.session.CompleteTool("c1", {{"ok", true}});
    Check(interrupted.sent.size() == sent && interrupted.calls.size() == 1 && interrupted.Count("conversation.item.create") == 1, "interrupted call is closed and stale completion ignored");
    SessionFixture stale;
    stale.Arguments("old", "old-response"); stale.Done();
    Check(stale.calls.empty(), "other response cannot enqueue tools");
    SessionFixture fallback;
    fallback.Done(json::array({fallback.Item()}));
    Check(fallback.calls.size() == 1, "response.done output fallback works without argument event");
    SessionFixture reset;
    reset.Arguments(); reset.Done(); reset.session.Reset(); reset.session.CompleteTool("c1", {{"ok", true}});
    Check(reset.Count("conversation.item.create") == 0 && !reset.session.Busy(), "reset invalidates tool results");

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
    std::cout << "Voice tool dispatch, protocol, search parsing and cancellation tests passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
