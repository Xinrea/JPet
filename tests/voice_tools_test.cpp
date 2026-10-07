#include "VoiceTools.hpp"
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
  Voice::Session session{{
    [this](const json& value) { sent.push_back(value); }, [](const std::string&) {}, [] { return false; }, [] {},
    [this](const std::string& value, const std::string&) { state = value; }, [](const std::string&) {},
    [this](const std::vector<Voice::ToolCall>& value) { calls.insert(calls.end(), value.begin(), value.end()); }
  }, Voice::ToolDefinitions()};
  SessionFixture() {
    session.Receive({{"type", "session.created"}});
    session.Receive({{"type", "session.updated"}});
    session.BeginInput(); session.AppendInput(std::string(6400, '\0')); session.EndInput();
    Created("r1");
  }
  void Created(const char* id) { session.Receive({{"type", "response.created"}, {"response", {{"id", id}}}}); }
  json Item(const char* id = "c1") {
    return {{"type", "function_call"}, {"call_id", id}, {"name", "get_game_state"}, {"arguments", R"({"section":"all"})"}};
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
    Check(definitions.size() == 6, "six tool families registered");
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
    Check(run("get_game_state", json::object())["ok"] == true && last["section"] == "all", "all-state default query");
    for (const char* section : {"profile", "tasks", "achievements", "statistics", "rank"})
      Check(run("get_game_state", {{"section", section}})["ok"] == true, "all query sections supported");
    for (const auto& pair : {std::pair{"start_task", "task.start"}, {"queue_task", "task.queue"}, {"cancel_task", "task.cancel"}}) {
      dependencies.command = [&](const json& command) { ++mutations; Check(command["type"] == pair.second && command["id"] == 13, "task command has directory ID"); return std::string{}; };
      Check(run("game_action", {{"action", pair.first}, {"task_id", 13}})["ok"] == true, "task action succeeds");
    }
    dependencies.command = [&](const json& command) { ++mutations; last = command; return std::string{}; };
    for (const auto& pair : {std::pair{"upgrade_attribute", "attr.buy"}, {"refund_attribute", "attr.refund"}}) {
      dependencies.command = [&](const json& command) { ++mutations; Check(command["type"] == pair.second && command["attr"] == "intellect", "attribute command uses server validation"); return std::string{}; };
      Check(run("game_action", {{"action", pair.first}, {"attribute", "intellect"}})["ok"] == true, "attribute action succeeds");
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
    const auto game = Voice::GameView(snapshot, {{"attributes", {{"exp", 50}}}, {"buffs", {{{"name", "好运"}}}}, {"buycost", 11}, {"cloud", {{"session", "secret-cloud"}}}},
      {{"current", nullptr}, {"list", {{{"id", 1}, {"success_rate", 0.8}}}}, {"queue", {{{"entry_id", 42}}}}, {"history", {{{"success", true}}}}},
      {{"total", 20}, {"unlocked", 1}, {"list", {{{"id", "first-task"}, {"progress", 1}}}}},
      {{"online", true}}, {{"uid", "123"}, {"name", "测试"}, {"cookies", "secret-account"}}, "all");
    Check(game["profile"]["buycost"] == 11 && game["tasks"]["queue"][0]["entry_id"] == 42 &&
      game["statistics"]["failcount"] == 2 && game["achievements"]["total"] == 20, "game data retains mechanics and IDs");
    Check(game.dump().find("secret-") == std::string::npos, "game view excludes credentials and raw save/config");
    Check(!Voice::GameView(snapshot, json::object(), json::object(), json::object(), {{"online", false}}, json::object(), "tasks").contains("profile"), "section query remains focused");
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
    Check(first.sent.front()["session"]["tools"] == definitions && !first.sent.front()["session"].contains("enable_search"), "custom tools configured without conflicting built-in search");
    first.Arguments();
    first.session.Receive({{"type", "response.output_item.done"}, {"response_id", "r1"}, {"item", first.Item()}});
    Check(first.calls.empty(), "tool waits for complete response before execution");
    first.Done({first.Item()});
    Check(first.calls.size() == 1 && first.session.Busy() && first.state == "tool", "three duplicate delivery forms execute once");
    first.session.CompleteTool("c1", {{"ok", true}});
    Check(first.Count("conversation.item.create") == 1 && first.Count("response.create") == 2, "tool output precedes one continuation");
    first.session.CompleteTool("c1", {{"ok", true}});
    Check(first.Count("response.create") == 2, "duplicate tool completion ignored");
    SessionFixture multiple;
    multiple.Arguments("c1"); multiple.Arguments("c2"); multiple.Done();
    multiple.session.CompleteTool("c2", {{"ok", false}, {"error", "失败"}});
    Check(multiple.Count("response.create") == 1 && multiple.session.Busy(), "all tool results required before continuation");
    multiple.session.CompleteTool("c1", {{"ok", true}});
    Check(multiple.Count("response.create") == 2, "one continuation for multiple calls");
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
    Check(cancelled.calls.empty(), "cancelled response cannot invoke mutation");
    SessionFixture interrupted;
    interrupted.Arguments(); interrupted.Done(); interrupted.session.BeginInput();
    const auto sent = interrupted.sent.size();
    interrupted.session.CompleteTool("c1", {{"ok", true}});
    Check(interrupted.sent.size() == sent && interrupted.calls.size() == 1 && interrupted.Count("conversation.item.create") == 1, "interrupted call is closed and stale completion ignored");
    SessionFixture stale;
    stale.Arguments("old", "old-response"); stale.Done();
    Check(stale.calls.empty(), "other response cannot enqueue tools");
    SessionFixture fallback;
    fallback.Done({fallback.Item()});
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
    executor.Submit({Call("get_game_state", json::object()), {"c2", "get_game_state", "{}"}});
    Check(started.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready, "executor runs outside UI thread");
    executor.Cancel(); release.set_value();
    executor.Submit({{"new", "get_game_state", "{}"}});
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
