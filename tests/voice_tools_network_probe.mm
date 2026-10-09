// Opt-in integration probe. Reads selected credentials without printing them,
// opens RocksDB read-only, never changes game state and never reads the desktop.
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#import <LocalAuthentication/LocalAuthentication.h>
#import <CoreGraphics/CoreGraphics.h>
#include "VoiceToolsNetwork.hpp"
#include "VoiceTools.hpp"
#include <rocksdb/db.h>
#include <toml++/toml.hpp>
#include <iostream>
#include <fstream>
#include <chrono>

namespace Voice { bool EncodeDesktopImage(CGImageRef, DesktopImage&, std::string&); }
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
bool CheckRealtime(const std::string& workspace, const std::string& key, const std::string& pcm = {}, bool localProbe = false) {
  NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:@(localProbe ? "ws://127.0.0.1:8791/realtime" : Voice::ConnectionUrl(workspace).c_str())]];
  [request setValue:@(("Bearer " + key).c_str()) forHTTPHeaderField:@"Authorization"];
  NSURLSession* connection = [NSURLSession sessionWithConfiguration:NSURLSessionConfiguration.ephemeralSessionConfiguration];
  NSURLSessionWebSocketTask* socket = [connection webSocketTaskWithRequest:request];
  size_t toolCalls = 0;
  std::string transcript;
  Voice::Session session{{
    [socket](const json& event) {
      auto message = [[NSURLSessionWebSocketMessage alloc] initWithString:@(event.dump().c_str())];
      [socket sendMessage:message completionHandler:^(NSError*) {}];
    }, [](const std::string&) {}, [] { return false; }, [] {}, [](const std::string&, const std::string&) {},
    [&](const std::string& value) { transcript = value; },
    [&](const std::vector<Voice::ToolCall>& calls) {
      for (const auto& call : calls) {
        if (call.name == "get_game_profile") {
          ++toolCalls;
          session.CompleteTool(call.id, {{"ok", true}, {"online", true}, {"source", "integration_fixture"},
            {"profile", {{"attributes", {{"speed", 7}, {"endurance", 9}, {"strength", 5}, {"will", 11}, {"intellect", 13}, {"exp", 2468}}}}}});
        } else session.CompleteTool(call.id, {{"ok", false}, {"error", "本次只读接口测试仅支持get_game_profile"}});
      }
    }
  }, Voice::ToolDefinitions()};
  [socket resume];
  bool success = false;
  bool submitted = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
  for (int i = 0; i < 512 && std::chrono::steady_clock::now() < deadline; ++i) {
    auto signal = dispatch_semaphore_create(0);
    __block NSString* received = nil;
    [socket receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage* message, NSError*) {
      received = message.string; dispatch_semaphore_signal(signal);
    }];
    if (dispatch_semaphore_wait(signal, dispatch_time(DISPATCH_TIME_NOW, 12 * NSEC_PER_SEC)) || !received) break;
    const auto event = json::parse(received.UTF8String, nullptr, false);
    if (!event.is_object()) break;
    const auto type = event.value("type", std::string{});
    if (localProbe && type.find(".delta") == std::string::npos) std::cout << "event=" << type << std::endl;
    if (type == "error") { std::cout << "realtime service rejected request\n"; break; }
    const bool finished = type == "response.done" && toolCalls && !session.Busy();
    session.Receive(event);
    if (session.Ready() && pcm.empty()) { success = true; break; }
    if (session.Ready() && !submitted) {
      session.BeginInput(); session.AppendInput(pcm); session.EndInput(); submitted = true;
    } else if (finished || (type == "response.done" && toolCalls && !session.Busy())) {
      success = transcript.find("2468") != std::string::npos || transcript.find("两千四百六十八") != std::string::npos ||
        transcript.find("二千四百六十八") != std::string::npos;
      break;
    }
  }
  [socket cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
  [connection invalidateAndCancel];
  if (!pcm.empty()) std::cout << "read_only_tool_calls=" << toolCalls << ", continuation_received=" << success << '\n';
  return success;
}
}
int main(int argc, char** argv) {
  @autoreleasepool {
    try {
      // Local preview proxy supplies its own server credential. This path uses
      // synthetic audio and fixtures only; it never opens a profile or keychain.
      if (argc == 3 && std::string(argv[1]) == "--realtime-local-probe") {
        std::ifstream file(argv[2], std::ios::binary);
        const std::string pcm((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (pcm.size() < 3200 || pcm.size() % 2 || pcm.size() > 320000) return 2;
        Report("native protocol via local preview", {{"ok", CheckRealtime("", "", pcm, true)}});
        return failures ? 1 : 0;
      }
      const bool realtimeCall = argc == 4 && std::string(argv[2]) == "--realtime-call";
      const bool realtimeConfig = argc == 3 && std::string(argv[2]) == "--realtime-config-only";
      if (argc != 2 && !(argc == 3 && std::string(argv[2]) == "--bilibili-only") && !realtimeCall && !realtimeConfig) {
        std::cerr << "Usage: voice_tools_network_probe <JPet profile directory> [--bilibili-only | --realtime-config-only | --realtime-call synthetic.pcm]\n"; return 2;
      }
      const std::string profile = argv[1];
      rocksdb::DB* raw = nullptr;
      if (!rocksdb::DB::OpenForReadOnly(rocksdb::Options{}, profile + "/GameData", &raw).ok()) { std::cerr << "cannot open profile read-only\n"; return 1; }
      std::unique_ptr<rocksdb::DB> db(raw);
      std::string cookies, uid;
      db->Get(rocksdb::ReadOptions{}, "cookies", &cookies); db->Get(rocksdb::ReadOptions{}, "uid", &uid);
      if (argc == 3 && !realtimeConfig) {
        for (const auto& pair : {std::pair{"video", "千问"}, {"user", "哔哩哔哩"}, {"live", "游戏"},
          {"article", "千问"}, {"bangumi", "凡人修仙传"}, {"film", "纪录片"}}) {
          Report(pair.first, Voice::SearchBilibili({{"query", pair.second}, {"type", pair.first},
            {"order", "relevance"}, {"page", 1}, {"limit", 3}}, cookies, uid));
        }
        return failures ? 1 : 0;
      }
      if (!realtimeCall && !realtimeConfig) {
        Report("Bilibili authenticated video search", Voice::SearchBilibili({{"query", "千问"}, {"type", "video"}, {"order", "relevance"}, {"page", 1}, {"limit", 3}}, cookies, uid));
        Report("game rank read", Voice::ReadGameRank({{"metric", "starcnt"}, {"limit", 3}}, "https://s.jpet.powerlive.io", uid));
      }
      const auto settings = toml::parse_file(profile + "/jpet.toml");
      const auto workspace = settings["voice"]["workspace_id"].value_or(std::string{});
      LAContext* authentication = [[LAContext alloc] init];
      authentication.interactionNotAllowed = YES;
      NSDictionary* query = @{(__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService: @"cn.vjoi.jpet.qwen", (__bridge id)kSecAttrAccount: @(profile.c_str()),
        (__bridge id)kSecReturnData: @YES, (__bridge id)kSecMatchLimit: (__bridge id)kSecMatchLimitOne,
        (__bridge id)kSecUseAuthenticationContext: authentication};
      CFTypeRef stored = nullptr;
      const auto status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &stored);
      if (status != errSecSuccess) { std::cout << "Qwen probes SKIP: credential unavailable without interactive keychain access\n"; return failures ? 1 : 0; }
      NSData* data = CFBridgingRelease(stored);
      const std::string key(static_cast<const char*>(data.bytes), data.length);
      if (realtimeCall) {
        std::ifstream file(argv[3], std::ios::binary);
        const std::string pcm((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (pcm.size() < 3200 || pcm.size() % 2 || pcm.size() > 16000 * 2 * 60) return 2;
        Report("synthetic speech -> read-only function -> spoken continuation", {{"ok", CheckRealtime(workspace, key, pcm)}});
        return failures ? 1 : 0;
      }
      Report("realtime tool configuration", {{"ok", CheckRealtime(workspace, key)}});
      if (realtimeConfig) return failures ? 1 : 0;
      Report("Qwen web search", Voice::SearchWeb({{"query", "阿里云百炼官方文档网址"}, {"limit", 5}}, workspace, key));
      auto color = CGColorSpaceCreateDeviceRGB();
      auto context = CGBitmapContextCreate(nullptr, 300, 200, 8, 1200, color, kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big);
      CGColorSpaceRelease(color);
      CGContextSetRGBFillColor(context, 1, 1, 1, 1); CGContextFillRect(context, CGRectMake(0, 0, 300, 200));
      CGContextSetRGBFillColor(context, 1, 0, 0, 1); CGContextFillEllipseInRect(context, CGRectMake(100, 50, 100, 100));
      auto source = CGBitmapContextCreateImage(context); CGContextRelease(context);
      Voice::DesktopImage image; std::string error;
      const bool encoded = Voice::EncodeDesktopImage(source, image, error); CGImageRelease(source);
      if (!encoded) throw std::runtime_error("synthetic image failed");
      Report("Qwen vision with synthetic image", Voice::DescribeDesktop({{"question", "图片中是什么颜色的什么形状？"}, {"display", 0}}, image, workspace, key));
      return failures ? 1 : 0;
    } catch (const std::exception&) { std::cerr << "integration probe failed; raw service/credential data suppressed\n"; return 1; }
  }
}
