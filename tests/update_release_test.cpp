#include "UpdateRelease.hpp"
#include <chrono>
#include <iostream>

using Json = nlohmann::json;
void Expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <class Function> void Reject(Function fn) {
  bool rejected = false;
  try { fn(); } catch (const std::exception&) { rejected = true; }
  Expect(rejected, "Invalid release/update was accepted");
}
Json Fixture(const std::string& tag = "v3.0.0") {
  const auto version = Updates::Version(tag);
  Json assets = Json::array();
  for (auto platform : {"windows-x64", "macos-arm64"}) {
    const auto name = "jpet-" + version + "-" + platform + ".zip";
    assets.push_back({{"name", name}, {"state", "uploaded"}, {"size", 3},
      {"browser_download_url", "https://github.com/Xinrea/JPet/releases/download/" + tag + "/" + name},
      {"digest", "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"}});
  }
  return {{"tag_name", tag}, {"draft", false}, {"prerelease", false}, {"body", "Release notes"}, {"assets", assets}};
}
int main() {
  const auto fixture = Fixture();
  auto release = Updates::ParseRelease(fixture, "2.2.5", "macos-arm64");
  Expect(release.newer && release.assetName == "jpet-3.0.0-macos-arm64.zip", "Wrong ARM asset");
  Expect(Updates::ParseRelease(fixture, "2.2.5", "windows-x64").assetName == "jpet-3.0.0-windows-x64.zip", "Wrong Windows asset");
  Expect(!Updates::ParseRelease(fixture, "3.0.0", "windows-x64").newer, "Equal version updated");
  Expect(!Updates::ParseRelease(fixture, "3.1.0", "windows-x64").newer, "Downgrade accepted");
  Expect(Updates::ParseRelease(Fixture("3.10.0"), "3.9.0", "macos-arm64").newer, "Versions sorted lexically");
  Expect(Updates::ParseRelease(fixture, "3.0.0-rc.1", "macos-arm64").newer, "Stable release did not supersede RC");
  Expect(Updates::ParseRelease(fixture, "2.2.5", "linux-x64").downloadURL.empty(), "Wrong platform selected");
  auto invalid = fixture;
  invalid["draft"] = true;
  Reject([&] { Updates::ParseRelease(invalid, "2.2.5", "macos-arm64"); });
  invalid = fixture; invalid["prerelease"] = true;
  Reject([&] { Updates::ParseRelease(invalid, "2.2.5", "macos-arm64"); });
  invalid = fixture; invalid["tag_name"] = "../../../evil";
  Reject([&] { Updates::ParseRelease(invalid, "2.2.5", "macos-arm64"); });
  invalid = fixture; invalid["assets"][1]["browser_download_url"] = "https://example.com/JPet.zip";
  Reject([&] { Updates::ParseRelease(invalid, "2.2.5", "macos-arm64"); });
  invalid = fixture; invalid["assets"][1]["size"] = -1;
  Reject([&] { Updates::ParseRelease(invalid, "2.2.5", "macos-arm64"); });
  invalid = fixture; invalid["assets"][1]["digest"] = nullptr;
  Expect(Updates::ParseRelease(invalid, "2.2.5", "macos-arm64").sha256.empty(), "Missing checksum accepted");
  invalid = fixture; invalid["assets"][1]["digest"] = "sha256:invalid";
  Expect(Updates::ParseRelease(invalid, "2.2.5", "macos-arm64").sha256.empty(), "Bad checksum accepted");
  invalid = fixture; invalid["body"] = nullptr;
  Expect(Updates::ParseRelease(invalid, "2.2.5", "macos-arm64").notes.empty(), "Null notes rejected");

  const auto path = std::filesystem::temp_directory_path() /
      ("jpet-release-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::ofstream(path, std::ios::binary) << "abc";
  Updates::Verify(path, release);
  std::ofstream(path, std::ios::binary) << "abd";
  Reject([&] { Updates::Verify(path, release); });
  std::ofstream(path, std::ios::binary) << "ab";
  Reject([&] { Updates::Verify(path, release); });
  std::filesystem::remove(path);
  std::cout << "Release selection, semantic versions and tamper checks passed\n";
}
