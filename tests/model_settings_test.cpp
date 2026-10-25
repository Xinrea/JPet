#include "LAppAllocator.hpp"
#include "ModelSettings.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static void ExpectFailure(const fs::path& path, const char* message) {
  try {
    LoadModelSettings(path.u8string());
    assert(false && "Invalid model settings must be rejected");
  } catch (const std::runtime_error& error) {
    assert(std::string(error.what()).find(message) != std::string::npos);
    assert(std::string(error.what()).find(path.u8string()) != std::string::npos);
  }
}

int main(int argc, char** argv) {
  assert(argc == 3);
  const auto fixture = fs::absolute(fs::u8path(argv[1]));
  const auto root = fs::absolute(fs::u8path(argv[2])) /
      ("model-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  assert(!fs::exists(root));
  fs::create_directories(root);
  const auto originalDirectory = fs::current_path();
  LAppAllocator allocator;
  Csm::CubismFramework::StartUp(&allocator);
  Csm::CubismFramework::Initialize();
  {
    // A failed read must not leave an arbitrary size for Cubism to consume.
    Csm::csmSizeInt size = 12345;
    assert(!LAppPal::LoadFileAsBytes((root / "missing.json").u8string(), &size));
    assert(size == 0);
    ExpectFailure(root / "missing.json", "Cannot read");
    std::ofstream(root / "empty.json").close();
    ExpectFailure(root / "empty.json", "Cannot read");
    std::ofstream(root / "malformed.json") << "{broken";
    ExpectFailure(root / "malformed.json", "Invalid model settings JSON");
    std::ofstream(root / "no-moc.json") << "{}";
    ExpectFailure(root / "no-moc.json", "no Moc file");

    const auto app = root / fs::u8path(u8"安装目录");
    fs::create_directories(app / "resources/joi");
    fs::copy_file(fixture, app / "resources/joi/ZMCW.model3.json", fs::copy_options::overwrite_existing);
    fs::current_path(root);
    ExpectFailure("resources/joi/ZMCW.model3.json", "Cannot read");
    // Windows startup now establishes this same resource base before loading.
    fs::current_path(app);
    auto setting = LoadModelSettings("resources/joi/ZMCW.model3.json");
    assert(std::string(setting->GetModelFileName()) == "ZMCW.moc3");
    auto unicodePathSetting = LoadModelSettings((app / "resources/joi/ZMCW.model3.json").u8string());
    assert(unicodePathSetting->IsValid());
  }
  Csm::CubismFramework::Dispose();
  Csm::CubismFramework::CleanUp();
  fs::current_path(originalDirectory);
  fs::remove_all(root);
  std::cout << "Model settings and resource file tests passed\n";
}
