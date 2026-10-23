#include "VoiceModels.hpp"

#include <cpr/cpr.h>
#include <openssl/evp.h>

#include <filesystem>
#include <fstream>
#include <memory>

namespace Voice {
namespace fs = std::filesystem;

namespace {
constexpr const char* Marker = "verified";

std::string Hex(const unsigned char* bytes, unsigned length) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (unsigned i = 0; i < length; ++i) { out += digits[bytes[i] >> 4]; out += digits[bytes[i] & 15]; }
  return out;
}

struct Sha256 {
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context{EVP_MD_CTX_new(), &EVP_MD_CTX_free};
  Sha256() { EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr); }
  void Update(const char* data, size_t size) { EVP_DigestUpdate(context.get(), data, size); }
  std::string Finish() {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned length = 0;
    EVP_DigestFinal_ex(context.get(), digest, &length);
    return Hex(digest, length);
  }
};

bool Verify(const fs::path& path, const SpeechModelStore::File& file) {
  std::error_code error;
  if (fs::file_size(path, error) != file.size || error) return false;
  std::ifstream input(path, std::ios::binary);
  Sha256 hash;
  std::vector<char> buffer(1 << 20);
  while (input.read(buffer.data(), buffer.size()) || input.gcount()) hash.Update(buffer.data(), size_t(input.gcount()));
  return hash.Finish() == file.sha256;
}
}  // namespace

const std::vector<SpeechModelStore::File>& SpeechModelStore::Files() {
  // sherpa-onnx's SenseVoice-Small int8 export (2025-09-09).
  static const std::vector<File> files = {
    {"model.int8.onnx", 237115547, "12ca1a2ae7ecf3e0019ef2822307ee0b5cadc9196569e379b4c4026f8205276d"},
    {"tokens.txt", 315894, "f449eb28dc567533d7fa59be34e2abca8784f771850c78a47fb731a31429a1dc"},
  };
  return files;
}

std::vector<std::string> SpeechModelStore::DefaultMirrors() {
  // Prototype sources; a production build should serve these from JPet's own CDN.
  const std::string path = "/csukuangfj/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2025-09-09/resolve/main/";
  return {"https://huggingface.co" + path, "https://hf-mirror.com" + path};
}

const std::vector<SpeechModelStore::File>& SpeechModelStore::SynthesisFiles() {
  // qwentts.cpp GGUF conversions of Qwen3-TTS-12Hz-0.6B-Base, Q8_0.
  static const std::vector<File> files = {
    {"qwen-talker-0.6b-base-Q8_0.gguf", 992615488, "d54dbaf10591421fa764ed630d764efa717ae40cd959bd48c66d4eb1af226426"},
    {"qwen-tokenizer-12hz-Q8_0.gguf", 291150624, "1883beeed99348fc35e23dd225e9082f93f6f8c109330a33d935baa8acdbfd94"},
  };
  return files;
}

std::vector<std::string> SpeechModelStore::SynthesisMirrors() {
  // Pinned to the revision the bundled qwentts library reads.
  const std::string path = "/Serveurperso/Qwen3-TTS-GGUF/resolve/b7ee2e8c7459c3bea99da23e3d178125a7d1713c/";
  return {"https://huggingface.co" + path, "https://hf-mirror.com" + path};
}

nlohmann::json SpeechModelStore::Combine(const std::vector<nlohmann::json>& statuses) {
  uint64_t received = 0, total = 0;
  std::string state = "ready", error;
  for (const auto& status : statuses) {
    received += status.value("received", uint64_t{0});
    total += status.value("total", uint64_t{0});
    const auto part = status.value("state", std::string{});
    if (part == "failed" && error.empty()) error = status.value("error", std::string{});
    // Precedence: downloading, failed, missing, ready.
    for (const auto* rank : {"downloading", "failed", "missing"}) {
      if (state == rank) break;
      if (part == rank) { state = rank; break; }
    }
  }
  return {{"state", state}, {"received", received}, {"total", total}, {"error", error}};
}

