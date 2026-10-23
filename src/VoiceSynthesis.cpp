#include "VoiceSynthesis.hpp"
#include "VoiceCommon.hpp"
#include "VoiceOnnx.hpp"
#include "VoiceText.hpp"

#include <qwentts/qwen.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Voice {
namespace {
using Clock = std::chrono::steady_clock;
constexpr size_t SampleRate = 24000;
constexpr float VoicedPeak = 0.02f;             // The model's silence stays below 0.01.
constexpr size_t TrimFrame = SampleRate / 100;  // 10 ms.
constexpr size_t LeadIn = SampleRate / 10;      // Kept before the first voiced frame.
constexpr size_t Tail = SampleRate / 5;         // Kept after the last one.
constexpr const char* Format = "jpet-voice/1";

long long Milliseconds(Clock::time_point from, Clock::time_point to) {
  return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count());
}

// Function table resolved from the shared library; the header only provides types.
struct Qwen {
  decltype(&qt_init_default_params) initDefaults = nullptr;
  decltype(&qt_init) init = nullptr;
  decltype(&qt_free) free = nullptr;
  decltype(&qt_tts_default_params) ttsDefaults = nullptr;
  decltype(&qt_synthesize) synthesize = nullptr;
  decltype(&qt_audio_free) audioFree = nullptr;
  decltype(&qt_extract_voice_ref) extract = nullptr;
  decltype(&qt_voice_ref_free) referenceFree = nullptr;
  decltype(&qt_last_error) lastError = nullptr;
  decltype(&qt_log_set) logSet = nullptr;
  std::string error;
};

// qwentts logs globally; warnings and errors go to the active synthesizer's log.
std::mutex logMutex;
std::function<void(const std::string&)> logSink;

void ForwardLog(qt_log_level level, const char* message, void*) {
  if (level < QT_LOG_WARN) return;  // Load and per-sentence timings would flood jpet.log.
  std::lock_guard<std::mutex> lock(logMutex);
  if (logSink) logSink(std::string("[Voice] qwentts ") + message);
}

void SetLogSink(std::function<void(const std::string&)> sink) {
  std::lock_guard<std::mutex> lock(logMutex);
  logSink = std::move(sink);
}

const Qwen& LoadQwen() {
  static Qwen qwen;
  static std::once_flag once;
  std::call_once(once, [] {
#ifdef __APPLE__
    // GGML's Metal backend uses Metal API introduced in macOS 12.
    if (__builtin_available(macOS 12.0, *)) {
    } else {
      qwen.error = "本机语音合成需要 macOS 12 或更新版本";
      return;
    }
#endif
    const auto path = QwenTtsLibrary();
#ifdef _WIN32
    const int length = MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0);
    std::wstring wide(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), wide.data(), length);
    // GGML's backend modules sit next to the library and JPet.exe.
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
    if (!bind(qwen.initDefaults, "qt_init_default_params") || !bind(qwen.init, "qt_init") ||
        !bind(qwen.free, "qt_free") || !bind(qwen.ttsDefaults, "qt_tts_default_params") ||
        !bind(qwen.synthesize, "qt_synthesize") || !bind(qwen.audioFree, "qt_audio_free") ||
        !bind(qwen.extract, "qt_extract_voice_ref") || !bind(qwen.referenceFree, "qt_voice_ref_free") ||
        !bind(qwen.lastError, "qt_last_error") || !bind(qwen.logSet, "qt_log_set")) {
      qwen.init = nullptr;
      qwen.error = "无法加载本机语音合成组件 qwentts，请重新安装 JPet";
      return;
    }
    qwen.logSet(ForwardLog, nullptr);
  });
  return qwen;
}

qt_context* OpenModel(const Qwen& qwen, const SynthesisModel& model, std::string& error) {
  qt_init_params params;
  qwen.initDefaults(&params);
  params.talker_path = model.talker.c_str();
  params.codec_path = model.codec.c_str();
  auto* context = qwen.init(&params);
  if (!context) error = "无法加载本机语音合成模型，请重新下载语音模型";
  return context;
}

std::string ToPcm(const float* samples, size_t count) {
  std::string pcm(count * 2, '\0');
  for (size_t i = 0; i < count; ++i) {
    const auto value = static_cast<int16_t>(std::lround(std::clamp(samples[i], -1.0f, 1.0f) * 32767.0f));
    std::memcpy(&pcm[2 * i], &value, sizeof(value));
  }
  return pcm;
}

std::string Bytes(const void* data, size_t size) { return std::string(static_cast<const char*>(data), size); }

