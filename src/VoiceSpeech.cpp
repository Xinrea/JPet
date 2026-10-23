#include "VoiceSpeech.hpp"
#include "VoiceOnnx.hpp"

#include <sherpa-onnx/c-api/c-api.h>

#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Voice {
namespace {
// Function table resolved from the shared library; the header only provides types.
struct Sherpa {
  decltype(&SherpaOnnxCreateOfflineRecognizer) createRecognizer = nullptr;
  decltype(&SherpaOnnxDestroyOfflineRecognizer) destroyRecognizer = nullptr;
  decltype(&SherpaOnnxCreateOfflineStream) createStream = nullptr;
  decltype(&SherpaOnnxDestroyOfflineStream) destroyStream = nullptr;
  decltype(&SherpaOnnxAcceptWaveformOffline) acceptWaveform = nullptr;
  decltype(&SherpaOnnxDecodeOfflineStream) decode = nullptr;
  decltype(&SherpaOnnxGetOfflineStreamResult) result = nullptr;
  decltype(&SherpaOnnxDestroyOfflineRecognizerResult) destroyResult = nullptr;
  std::string error;
};

const Sherpa& LoadSherpa() {
  static Sherpa sherpa;
  static bool loaded = [] {
    if (!LoadOnnxRuntime(sherpa.error)) return false;
    const auto path = SherpaOnnxLibrary();
#ifdef _WIN32
    const int length = MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0);
    std::wstring wide(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), wide.data(), length);
    HMODULE library = LoadLibraryExW(wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    const auto symbol = [&](const char* name) { return library ? reinterpret_cast<void*>(GetProcAddress(library, name)) : nullptr; };
#else
    void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    const auto symbol = [&](const char* name) { return library ? dlsym(library, name) : nullptr; };
#endif
    const auto bind = [&](auto& function, const char* name) {
      function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(symbol(name));
      return function != nullptr;
    };
    if (!bind(sherpa.createRecognizer, "SherpaOnnxCreateOfflineRecognizer") ||
        !bind(sherpa.destroyRecognizer, "SherpaOnnxDestroyOfflineRecognizer") ||
        !bind(sherpa.createStream, "SherpaOnnxCreateOfflineStream") ||
        !bind(sherpa.destroyStream, "SherpaOnnxDestroyOfflineStream") ||
        !bind(sherpa.acceptWaveform, "SherpaOnnxAcceptWaveformOffline") ||
        !bind(sherpa.decode, "SherpaOnnxDecodeOfflineStream") ||
        !bind(sherpa.result, "SherpaOnnxGetOfflineStreamResult") ||
        !bind(sherpa.destroyResult, "SherpaOnnxDestroyOfflineRecognizerResult")) {
      sherpa.error = "无法加载本地语音识别组件 sherpa-onnx，请重新安装 JPet";
      return false;
    }
    return true;
  }();
  (void)loaded;
  return sherpa;
}
}  // namespace

std::string SherpaOnnxLibrary() {
  if (const char* path = std::getenv("JPET_SHERPA_ONNX_PATH"); path && *path) return path;
  auto directory = OnnxRuntimeLibrary();
  directory.erase(directory.find_last_of("/\\") + 1);
#ifdef _WIN32
  return directory + "sherpa-onnx-c-api.dll";
#else
  return directory + "libsherpa-onnx-c-api.dylib";
#endif
}

struct LocalRecognizer::Impl {
  const Sherpa* sherpa = nullptr;
  const SherpaOnnxOfflineRecognizer* recognizer = nullptr;
  ~Impl() { if (recognizer) sherpa->destroyRecognizer(recognizer); }
};

LocalRecognizer::LocalRecognizer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
LocalRecognizer::~LocalRecognizer() = default;

std::unique_ptr<LocalRecognizer> LocalRecognizer::Load(const std::string& directory, std::string& error) {
  const auto& sherpa = LoadSherpa();
  if (!sherpa.createRecognizer) { error = sherpa.error; return nullptr; }
  const auto model = directory + "/model.int8.onnx", tokens = directory + "/tokens.txt";
  SherpaOnnxOfflineRecognizerConfig config;
  std::memset(&config, 0, sizeof(config));
  config.feat_config.sample_rate = 16000;
  config.feat_config.feature_dim = 80;
  config.model_config.sense_voice.model = model.c_str();
  config.model_config.sense_voice.language = "auto";  // Mandarin, Cantonese, English, Japanese, Korean.
  config.model_config.sense_voice.use_itn = 1;        // Punctuation and digits.
  config.model_config.tokens = tokens.c_str();
  config.model_config.num_threads = 2;
  config.model_config.provider = "cpu";
  config.decoding_method = "greedy_search";
  auto impl = std::make_unique<Impl>();
  impl->sherpa = &sherpa;
  impl->recognizer = sherpa.createRecognizer(&config);
  if (!impl->recognizer) { error = "无法加载本地语音识别模型，请重新下载"; return nullptr; }
  return std::unique_ptr<LocalRecognizer>(new LocalRecognizer(std::move(impl)));
}

std::string LocalRecognizer::Transcribe(const std::string& pcm) {
  const auto& sherpa = *impl_->sherpa;
  std::vector<float> samples(pcm.size() / 2);
  for (size_t i = 0; i < samples.size(); ++i) {
    int16_t sample;
    std::memcpy(&sample, pcm.data() + 2 * i, sizeof(sample));
    samples[i] = sample / 32768.0f;
  }
  const auto* stream = sherpa.createStream(impl_->recognizer);
  if (!stream) return {};
  sherpa.acceptWaveform(stream, 16000, samples.data(), static_cast<int32_t>(samples.size()));
  sherpa.decode(impl_->recognizer, stream);
  std::string text;
  if (const auto* result = sherpa.result(stream)) {
    if (result->text) text = result->text;
    sherpa.destroyResult(result);
  }
  sherpa.destroyStream(stream);
  return text;
}
}  // namespace Voice
