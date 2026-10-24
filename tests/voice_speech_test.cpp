// Exercises the real VoiceSpeech.cpp through dlopen/LoadLibrary, not a copied wrapper.
// One CTest process per loader scenario: LoadSherpa caches its function table.
#include "VoiceSpeech.hpp"
#include "VoiceOnnx.hpp"
#include "voice_speech_mock.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Voice {
// Mock tests do not depend on ONNX Runtime or its installed location.
bool LoadOnnxRuntime(std::string&) { return true; }
std::string OnnxRuntimeLibrary() { return "unused-onnx-runtime"; }
}  // namespace Voice

namespace {
void Check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}
void SetLibrary(const char* path) {
#ifdef _WIN32
  Check(_putenv_s("JPET_SHERPA_ONNX_PATH", path) == 0, "cannot set runtime path");
#else
  Check(setenv("JPET_SHERPA_ONNX_PATH", path, 1) == 0, "cannot set runtime path");
#endif
}
struct Mock {
#ifdef _WIN32
  HMODULE library;
  explicit Mock(const char* path) : library(LoadLibraryW(std::filesystem::u8path(path).c_str())) {}
  void* Symbol(const char* name) { return reinterpret_cast<void*>(GetProcAddress(library, name)); }
  ~Mock() { if (library) FreeLibrary(library); }
#else
  void* library;
  explicit Mock(const char* path) : library(dlopen(path, RTLD_NOW | RTLD_LOCAL)) {}
  void* Symbol(const char* name) { return dlsym(library, name); }
  ~Mock() { if (library) dlclose(library); }
#endif
};

void Rejected(const std::string& directory, const std::string& hotwords, const std::string& reason) {
  std::string first;
  for (int attempt = 0; attempt != 2; ++attempt) {
    std::string error = "stale caller error";
    auto recognizer = Voice::LocalRecognizer::Load(directory, hotwords, error);
    Check(!recognizer, "invalid installation unexpectedly created a recognizer");
    Check(error != "stale caller error" && error.find(reason) != std::string::npos,
          "missing actionable error: " + error);
    if (attempt == 0) first = error;
    else Check(error == first, "cached failure must remain a failure on the second Load");
  }
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    std::cerr << "Usage: voice_speech_test <scenario> <mock library> <bundled hotwords> <fixture directory>\n";
    return 2;
  }
  try {
    const std::string scenario = argv[1], bundled = argv[3];
    const auto fixture = std::filesystem::u8path(argv[4]);
    std::filesystem::create_directories(fixture);
    const auto directory = (fixture / "model directory").u8string();
    SetLibrary(argv[2]);
    if (scenario == "missing_library") {
      Check(!std::filesystem::exists(std::filesystem::u8path(argv[2])), "missing-library fixture unexpectedly exists");
      Rejected(directory, bundled, "重新安装");
    } else {
      Mock mock(argv[2]);
      Check(mock.library != nullptr, "cannot open mock shared library");
      auto configure = reinterpret_cast<VoiceSpeechMockConfigure>(mock.Symbol("JpetSpeechMockConfigure"));
      auto inspect = reinterpret_cast<VoiceSpeechMockInspect>(mock.Symbol("JpetSpeechMockInspect"));
      Check(configure && inspect, "missing mock observation API");
      configure(directory.c_str(), bundled.c_str(), Normal);
      if (scenario == "stock" || scenario == "wrong_version" || scenario == "incomplete") {
        Rejected(directory, bundled, scenario == "incomplete" ? "sherpa-onnx" : "热词");
        Check(inspect()->created == 0, "unsupported/partially bound runtime entered recognizer creation");
        Check(inspect()->capability == (scenario == "stock" ? 0 : 1), "capability lookup is not cached");
      } else if (scenario == "missing_hotwords" || scenario == "empty_hotwords" || scenario == "blank_hotwords") {
        const auto path = fixture / "invalid-hotwords.txt";
        if (scenario == "missing_hotwords") {
          Check(!std::filesystem::exists(path), "missing-hotwords fixture unexpectedly exists");
        } else {
          std::ofstream output(path, std::ios::binary);
          if (scenario == "blank_hotwords") output << " \t\r\n";
          Check(bool(output), "cannot write invalid-hotwords fixture");
        }
        Rejected(directory, path.u8string(), "热词");
        Check(inspect()->created == 0, "missing/empty hotword file reached the runtime");
        // Resource failures must not poison an otherwise compatible loader.
        std::string error;
        auto recovered = Voice::LocalRecognizer::Load(directory, bundled, error);
        Check(recovered && error.empty(), "valid resource should recover after resource failure: " + error);
        recovered.reset();
        Check(inspect()->created == 1 && inspect()->destroyed == 1, "recovery lifetime mismatch");
      } else if (scenario == "create_failure") {
        configure(directory.c_str(), bundled.c_str(), RejectRecognizer);
        Rejected(directory, bundled, "模型");
        Check(inspect()->created == 2 && inspect()->destroyed == 0, "failed creation cleanup mismatch");
      } else {
        Check(scenario == "success", "unknown scenario");
        std::string error = "stale caller error";
        auto recognizer = Voice::LocalRecognizer::Load(directory, bundled, error);
        Check(recognizer && error.empty(), "compatible runtime failed: " + error);
        // Exact signed PCM limits exercise the production s16-to-float conversion.
        const int16_t samples[] = {-32768, -16384, 0, 16384, 32767};
        const std::string pcm(reinterpret_cast<const char*>(samples), sizeof(samples));
        Check(recognizer->Transcribe(pcm) == "轴伊你好呀", "transcription was not returned unchanged");
        Check(recognizer->Transcribe(pcm) == "轴伊你好呀", "second transcription failed");
        for (const int mode : {RejectStream, NullResult, NullText}) {
          configure(directory.c_str(), bundled.c_str(), mode);
          Check(recognizer->Transcribe(pcm).empty(), "failed/empty stream should produce empty text");
        }
        Check(inspect()->streams == 4 && inspect()->streamsDestroyed == 4, "stream cleanup mismatch");
        Check(inspect()->accepted == 4 && inspect()->decoded == 4, "failed stream must not accept/decode audio");
        Check(inspect()->results == 3 && inspect()->resultsDestroyed == 3, "result cleanup mismatch");
        Check(inspect()->destroyed == 0, "recognizer destroyed prematurely");
        recognizer.reset();
        Check(inspect()->destroyed == 1, "recognizer must be destroyed exactly once");
        configure(directory.c_str(), bundled.c_str(), Normal);
        auto second = Voice::LocalRecognizer::Load(directory, bundled, error);
        Check(second && error.empty(), "cached compatible loader cannot create a second recognizer");
        second.reset();
        Check(inspect()->created == 2 && inspect()->destroyed == 2 && inspect()->capability == 1,
              "successful loader cache/lifetime mismatch");
      }
      Check(!inspect()->failure[0], inspect()->failure);
    }
    std::cout << "voice_speech " << scenario << " passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "voice_speech " << argv[1] << " FAILED: " << error.what() << '\n';
    return 1;
  }
}