class LocalSynthesizer final : public Synthesizer {
 public:
  LocalSynthesizer(SynthesisModel model, VoiceReference voice, std::function<void(const std::string&)> log)
      : model_(std::move(model)), voice_(std::move(voice)), log_(std::move(log)) {
    worker_ = std::thread([this] { Run(); });
  }
  ~LocalSynthesizer() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
      ++generation_;
    }
    wake_.notify_all();
    worker_.join();  // A sentence stops within a frame; a model load runs to its end.
  }
  void Speak(uint64_t id, const std::string& text) override {
    { std::lock_guard<std::mutex> lock(mutex_); jobs_.push_back({id, text}); }
    wake_.notify_all();
  }
  void Cancel() override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++generation_;
    jobs_.clear();
    outputs_.clear();
  }
  std::vector<Output> Poll() override {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Output> outputs(std::make_move_iterator(outputs_.begin()), std::make_move_iterator(outputs_.end()));
    outputs_.clear();
    return outputs;
  }

 private:
  struct Job { uint64_t id; std::string text; };
  // One sentence, shared with qwentts' callbacks on its compute thread.
  struct Sentence {
    LocalSynthesizer* self;
    uint64_t id, generation;
    bool warmup;
    SpeechTrimmer trimmer;
    Clock::time_point start, first;
    bool heard = false;
  };

  void Log(const std::string& line) { if (log_) log_("[Voice] " + line); }

  void Emit(uint64_t generation, Output output) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation == generation_) outputs_.push_back(std::move(output));
  }

  void Run() {
    const auto& qwen = LoadQwen();
    std::string error = qwen.error;
    qt_context* context = nullptr;
    if (qwen.init) {
      SetLogSink(log_);
      const auto start = Clock::now();
      context = OpenModel(qwen, model_, error);
      const auto loaded = Clock::now();
      // The first synthesis builds the GPU pipelines; pay for it before the first reply.
      if (context) Synthesize(qwen, context, {0, "嗯。"}, generation_, true);
      Log("Synthesis model load ok=" + std::to_string(context ? 1 : 0) + " ms=" + std::to_string(Milliseconds(start, loaded)) +
          " warmup_ms=" + std::to_string(Milliseconds(loaded, Clock::now())));
    } else {
      Log("Synthesis library unavailable");
    }
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      wake_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
      if (stopping_) break;
      auto job = std::move(jobs_.front());
      jobs_.pop_front();
      const uint64_t generation = generation_;
      lock.unlock();
      if (context) Synthesize(qwen, context, job, generation, false);
      else Emit(generation, {job.id, {}, true, error});
      lock.lock();
    }
    lock.unlock();
    if (context) qwen.free(context);
    SetLogSink(nullptr);
  }

  void Synthesize(const Qwen& qwen, qt_context* context, const Job& job, uint64_t generation, bool warmup) {
    Sentence sentence{this, job.id, generation, warmup, {}, Clock::now(), {}};
    const auto text = SpokenNumbers(job.text);
    qt_tts_params params;
    qwen.ttsDefaults(&params);
    params.text = text.c_str();
    params.lang = "chinese";
    params.max_new_tokens = MaxSynthesisFrames(text);
    params.ref_spk_emb = voice_.speaker.data();
    params.ref_spk_dim = static_cast<int>(voice_.speaker.size());
    params.ref_codes = voice_.codes.data();
    params.ref_T = voice_.frames;
    params.ref_text = voice_.text.c_str();
    params.cancel = [](void* user) {
      const auto& s = *static_cast<Sentence*>(user);
      return s.self->generation_ != s.generation;
    };
    params.cancel_user_data = &sentence;
    params.on_chunk = [](const float* samples, int count, void* user) {
      auto& s = *static_cast<Sentence*>(user);
      if (s.self->generation_ != s.generation) return false;
      auto pcm = s.trimmer.Push(samples, static_cast<size_t>((std::max)(count, 0)));
      if (pcm.empty() || s.warmup) return true;
      if (!s.heard) { s.heard = true; s.first = Clock::now(); }
      s.self->Emit(s.generation, {s.id, std::move(pcm)});
      return true;
    };
    params.on_chunk_user_data = &sentence;
    qt_audio audio = {};
    const auto status = qwen.synthesize(context, &params, &audio);
    qwen.audioFree(&audio);
    if (warmup) return;
    Output last{job.id, sentence.trimmer.Finish(), true};
    if (status == QT_STATUS_OK) {
      Log("Synthesis sentence first_audio_ms=" + std::to_string(sentence.heard ? Milliseconds(sentence.start, sentence.first) : -1) +
          " ms=" + std::to_string(Milliseconds(sentence.start, Clock::now())));
    } else if (status != QT_STATUS_CANCELLED) {
      Log("Synthesis failed status=" + std::to_string(static_cast<int>(status)) + " " + qwen.lastError());
      last.error = "轴伊的声音暂时无法合成，请稍后重试";
    }
    Emit(generation, std::move(last));
  }

  SynthesisModel model_;
  VoiceReference voice_;
  std::function<void(const std::string&)> log_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Job> jobs_;
  std::vector<Output> outputs_;
  std::atomic<uint64_t> generation_{0};
  bool stopping_ = false;
  std::thread worker_;
};
}  // namespace

std::string QwenTtsLibrary() {
  if (const char* path = std::getenv("JPET_QWENTTS_PATH"); path && *path) return path;
  auto directory = OnnxRuntimeLibrary();
  directory.erase(directory.find_last_of("/\\") + 1);
#ifdef _WIN32
  return directory + "qwen.dll";
#else
  return directory + "libqwen.dylib";
#endif
}

