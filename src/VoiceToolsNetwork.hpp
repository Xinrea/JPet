#pragma once
#include <nlohmann/json.hpp>

namespace Voice {
// Shared by the game runtime and opt-in read-only integration probes.
// Runs a validated `bilibili` tool request; JPet's cookies only reach Bilibili API hosts.
nlohmann::json QueryBilibili(const nlohmann::json& request, const std::string& cookies, const std::string& uid);
} // namespace Voice
