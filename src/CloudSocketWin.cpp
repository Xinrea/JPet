#include "CloudSocketInbox.hpp"
#include <windows.h>
#include <winhttp.h>
#include <array>
#include <atomic>
#include <thread>

namespace {
struct HttpHandle {
  HINTERNET value = nullptr;
  ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
};
struct WinConnection {
  explicit WinConnection(std::function<void()> notify) : inbox(std::move(notify)) {}
  CloudSocketInbox inbox;
  std::mutex mutex;
  std::condition_variable changed;
  std::atomic<bool> stopping{false}, failed{false};
  HINTERNET socket = nullptr;
  void Fail() {
    failed = true;
    inbox.Push({CloudSocketEvent::Type::Error, "云端连接中断，游戏已暂停，正在重试"});
    changed.notify_all();
  }
};

void RunSocket(const std::shared_ptr<WinConnection>& state, const std::string& url) {
  const std::string http = url.rfind("wss://", 0) == 0 ? "https://" + url.substr(6) : "http://" + url.substr(5);
  const int length = MultiByteToWideChar(CP_UTF8, 0, http.data(), static_cast<int>(http.size()), nullptr, 0);
  std::wstring address(length, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, http.data(), static_cast<int>(http.size()), address.data(), length);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts)) { state->Fail(); return; }
  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  const std::wstring path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) + std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  HttpHandle session{WinHttpOpen(L"JPet game", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session.value) { state->Fail(); return; }
  WinHttpSetTimeouts(session.value, 2000, 2000, 2000, 4000);
  HttpHandle connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};
  HttpHandle request{connection.value ? WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr};
  DWORD disabled = WINHTTP_DISABLE_REDIRECTS;
  if (!request.value || !WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
      !WinHttpSetOption(request.value, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
      !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.value, nullptr)) { state->Fail(); return; }
  DWORD status = 0, bytes = sizeof(status);
  if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX) || status != 101) { state->Fail(); return; }
  HttpHandle socket{WinHttpWebSocketCompleteUpgrade(request.value, 0)};
  if (!socket.value) { state->Fail(); return; }
  HINTERNET active;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->stopping) return;
    active = state->socket = socket.value;
    socket.value = nullptr;  // Close cancels a blocking receive and owns this handle.
  }
  state->changed.notify_all();
  std::array<char, 16384> buffer;
  std::string message;
  while (!state->stopping) {
    DWORD count = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
    const auto error = WinHttpWebSocketReceive(active, buffer.data(), static_cast<DWORD>(buffer.size()), &count, &type);
    if (state->stopping) return;
    if (error != NO_ERROR || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) { state->Fail(); return; }
    if (type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE && type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) { state->Fail(); return; }
    if (message.size() + count > CloudSocketInbox::MaxMessageBytes) { state->Fail(); return; }
    message.append(buffer.data(), count);
    if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
      state->inbox.Push({CloudSocketEvent::Type::Message, std::move(message)});
      message.clear();
    }
  }
}

class WinCloudSocket final : public CloudSocket {
 public:
  ~WinCloudSocket() override { Close(); }
  void Connect(const std::string& url, std::function<void()> notify) override {
    Close();
    state_ = std::make_shared<WinConnection>(std::move(notify));
    receiver_ = std::thread(RunSocket, state_, url);
  }
  bool Send(const std::string& message) override {
    if (!state_) return false;
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->changed.wait_for(lock, std::chrono::seconds(8), [&] { return state_->socket || state_->stopping || state_->failed; });
    if (!state_->socket || state_->stopping || state_->failed) return false;
    return WinHttpWebSocketSend(state_->socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
      const_cast<char*>(message.data()), static_cast<DWORD>(message.size())) == NO_ERROR;
  }
  bool Receive(CloudSocketEvent& event, std::chrono::milliseconds timeout) override {
    return state_ && state_->inbox.Receive(event, timeout);
  }
  void Close() override {
    if (!state_) return;
    state_->stopping = true;
    state_->changed.notify_all(); state_->inbox.Close();
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->socket) { WinHttpCloseHandle(state_->socket); state_->socket = nullptr; }
    }
    if (receiver_.joinable()) receiver_.join();
    state_.reset();
  }
 private:
  std::shared_ptr<WinConnection> state_;
  std::thread receiver_;
};
}

std::unique_ptr<CloudSocket> CreateCloudSocket() { return std::make_unique<WinCloudSocket>(); }
