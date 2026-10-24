#pragma once

// The aggregated, read-only `bilibili(action=...)` voice tool.
//
// This layer owns the schema, argument validation, request sequencing (WBI
// signing, login checks, multi-step lookups) and response shaping. It never sees
// cookies: HTTP goes through `BilibiliGet`, whose runtime adapter in
// VoiceToolsNetwork.cpp decides which hosts receive credentials. Tests inject a
// fake `BilibiliGet` to exercise every action offline.
#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>
#include <string>

namespace Voice {
struct BilibiliReply {
  nlohmann::json body;  // Parsed JSON object; unset when `error` is set.
  int status = 0;
  std::string error;    // Transport failure, already readable.
};
// GET an absolute HTTPS URL with the given Referer.
using BilibiliGet = std::function<BilibiliReply(const std::string& url, const std::string& referer)>;

nlohmann::json BilibiliToolDefinition();
// Validates model arguments into a normalized request; throws std::invalid_argument.
nlohmann::json BilibiliRequest(const nlohmann::json& args);
// Runs a normalized request. `uid` is the account JPet is logged in with (empty
// when logged out); account actions require the cookies to belong to it.
nlohmann::json RunBilibili(const nlohmann::json& request, const std::string& uid,
    const BilibiliGet& get, int64_t now);
// Shapes a /x/web-interface/wbi/search/type response.
nlohmann::json BilibiliSearchResults(const nlohmann::json& response, const std::string& kind, int limit);
}  // namespace Voice