SpeechModelStore::SpeechModelStore(std::string directory, std::vector<std::string> mirrors, std::vector<File> files)
    : directory_(std::move(directory)), mirrors_(std::move(mirrors)), files_(std::move(files)) {
  if (!Installed().empty()) state_ = "ready";
}

SpeechModelStore::~SpeechModelStore() {
  ++generation_;
  if (worker_.joinable()) worker_.join();
}

std::string SpeechModelStore::Installed() const {
  const auto root = fs::u8path(directory_);
  std::error_code error;
  if (!fs::exists(root / Marker, error)) return {};
  for (const auto& file : files_)
    if (fs::file_size(root / file.name, error) != file.size || error) return {};
  return directory_;
}

void SpeechModelStore::Download() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == "downloading" || state_ == "ready") return;
    state_ = "downloading";
    error_.clear();
  }
  if (worker_.joinable()) worker_.join();
  received_ = 0;
  const auto generation = ++generation_;
  worker_ = std::thread([this, generation] { Run(generation); });
}

void SpeechModelStore::Cancel() {
  ++generation_;
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == "downloading") state_ = "missing";
}

bool SpeechModelStore::Remove(std::string& error) {
  Cancel();
  if (worker_.joinable()) worker_.join();
  std::error_code code;
  fs::remove_all(fs::u8path(directory_), code);
  std::lock_guard<std::mutex> lock(mutex_);
  state_ = "missing";
  if (code) error = "无法删除本机语音模型，请稍后重试";
  return !code;
}

nlohmann::json SpeechModelStore::Status() const {
  uint64_t total = 0;
  for (const auto& file : files_) total += file.size;
  std::lock_guard<std::mutex> lock(mutex_);
  return {{"state", state_}, {"received", state_ == "ready" ? total : received_.load()}, {"total", total}, {"error", error_}};
}

void SpeechModelStore::Run(uint64_t generation) {
  const auto live = [&] { return generation_ == generation; };
  const auto fail = [&](const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!live()) return;
    state_ = "failed";
    error_ = message;
  };
  const auto root = fs::u8path(directory_);
  std::error_code code;
  fs::create_directories(root, code);
  fs::remove(root / Marker, code);
  if (code && !fs::exists(root)) { fail("无法创建模型目录，请检查磁盘空间和权限"); return; }
  uint64_t done = 0;
  for (const auto& file : files_) {
    const auto target = root / file.name;
    if (Verify(target, file)) { done += file.size; received_ = done; continue; }
    bool ok = false;
    for (const auto& mirror : mirrors_) {
      if (!live()) return;
      const auto partial = fs::path(target).concat(".part");
      {
        std::ofstream output(partial, std::ios::binary | std::ios::trunc);
        Sha256 hash;
        uint64_t written = 0;
        const cpr::WriteCallback write{[&](std::string data, intptr_t) {
          if (!live() || written + data.size() > file.size) return false;
          output.write(data.data(), std::streamsize(data.size()));
          hash.Update(data.data(), data.size());
          written += data.size();
          received_ = done + written;
          return bool(output);
        }};
        const cpr::ProgressCallback progress{[&](cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t) {
          return live();
        }};
        // Mirrors redirect to their storage hosts; the hash pins the content.
        const auto response = cpr::Get(cpr::Url{mirror + file.name}, cpr::ConnectTimeout{10000},
            cpr::LowSpeed{1024, 30}, cpr::Redirect{10L}, write, progress);
        output.close();
        ok = live() && response.error.code == cpr::ErrorCode::OK && response.status_code == 200 &&
            written == file.size && hash.Finish() == file.sha256;
      }
      if (ok) { fs::rename(partial, target, code); ok = !code; }
      fs::remove(partial, code);
      if (ok) break;
      received_ = done;
    }
    if (!live()) return;
    if (!ok) { fail("本机语音模型下载失败，请检查网络后重试"); return; }
    done += file.size;
  }
  std::ofstream(root / Marker) << root.filename().u8string() << "\n";
  std::lock_guard<std::mutex> lock(mutex_);
  if (live()) state_ = "ready";
}
}  // namespace Voice
