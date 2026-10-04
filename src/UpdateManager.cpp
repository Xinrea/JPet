#include "UpdateManager.hpp"
#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "Platform.hpp"
#include <httplib.h>
#include <openssl/rand.h>
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#include <openssl/x509.h>
#endif

namespace {
namespace fs = std::filesystem;
fs::path DataPath() {
#ifdef _WIN32
  return fs::path(LAppDefine::documentPath) / L"updates";
#else
  return fs::u8path(LAppPal::WStringToString(LAppDefine::documentPath)) / "updates";
#endif
}

// Use OS trust roots instead of disabling verification or relying on a
// developer machine's OpenSSL installation being present on the user's PC.
void ConfigureTLS(httplib::Client& client) {
  client.enable_server_certificate_verification(true);
  client.set_follow_location(false);
  client.set_connection_timeout(5, 0);
  client.set_read_timeout(15, 0);
  client.set_write_timeout(5, 0);
#ifdef _WIN32
  auto roots = CertOpenSystemStoreW(0, L"ROOT");
  if (!roots) throw std::runtime_error("无法读取系统证书");
  X509_STORE* store = X509_STORE_new();
  if (!store) { CertCloseStore(roots, 0); throw std::runtime_error("无法初始化系统证书"); }
  PCCERT_CONTEXT cert = nullptr;
  while ((cert = CertEnumCertificatesInStore(roots, cert)) != nullptr) {
    const unsigned char* bytes = cert->pbCertEncoded;
    X509* parsed = d2i_X509(nullptr, &bytes, cert->cbCertEncoded);
    if (parsed) { X509_STORE_add_cert(store, parsed); X509_free(parsed); }
  }
  CertCloseStore(roots, 0);
  client.set_ca_cert_store(store); // Ownership transfers to the SSL client.
#else
  client.set_ca_cert_path("/etc/ssl/cert.pem");
#endif
}

httplib::Headers Headers() {
  return {{"User-Agent", "JPet/" VERSION}, {"Accept", "application/vnd.github+json"},
          {"X-GitHub-Api-Version", "2022-11-28"}};
}

std::pair<std::string, std::string> DownloadAddress(const std::string& url) {
  const std::regex pattern(R"(https://(github\.com|release-assets\.githubusercontent\.com|objects\.githubusercontent\.com)(/[^\s#]*))");
  std::smatch parts;
  if (!std::regex_match(url, parts, pattern)) throw std::runtime_error("更新包重定向地址无效");
  return {"https://" + parts[1].str(), parts[2].str()};
}

std::string WorkId() {
  unsigned char bytes[12];
  if (RAND_bytes(bytes, sizeof(bytes)) != 1) throw std::runtime_error("无法创建更新目录");
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (auto byte : bytes) { result += hex[byte >> 4]; result += hex[byte & 15]; }
  return result;
}
}

UpdateManager* UpdateManager::GetInstance() { static UpdateManager instance; return &instance; }
UpdateManager::~UpdateManager() { Stop(); }

void UpdateManager::Start(std::function<void(const Updates::Release&)> notify) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stopping_.exchange(false)) return;
  notify_ = std::move(notify);
  checkRequested_ = true;
  state_ = "checking";
  // Surface helper failures after the old version is relaunched.
  std::ifstream failure(DataPath() / "last-error.txt");
  if (failure) installError_.assign(std::istreambuf_iterator<char>(failure), {});
  worker_ = std::thread(&UpdateManager::Run, this);
}

void UpdateManager::Stop() {
  stopping_ = true;
  wake_.notify_all();
  if (worker_.joinable()) worker_.join();
}

bool UpdateManager::Busy() const {
  return state_ == "checking" || state_ == "downloading" || state_ == "verifying" || state_ == "installing";
}

nlohmann::json UpdateManager::Status() {
  std::lock_guard<std::mutex> lock(mutex_);
  return {{"local_version", VERSION}, {"latest_version", release_.version}, {"need_update", release_.newer},
          {"state", state_}, {"error", error_.empty() ? installError_ : error_}, {"release_url", release_.pageURL.empty() ? Updates::ReleasesURL : release_.pageURL},
          {"release_notes", release_.notes}, {"download_available", release_.newer && !release_.downloadURL.empty() && !release_.sha256.empty()},
          {"downloaded_bytes", downloaded_}, {"total_bytes", release_.size},
          {"progress", release_.size ? static_cast<int>(downloaded_ * 100 / release_.size) : 0}};
}

bool UpdateManager::Check(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_ || Busy() || state_ == "ready") { error = "请等待当前更新操作完成"; return false; }
  checkRequested_ = true; state_ = "checking"; error_.clear();
  wake_.notify_all();
  return true;
}

bool UpdateManager::Download(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_ || Busy() || state_ == "ready") { error = "请等待当前更新操作完成"; return false; }
  if (!release_.newer || release_.downloadURL.empty() || release_.sha256.empty()) {
    error = "暂无可自动安装的更新包，请查看 GitHub Release"; return false;
  }
  downloaded_ = 0; error_.clear(); state_ = "downloading"; downloadRequested_ = true;
  wake_.notify_all();
  return true;
}

bool UpdateManager::Install(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_ || state_ != "ready" || staged_.empty()) { error = "请先完成更新包下载"; return false; }
  state_ = "installing"; error_.clear(); installRequested_ = true;
  return true;
}

