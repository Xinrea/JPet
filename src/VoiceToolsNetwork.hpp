#pragma once
#include "VoiceDesktop.hpp"
#include <nlohmann/json.hpp>

namespace Voice {
// Shared by the game runtime and opt-in read-only integration probes.
nlohmann::json DescribeDesktop(const nlohmann::json& query, const DesktopImage& image,
    const std::string& workspace, const std::string& apiKey);
nlohmann::json SearchWeb(const nlohmann::json& query, const std::string& workspace, const std::string& apiKey);
nlohmann::json SearchBilibili(const nlohmann::json& query, const std::string& cookies, const std::string& uid);
nlohmann::json ReadGameRank(const nlohmann::json& query, const std::string& serviceUrl, const std::string& uid);
} // namespace Voice
