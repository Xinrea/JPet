// Opt-in integration probe for the bilibili tool. Reads the login cookie without
// printing it, opens RocksDB read-only and never changes game state.
#import <Foundation/Foundation.h>
#include "VoiceToolsNetwork.hpp"
#include "VoiceTools.hpp"
#include "VoiceBilibili.hpp"
#include <rocksdb/db.h>
#include <iostream>
namespace {
using nlohmann::json;
int failures = 0;
void Report(const char* name, const json& result) {
  const bool ok = result.value("ok", false);
  std::cout << name << ": " << (ok ? "PASS" : "FAIL") << '\n';
  if (ok && result.contains("results")) std::cout << "result_count=" << result["results"].size() << '\n';
  if (ok && result.contains("sources")) std::cout << "source_count=" << result["sources"].size() << '\n';
  if (!ok) { ++failures; std::cout << result.value("error", std::string("unexpected service response")) << '\n'; }
}
}
int main(int argc, char** argv) {
  @autoreleasepool {
    try {
      if (argc != 2 && !(argc == 3 && std::string(argv[2]) == "--bilibili-only")) {
        std::cerr << "Usage: voice_tools_network_probe <JPet profile directory> [--bilibili-only]\n"; return 2;
      }
      const std::string profile = argv[1];
      rocksdb::DB* raw = nullptr;
      if (!rocksdb::DB::OpenForReadOnly(rocksdb::Options{}, profile + "/GameData", &raw).ok()) { std::cerr << "cannot open profile read-only\n"; return 1; }
      std::unique_ptr<rocksdb::DB> db(raw);
      std::string cookies, uid;
      db->Get(rocksdb::ReadOptions{}, "cookies", &cookies); db->Get(rocksdb::ReadOptions{}, "uid", &uid);
      const auto bilibili = [&](const char* name, json args) {
        Report(name, Voice::QueryBilibili(Voice::BilibiliRequest(args), cookies, uid));
      };
      if (argc == 3) {
        for (const auto& pair : {std::pair{"video", "千问"}, {"user", "哔哩哔哩"}, {"live", "游戏"},
          {"article", "千问"}, {"bangumi", "凡人修仙传"}, {"film", "纪录片"}})
          bilibili(pair.first, {{"action", "search"}, {"query", pair.second}, {"type", pair.first}, {"limit", 3}});
        // Public actions; a well-known video, uploader and live room keep results stable.
        for (const auto& args : {json{{"action", "hot_search"}}, json{{"action", "popular"}}, json{{"action", "ranking"}},
          json{{"action", "ranking"}, {"category", "bangumi"}}, json{{"action", "bangumi_timeline"}, {"days", 2}},
          json{{"action", "video_info"}, {"video", "BV1GJ411x7h7"}}, json{{"action", "video_comments"}, {"video", "BV1GJ411x7h7"}},
          json{{"action", "related_videos"}, {"video", "BV1GJ411x7h7"}}, json{{"action", "user_info"}, {"mid", 2}},
          json{{"action", "user_videos"}, {"mid", 2}}, json{{"action", "user_dynamics"}, {"mid", 2}},
          json{{"action", "live_room"}, {"room_id", 1024}}, json{{"action", "live_recommend"}}})
          bilibili(args["action"].get_ref<const std::string&>().c_str(), args);
        // Account actions need the profile's login.
        for (const auto& args : {json{{"action", "my_info"}}, json{{"action", "unread"}}, json{{"action", "following_live"}},
          json{{"action", "following_feed"}}, json{{"action", "history"}}, json{{"action", "watch_later"}},
          json{{"action", "favorites"}}, json{{"action", "followings"}},
          json{{"action", "video_summary"}, {"video", "BV1ZMSGYfEGN"}}, json{{"action", "video_subtitle"}, {"video", "BV1ZMSGYfEGN"}}})
          bilibili(args["action"].get_ref<const std::string&>().c_str(), args);
        return failures ? 1 : 0;
      }
      bilibili("Bilibili video search", {{"action", "search"}, {"query", "千问"}, {"limit", 3}});
      return failures ? 1 : 0;
    } catch (const std::exception&) { std::cerr << "integration probe failed; raw service/credential data suppressed\n"; return 1; }
  }
}
