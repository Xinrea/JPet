#pragma once

#include <filesystem>
#include <cctype>
#include <fstream>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <semver.hpp>

namespace Updates {
inline constexpr const char* Repository = "Xinrea/JPet";
inline constexpr const char* ReleasesURL = "https://github.com/Xinrea/JPet/releases";
inline constexpr const char* LatestURL = "https://api.github.com/repos/Xinrea/JPet/releases/latest";

struct Release {
  std::string version, tag, notes, pageURL, assetName, downloadURL, sha256;
  uint64_t size = 0;
  bool newer = false;
};

inline std::string PlatformName() {
#ifdef _WIN32
  return "windows-x64";
#else
  return "macos-arm64";
#endif
}

inline std::string Version(const std::string& tag) {
  auto value = tag;
  if (!value.empty() && value.front() == 'v') value.erase(0, 1);
  // Bound and restrict tag text before using it in asset names or URLs.
  if (value.size() > 128 || !std::regex_match(value, std::regex(R"([0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?)")))
    throw std::runtime_error("Release 的版本号无效");
  return semver::version{value}.to_string();
}

inline Release ParseRelease(const nlohmann::json& json, const std::string& current,
                            const std::string& platform) {
  if (json.value("draft", false) || json.value("prerelease", false))
    throw std::runtime_error("Release 尚未正式发布");
  Release release;
  release.tag = json.at("tag_name").get<std::string>();
  release.version = Version(release.tag);
  if (semver::version{release.version}.prerelease_type != semver::prerelease::none)
    throw std::runtime_error("自动更新仅使用正式版本");
  release.newer = semver::version{Version(current)} < semver::version{release.version};
  release.pageURL = std::string(ReleasesURL) + "/tag/" + release.tag;
  if (json.contains("body") && json["body"].is_string()) release.notes = json["body"].get<std::string>();
  release.assetName = "jpet-" + release.version + "-" + platform + ".zip";
  for (const auto& asset : json.at("assets")) {
    if (asset.value("name", "") != release.assetName || asset.value("state", "") != "uploaded") continue;
    release.downloadURL = asset.at("browser_download_url").get<std::string>();
    const auto prefix = std::string("https://github.com/") + Repository + "/releases/download/";
    if (release.downloadURL.compare(0, prefix.size(), prefix) != 0 ||
        release.downloadURL.find("/../") != std::string::npos ||
        release.downloadURL.substr(release.downloadURL.find_last_of('/') + 1) != release.assetName)
      throw std::runtime_error("Release 的下载地址无效");
    release.size = asset.at("size").get<uint64_t>();
    if (!release.size || release.size > uint64_t{2} * 1024 * 1024 * 1024)
      throw std::runtime_error("更新包大小无效");
    if (asset.contains("digest") && asset["digest"].is_string()) {
      const auto digest = asset["digest"].get<std::string>();
      if (std::regex_match(digest, std::regex("sha256:[0-9a-fA-F]{64}"))) {
        release.sha256 = digest.substr(7);
        for (auto& c : release.sha256) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
    }
    break;
  }
  return release;
}

inline std::string SHA256(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("无法读取更新包");
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> hash(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!hash || EVP_DigestInit_ex(hash.get(), EVP_sha256(), nullptr) != 1)
    throw std::runtime_error("无法初始化更新校验");
  char bytes[65536];
  while (stream.read(bytes, sizeof(bytes)) || stream.gcount()) {
    if (EVP_DigestUpdate(hash.get(), bytes, static_cast<size_t>(stream.gcount())) != 1)
      throw std::runtime_error("更新包校验失败");
  }
  if (!stream.eof()) throw std::runtime_error("读取更新包失败");
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  if (EVP_DigestFinal_ex(hash.get(), digest, &length) != 1)
    throw std::runtime_error("更新包校验失败");
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (unsigned int i = 0; i < length; ++i) { result += hex[digest[i] >> 4]; result += hex[digest[i] & 15]; }
  return result;
}

inline void Verify(const std::filesystem::path& path, const Release& release) {
  if (release.sha256.empty() || std::filesystem::file_size(path) != release.size || SHA256(path) != release.sha256)
    throw std::runtime_error("更新包校验失败，请重新下载");
}
}
