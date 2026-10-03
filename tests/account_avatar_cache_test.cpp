#include "AccountAvatarCache.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>

int main() {
  const auto directory = std::filesystem::temp_directory_path() /
      ("jpet-avatar-test-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::string first = "https://i0.hdslb.com/bfs/face/first.jpg";
  const std::string second = "https://i1.hdslb.com/bfs/face/second.png";
  const std::string jpeg = std::string("\xff\xd8\xff", 3) + "first-image";
  const std::string png = std::string("\x89PNG\r\n\x1a\n", 8) + "second-image";
  std::atomic_int downloads{0};
  const auto download = [&](const std::string& url) {
    ++downloads;
    return url == first ? jpeg : png;
  };
  const auto version = [](const std::string& url) {
    return url.substr(url.find("&v=") + 3);
  };

  AccountAvatarCache cache;
  const auto initialUrl = cache.SetAccount("123", first);
  const auto initialVersion = version(initialUrl);
  auto a = std::async(std::launch::async, [&] {
    return cache.Get("123", initialVersion, directory, download);
  });
  auto b = std::async(std::launch::async, [&] {
    return cache.Get("123", initialVersion, directory, download);
  });
  assert(a.get().body == jpeg);
  assert(b.get().contentType == "image/jpeg");
  assert(downloads == 1);

  // A fresh application instance reuses the persistent cache without network.
  AccountAvatarCache restarted;
  assert(restarted.SetAccount("123", "http://i0.hdslb.com/bfs/face/first.jpg") ==
         initialUrl);
  assert(restarted.Get("123", initialVersion, directory, download).body == jpeg);
  assert(downloads == 1);

  // Changing an avatar changes its URL and refreshes the image on disk.
  const auto changedUrl = restarted.SetAccount("123", second);
  assert(changedUrl != initialUrl);
  assert(restarted.Get("123", initialVersion, directory, download).body.empty());
  assert(restarted.Get("123", version(changedUrl), directory, download).body == png);
  assert(downloads == 2);

  // Another account cannot receive the previous account's cached avatar.
  const auto otherUrl = restarted.SetAccount("456", first);
  assert(restarted.Get("123", version(changedUrl), directory, download).body.empty());
  assert(restarted.Get("456", version(otherUrl), directory, download).body == jpeg);
  assert(downloads == 3);
  restarted.ClearAccount();
  assert(restarted.Get("456", version(otherUrl), directory, download).body.empty());

  // Corrupt disk data is repaired. Failed/non-image responses are never cached.
  restarted.SetAccount("123", second);
  std::ofstream(directory / "123.cache", std::ios::binary | std::ios::trunc)
      << second << '\n' << "broken image";
  assert(restarted.Get("123", version(changedUrl), directory,
                       [](const std::string&) { return std::string("<html>error</html>"); })
             .body.empty());
  assert(restarted.Get("123", version(changedUrl), directory, download).body == png);
  assert(downloads == 4);
  const auto failedUrl = restarted.SetAccount("789", first);
  assert(restarted.Get("789", version(failedUrl), directory,
                       [](const std::string&) { return std::string{}; }).body.empty());
  assert(!std::filesystem::exists(directory / "789.cache"));
  assert(restarted.Get("789", version(failedUrl), directory,
                       [&](const std::string&) {
                         return jpeg + std::string(AccountAvatarCache::MaxImageSize, 'x');
                       }).body.empty());
  assert(!std::filesystem::exists(directory / "789.cache"));
  assert(restarted.Get("789", version(failedUrl), directory, download).body == jpeg);

  // Downloaded images can still display when the cache directory is unwritable.
  const auto blocked = directory / "file";
  std::ofstream(blocked) << "not a directory";
  assert(restarted.Get("789", version(failedUrl), blocked, download).body == jpeg);
  assert(restarted.SetAccount("../123", first).empty());
  assert(restarted.SetAccount("123", "https://i0.hdslb.com.evil.test/a").empty());
  assert(restarted.SetAccount("123", "https://localhost/a").empty());
  std::filesystem::remove_all(directory);
  std::cout << "Account avatar cache tests passed\n";
}
