#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include <string>

namespace Voice {
// Shared by the game runtime and opt-in read-only integration probes.
// Queries DuckDuckGo's HTML endpoint without credentials or destination fetches.
nlohmann::json QueryWebSearch(const nlohmann::json& request, const std::function<bool()>& cancelled = {});
// Runs a validated `bilibili` tool request; JPet's cookies only reach Bilibili API hosts.
nlohmann::json QueryBilibili(const nlohmann::json& request, const std::string& cookies, const std::string& uid);
} // namespace Voice
