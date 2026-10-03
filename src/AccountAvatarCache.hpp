#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

// Public account images are cached separately from login credentials.
class AccountAvatarCache {
 public:
  static constexpr size_t MaxImageSize = 5 * 1024 * 1024;
  struct Image {
    std::string body;
    std::string contentType;
  };
  using Downloader = std::function<std::string(const std::string&)>;

  std::string SetAccount(const std::string& uid, std::string source) {
    std::lock_guard<std::mutex> lock(mutex_);
    uid_ = uid;
    source_ = NormalizeSource(std::move(source));
    if (!ValidUid(uid_) || source_.empty()) return {};
    return "/api/account/avatar?uid=" + uid_ + "&v=" + Version(source_);
  }

  void ClearAccount() {
    std::lock_guard<std::mutex> lock(mutex_);
    uid_.clear();
    source_.clear();
  }

  Image Get(const std::string& uid, const std::string& version,
            const std::filesystem::path& directory,
            const Downloader& download) {
    // Serialize downloads so concurrent WebViews only fetch an image once.
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ValidUid(uid) || uid != uid_ || source_.empty() ||
        version != Version(source_)) return {};

    const auto path = directory / (uid + ".cache");
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (!error && size <= MaxImageSize + 4096) {
      std::ifstream cached(path, std::ios::binary);
      std::string source;
      std::getline(cached, source);
      if (source == source_) {
        std::string body((std::istreambuf_iterator<char>(cached)), {});
        auto type = ImageType(body);
        if (!type.empty()) return {std::move(body), std::move(type)};
      }
    }

    auto body = download(source_);
    auto type = ImageType(body);
    if (type.empty()) return {};

    std::filesystem::create_directories(directory, error);
    if (!error) {
      const auto temporary = directory / (uid + ".tmp");
      std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      output << source_ << '\n';
      output.write(body.data(), body.size());
      output.close();
      if (output) {
        // Windows rename does not replace an existing file.
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
      }
      std::filesystem::remove(temporary, error);
    }
    // A read-only/full data directory must not prevent displaying the image.
    return {std::move(body), std::move(type)};
  }

 private:
  static bool ValidUid(const std::string& uid) {
    return !uid.empty() && uid != "0" && uid.size() <= 20 &&
           uid.find_first_not_of("0123456789") == std::string::npos;
  }

  static std::string NormalizeSource(std::string source) {
    if (source.compare(0, 2, "//") == 0) source = "https:" + source;
    if (source.compare(0, 7, "http://") == 0) source.replace(0, 7, "https://");
    if (source.compare(0, 8, "https://") != 0 || source.size() > 2048 ||
        source.find_first_of("\r\n") != std::string::npos) return {};
    const auto end = source.find_first_of("/?#", 8);
    const auto host = source.substr(8, end == std::string::npos
                                          ? std::string::npos : end - 8);
    const std::string suffix = ".hdslb.com";
    if (host != "hdslb.com" &&
        !(host.size() > suffix.size() &&
          host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0)) {
      return {};
    }
    if (host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-") !=
        std::string::npos) return {};
    return source;
  }

  static std::string Version(const std::string& source) {
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char c : source) {
      hash ^= c;
      hash *= 1099511628211ULL;
    }
    std::ostringstream result;
    result << std::hex << hash;
    return result.str();
  }

  static std::string ImageType(const std::string& body) {
    if (body.empty() || body.size() > MaxImageSize) return {};
    if (body.size() >= 8 && body.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0)
      return "image/png";
    if (body.size() >= 3 && body.compare(0, 3, "\xff\xd8\xff", 3) == 0)
      return "image/jpeg";
    if (body.compare(0, 6, "GIF87a") == 0 || body.compare(0, 6, "GIF89a") == 0)
      return "image/gif";
    if (body.size() >= 12 && body.compare(0, 4, "RIFF") == 0 &&
        body.compare(8, 4, "WEBP") == 0) return "image/webp";
    return {};
  }

  std::mutex mutex_;
  std::string uid_;
  std::string source_;
};