bool LocalSynthesisAvailable(std::string& error) {
  const auto& qwen = LoadQwen();
  if (!qwen.init) error = qwen.error;
  return qwen.init != nullptr;
}

std::unique_ptr<Synthesizer> MakeLocalSynthesizer(SynthesisModel model, VoiceReference voice,
                                                   std::function<void(const std::string&)> log) {
  return std::make_unique<LocalSynthesizer>(std::move(model), std::move(voice), std::move(log));
}

bool ExtractVoiceReference(const SynthesisModel& model, const std::vector<float>& audio, const std::string& text,
                           VoiceReference& voice, std::string& error) {
  const auto& qwen = LoadQwen();
  if (!qwen.init) { error = qwen.error; return false; }
  auto* context = OpenModel(qwen, model, error);
  if (!context) return false;
  qt_voice_ref reference = {};
  const bool ok = qwen.extract(context, audio.data(), static_cast<int>(audio.size()), &reference) == QT_STATUS_OK;
  if (ok) {
    voice.speaker.assign(reference.ref_spk_emb, reference.ref_spk_emb + reference.ref_spk_dim);
    voice.codebooks = reference.num_codebooks;
    voice.frames = reference.ref_T;
    voice.codes.assign(reference.ref_codes, reference.ref_codes + size_t(reference.num_codebooks) * reference.ref_T);
    voice.text = text;
  } else {
    error = qwen.lastError();
  }
  qwen.referenceFree(&reference);
  qwen.free(context);
  return ok;
}

bool LoadVoiceReference(const std::string& path, VoiceReference& voice, std::string& error) {
  std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
  const auto json = nlohmann::json::parse(file, nullptr, false);
  error = "JPet 安装不完整：找不到轴伊的声音文件，请重新安装";
  if (!json.is_object() || json.value("format", std::string{}) != Format) return false;
  try {
    const auto speaker = DecodeBase64(json.at("speaker_embedding").get<std::string>());
    const auto codes = DecodeBase64(json.at("codes").get<std::string>());
    VoiceReference loaded;
    loaded.codebooks = json.at("codebooks").get<int>();
    loaded.frames = json.at("frames").get<int>();
    loaded.text = json.at("text").get<std::string>();
    if (speaker.empty() || speaker.size() % sizeof(float) || loaded.codebooks <= 0 || loaded.frames <= 0 ||
        codes.size() != size_t(loaded.codebooks) * size_t(loaded.frames) * sizeof(int32_t) || loaded.text.empty())
      return false;
    loaded.speaker.resize(speaker.size() / sizeof(float));
    std::memcpy(loaded.speaker.data(), speaker.data(), speaker.size());
    loaded.codes.resize(codes.size() / sizeof(int32_t));
    std::memcpy(loaded.codes.data(), codes.data(), codes.size());
    voice = std::move(loaded);
  } catch (const std::exception&) {
    return false;
  }
  error.clear();
  return true;
}

std::string VoiceReferenceJson(const VoiceReference& voice, const std::string& source) {
  return nlohmann::json{
      {"format", Format},
      {"model", "Qwen3-TTS-12Hz-0.6B-Base"},
      {"source", source},
      {"text", voice.text},
      {"speaker_embedding", EncodeBase64(Bytes(voice.speaker.data(), voice.speaker.size() * sizeof(float)))},
      {"codebooks", voice.codebooks},
      {"frames", voice.frames},
      {"codes", EncodeBase64(Bytes(voice.codes.data(), voice.codes.size() * sizeof(int32_t)))}}
      .dump(2) + "\n";
}

int MaxSynthesisFrames(const std::string& text) {
  // 0.4 s per character is about twice a slow speaking rate; 12.5 frames per second.
  const double seconds = 0.4 * double(CodePoints(text)) + 2.0;
  return std::clamp(static_cast<int>(seconds * 12.5), 40, 1024);
}

std::string SpeechTrimmer::Push(const float* samples, size_t count) {
  pending_.insert(pending_.end(), samples, samples + count);
  size_t first = 0, end = 0;  // First voiced frame and the end of the last one.
  for (size_t at = 0; at < pending_.size(); at += TrimFrame) {
    const size_t stop = (std::min)(pending_.size(), at + TrimFrame);
    float peak = 0;
    for (size_t i = at; i < stop; ++i) peak = (std::max)(peak, std::fabs(pending_[i]));
    if (peak < VoicedPeak) continue;
    if (end == 0) first = at;
    end = stop;
  }
  if (end == 0) {
    // Before speech starts only the lead-in can still be played.
    if (!voiced_ && pending_.size() > LeadIn) pending_.erase(pending_.begin(), pending_.end() - LeadIn);
    return {};
  }
  const size_t from = voiced_ || first < LeadIn ? 0 : first - LeadIn;
  voiced_ = true;
  auto pcm = ToPcm(pending_.data() + from, end - from);
  pending_.erase(pending_.begin(), pending_.begin() + end);
  return pcm;
}

std::string SpeechTrimmer::Finish() {
  auto pcm = voiced_ ? ToPcm(pending_.data(), (std::min)(pending_.size(), Tail)) : std::string{};
  pending_.clear();
  voiced_ = false;
  return pcm;
}
}  // namespace Voice
