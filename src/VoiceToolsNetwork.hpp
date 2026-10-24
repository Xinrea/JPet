#pragma once
#include "VoiceDesktop.hpp"
#include <nlohmann/json.hpp>

namespace Voice {
// Shared by the game runtime and opt-in read-only integration probes.
nlohmann::json DescribeDesktop(const nlohmann::json& query, const DesktopImage& image,
    const std::string& workspace, const std::string& apiKey);
nlohmann::json SearchWeb(const nlohmann::json& query, const std::string& workspace, const std::string& apiKey);
// Runs a validated `bilibili` tool request; JPet's cookies only reach Bilibili API hosts.
nlohmann::json QueryBilibili(const nlohmann::json& request, const std::string& cookies, const std::string& uid);
} // namespace Voice