bool UpdateManager::ApplyPendingInstall() {
  fs::path staged, work;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!installRequested_) return false;
    installRequested_ = false; staged = staged_; work = work_;
  }
  std::string error;
  if (Platform::LaunchUpdate(staged, work, DataPath() / "last-error.txt", error)) return true;
  std::lock_guard<std::mutex> lock(mutex_);
  state_ = "ready"; error_ = error;
  return false;
}

void UpdateManager::Run() {
  auto nextCheck = std::chrono::steady_clock::now();
  while (!stopping_) {
    bool download = false;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait_until(lock, nextCheck, [&] { return stopping_ || checkRequested_ || downloadRequested_; });
      if (stopping_) break;
      download = downloadRequested_;
      if (!download && !checkRequested_ && (Busy() || state_ == "ready")) {
        nextCheck = std::chrono::steady_clock::now() + std::chrono::hours(6);
        continue;
      }
      checkRequested_ = false; downloadRequested_ = false;
      if (!download) state_ = "checking";
    }
    try {
      if (download) DownloadRelease(); else CheckRelease();
    } catch (const std::exception& e) {
      LAppPal::PrintLog(LogLevel::Warn, "[Updater]%s", e.what());
      std::lock_guard<std::mutex> lock(mutex_);
      state_ = "error"; error_ = e.what();
    }
    nextCheck = std::chrono::steady_clock::now() + std::chrono::hours(6);
  }
}

void UpdateManager::CheckRelease() {
  httplib::Client client("https://api.github.com");
  ConfigureTLS(client);
  std::string body;
  auto response = client.Get("/repos/Xinrea/JPet/releases/latest", Headers(), [&](const char* data, size_t size) {
    if (stopping_ || body.size() + size > 2 * 1024 * 1024) return false;
    body.append(data, size); return true;
  });
  if (!response) throw std::runtime_error("无法连接 GitHub，请稍后重试");
  if (response->status == 404) {
    std::lock_guard<std::mutex> lock(mutex_);
    release_ = {}; state_ = "idle"; error_ = "尚无正式 Release"; return;
  }
  if (response->status == 403 || response->status == 429) throw std::runtime_error("GitHub 请求过于频繁，请稍后重试");
  if (response->status != 200) throw std::runtime_error("GitHub 版本检查失败（HTTP " + std::to_string(response->status) + "）");
  auto release = Updates::ParseRelease(nlohmann::json::parse(body), VERSION, Updates::PlatformName());
  bool notify = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    release_ = release; state_ = release.newer ? "available" : "idle"; error_.clear();
    if (release.newer && (release.downloadURL.empty() || release.sha256.empty()))
      error_ = "此 Release 尚无适用的完整校验更新包，可前往 GitHub 手动下载";
    notify = release.newer && notifiedVersion_ != release.version;
    if (notify) notifiedVersion_ = release.version;
  }
  if (notify && notify_ && !stopping_) notify_(release);
}

void UpdateManager::DownloadRelease() {
  Updates::Release release;
  { std::lock_guard<std::mutex> lock(mutex_); release = release_; }
  const auto work = DataPath() / WorkId();
  fs::create_directories(work);
  try {
    const auto partial = work / "package.zip.part";
    std::ofstream output(partial, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("无法写入更新包，请检查磁盘空间");
    auto url = release.downloadURL;
    uint64_t received = 0;
    bool completed = false;
    for (int redirect = 0; redirect < 6; ++redirect) {
      const auto [origin, path] = DownloadAddress(url);
      httplib::Client client(origin);
      ConfigureTLS(client);
      int status = 0;
      auto response = client.Get(path, {{"User-Agent", "JPet/" VERSION}, {"Accept", "application/octet-stream"}},
        [&](const httplib::Response& response) { status = response.status; return !stopping_; },
        [&](const char* bytes, size_t size) {
          if (stopping_) return false;
          if (status != 200) return true;
          if (received + size > release.size) return false;
          output.write(bytes, size);
          if (!output) return false;
          received += size;
          std::lock_guard<std::mutex> lock(mutex_); downloaded_ = received;
          return true;
        });
      if (!response) throw std::runtime_error("更新包下载中断，请重新下载");
      if (status >= 300 && status < 400 && response->has_header("Location")) {
        url = response->get_header_value("Location"); continue;
      }
      if (status != 200) throw std::runtime_error("更新包下载失败（HTTP " + std::to_string(status) + "）");
      completed = true; break;
    }
    output.close();
    if (!completed || !output) throw std::runtime_error("更新包下载未完成");
    { std::lock_guard<std::mutex> lock(mutex_); state_ = "verifying"; }
    Updates::Verify(partial, release);
    if (stopping_) throw std::runtime_error("下载已停止");
    const auto archive = work / "package.zip";
    fs::rename(partial, archive);
    const auto stage = work / "stage";
    std::string error;
    if (!Platform::ExtractUpdate(archive, stage, error)) throw std::runtime_error(error);
#ifdef _WIN32
    const auto staged = stage / "JPet";
    const auto executable = staged / "JPet.exe";
#else
    const auto staged = stage / "JPet.app";
    const auto executable = staged / "Contents/MacOS/JPet";
#endif
    if (!fs::is_regular_file(executable)) throw std::runtime_error("更新包缺少 JPet 程序");
    if (stopping_) throw std::runtime_error("下载已停止");
    std::lock_guard<std::mutex> lock(mutex_);
    if (!work_.empty()) { std::error_code ignored; fs::remove_all(work_, ignored); }
    work_ = work; staged_ = staged; state_ = "ready"; error_.clear();
  } catch (...) {
    std::error_code ignored; fs::remove_all(work, ignored);
    throw;
  }
}
