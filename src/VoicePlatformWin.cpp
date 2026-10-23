#include "VoicePlatform.hpp"
#include "VoiceEcho.hpp"
#include "VoiceEventQueue.hpp"
#include "VoiceIndicatorStyle.hpp"

#include <windows.h>
#include <winhttp.h>
#include <wincred.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <thread>

namespace Voice {
namespace {
std::wstring Wide(const std::string& value) {
  if (value.empty()) return {};
  const int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  std::wstring out(length, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), length);
  return out;
}

struct HttpHandle {
  HINTERNET value = nullptr;
  ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
};

using Microsoft::WRL::ComPtr;
using AudioClock = std::chrono::steady_clock;

struct ComScope {
  const bool owned = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
  ~ComScope() { if (owned) CoUninitialize(); }
};

// Opens the default endpoint in shared, event-driven mode. The audio engine
// converts between the device mix format and 16-bit mono at `rate`, so neither
// direction needs its own resampler.
HRESULT OpenStream(EDataFlow flow, DWORD rate, REFERENCE_TIME duration, HANDLE event,
                   ComPtr<IAudioClient>& client) {
  ComPtr<IMMDeviceEnumerator> enumerator;
  ComPtr<IMMDevice> device;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
  if (SUCCEEDED(hr)) hr = enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device);
  if (SUCCEEDED(hr)) hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
      reinterpret_cast<void**>(client.ReleaseAndGetAddressOf()));
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 1;
  format.nSamplesPerSec = rate;
  format.wBitsPerSample = 16;
  format.nBlockAlign = 2;
  format.nAvgBytesPerSec = rate * 2;
  if (SUCCEEDED(hr)) hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
      AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
      duration, 0, &format, nullptr);
  if (SUCCEEDED(hr)) hr = client->SetEventHandle(event);
  return hr;
}

int LatencyMs(IAudioClient* client, REFERENCE_TIME buffer) {
  REFERENCE_TIME latency = 0;
  client->GetStreamLatency(&latency);
  return static_cast<int>((latency + buffer) / 10000);
}

std::string Hresult(HRESULT hr) {
  char text[16];
  std::snprintf(text, sizeof(text), "0x%08lx", static_cast<unsigned long>(hr));
  return text;
}

struct NetworkChannel {
  std::atomic<bool> stopping{true};
  std::mutex mutex;
  std::condition_variable changed;
  HINTERNET socket = nullptr;
  std::deque<std::string> outgoing;
  size_t outgoingBytes = 0;
};

struct WinState {
  std::shared_ptr<EventQueue> queue = std::make_shared<EventQueue>();
  NetworkChannel channels[Channels];
  // Null when JPET_VOICE_AEC=0, for comparing raw microphone input.
  std::shared_ptr<EchoCanceller> echo;
  std::atomic<bool> captureStop{false}, captureRunning{false};
  std::atomic<int> renderLatencyMs{0};
  // Reply PCM (24 kHz mono) waiting for the render thread, guarded by renderMutex.
  std::mutex renderMutex;
  std::deque<int16_t> pending;
  float volume = 1;
  AudioClock::time_point audibleUntil{};
  std::atomic<bool> renderStop{false}, renderFailed{false};
  std::atomic<uint64_t> played{0};  // Reply samples written to the device since StopPlayback.
  HWND indicator = nullptr;
  bool indicatorError = false;
  std::wstring indicatorText;
  HFONT font = nullptr;
  HFONT titleFont = nullptr;
  HFONT shortcutFont = nullptr;
};

void Receive(const std::shared_ptr<WinState>& state, int channel, uint64_t generation,
             const std::string& url, const std::string& key) {
  auto& net = state->channels[channel];
  const auto fail = [&](const std::string& message) {
    state->queue->Network(channel, generation, Event::Type::Error, message);
  };
  const std::wstring address = Wide(url);
  URL_COMPONENTS components{};
  components.dwStructSize = sizeof(components);
  components.dwHostNameLength = components.dwUrlPathLength = components.dwExtraInfoLength = static_cast<DWORD>(-1);
  // WinHTTP cracks HTTPS URLs; the WebSocket upgrade is requested below.
  const std::wstring https = L"https" + address.substr(3);
  if (!WinHttpCrackUrl(https.c_str(), 0, 0, &components)) { fail("语音服务地址无效，请检查业务空间 ID"); return; }
  const std::wstring host(components.lpszHostName, components.dwHostNameLength);
  const std::wstring path = std::wstring(components.lpszUrlPath, components.dwUrlPathLength) +
      std::wstring(components.lpszExtraInfo, components.dwExtraInfoLength);
  HttpHandle session{WinHttpOpen(L"JPet voice", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session.value) { fail("无法初始化语音连接"); return; }
  WinHttpSetTimeouts(session.value, 15000, 15000, 15000, 15000);
  HttpHandle connection{WinHttpConnect(session.value, host.c_str(), components.nPort, 0)};
  HttpHandle request{connection.value ? WinHttpOpenRequest(connection.value, L"GET", path.c_str(),
      nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr};
  DWORD disable = WINHTTP_DISABLE_REDIRECTS;
  if (request.value) WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));
  const std::wstring authorization = L"Authorization: Bearer " + Wide(key) + L"\r\n";
  if (!request.value || !WinHttpSetOption(request.value, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
      !WinHttpAddRequestHeaders(request.value, authorization.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD) ||
      !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.value, nullptr)) {
    fail("语音连接失败，请检查网络和语音服务设置"); return;
  }
  DWORD status = 0, length = sizeof(status);
  WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX);
  if (status != 101) {
    fail(status == 401 || status == 403 ? "语音服务认证失败，请检查 API Key、业务空间 ID 和模型权限" :
        status == 429 ? "语音服务请求过于频繁或额度不足，请稍后重试" : "语音连接失败，请检查业务空间设置");
    return;
  }
  HttpHandle socket{WinHttpWebSocketCompleteUpgrade(request.value, 0)};
  if (!socket.value) { fail("无法建立语音 WebSocket 连接"); return; }
  {
    std::lock_guard<std::mutex> lock(net.mutex);
    if (net.stopping || state->queue->connections[channel] != generation) return;
    net.socket = socket.value;
    socket.value = nullptr;  // Disconnect owns closing this handle.
  }
  net.changed.notify_all();
  std::array<char, 16384> buffer;
  std::string message, binary;
  while (!net.stopping && state->queue->connections[channel] == generation) {
    HINTERNET active;
    { std::lock_guard<std::mutex> lock(net.mutex); active = net.socket; }
    if (!active) break;
    DWORD bytes = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
    const auto result = WinHttpWebSocketReceive(active, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, &type);
    if (result != NO_ERROR || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
      fail("语音连接已断开，请按 Ctrl 重新开启麦克风"); break;
    }
    if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
      if (message.size() + bytes > 4 * 1024 * 1024) { fail("语音服务返回的消息过大"); break; }
      message.append(buffer.data(), bytes);
      if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
        state->queue->Network(channel, generation, Event::Type::Message, std::move(message));
        message.clear();
      }
    } else if (type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
      if (binary.size() + bytes > 4 * 1024 * 1024) { fail("语音服务返回的消息过大"); break; }
      binary.append(buffer.data(), bytes);
      if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
        state->queue->Network(channel, generation, Event::Type::Binary, std::move(binary));
        binary.clear();
      }
    }
  }
}

LRESULT CALLBACK IndicatorProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    const auto create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  auto* state = reinterpret_cast<WinState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCHITTEST) return HTTRANSPARENT;
  if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
  if (message == WM_ERASEBKGND) return 1;
  if (message == WM_PAINT && state) {
    using namespace Indicator;
    const auto color = [](Color value) { return RGB(value.red, value.green, value.blue); };
    PAINTSTRUCT paint;
    HDC target = BeginPaint(window, &paint);
    RECT rect;
    GetClientRect(window, &rect);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, rect.right, rect.bottom);
    const auto oldBitmap = SelectObject(dc, bitmap);
    const int saved = SaveDC(dc);
    SetMapMode(dc, MM_ANISOTROPIC);
    SetWindowExtEx(dc, Width, Height, nullptr);
    SetViewportExtEx(dc, rect.right, rect.bottom, nullptr);
    rect = {0, 0, Width, Height};
    HBRUSH brush = CreateSolidBrush(color(Background));
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
    const Color accent = state->indicatorError ? ErrorAccent : Accent;
    const Color top = state->indicatorError ? ErrorTop : AccentTop;
    const Color soft = state->indicatorError ? ErrorSoft : Soft;
    // Paint the small header gradient without introducing another graphics dependency.
    for (int y = 0; y < 28; ++y) {
      const auto blend = [y](int a, int b) { return a + (b - a) * y / 27; };
      brush = CreateSolidBrush(RGB(blend(top.red, accent.red), blend(top.green, accent.green), blend(top.blue, accent.blue)));
      RECT line{0, y, Width, y + 1};
      FillRect(dc, &line, brush);
      DeleteObject(brush);
    }
    HPEN stripePen = CreatePen(PS_SOLID, 3, state->indicatorError ? RGB(246, 181, 198) : RGB(183, 224, 122));
    const auto oldPen = SelectObject(dc, stripePen);
    for (int x = 220; x < Width; x += 10) { MoveToEx(dc, x, 0, nullptr); LineTo(dc, x - 14, 28); }
    SelectObject(dc, oldPen);
    DeleteObject(stripePen);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    SelectObject(dc, state->titleFont);
    RECT title{14, 5, 220, 24};
    DrawTextW(dc, L"JPet · 语音对话", -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SelectObject(dc, GetStockObject(NULL_PEN));
    SetDCBrushColor(dc, RGB(255, 255, 255));
    RoundRect(dc, 277, 5, 326, 23, 14, 14);
    SetTextColor(dc, color(state->indicatorError ? Error : Ink));
    SelectObject(dc, state->shortcutFont);
    RECT shortcut{277, 5, 326, 23};
    DrawTextW(dc, L"Ctrl", -1, &shortcut, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SetDCBrushColor(dc, color(soft));
    Ellipse(dc, 14, 39, 48, 73);
    HPEN micPen = CreatePen(PS_SOLID, 2, color(state->indicatorError ? Error : Accent));
    SelectObject(dc, micPen);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, 28, 46, 35, 59, 6, 6);
    MoveToEx(dc, 24, 53, nullptr); LineTo(dc, 24, 56);
    Arc(dc, 24, 49, 39, 64, 24, 56, 39, 56);
    MoveToEx(dc, 38, 56, nullptr); LineTo(dc, 38, 53);
    MoveToEx(dc, 31, 63, nullptr); LineTo(dc, 31, 67);
    MoveToEx(dc, 27, 67, nullptr); LineTo(dc, 35, 67);
    SelectObject(dc, oldPen);
    DeleteObject(micPen);
    brush = CreateSolidBrush(color(soft));
    RECT footer{0, Height - 5, Width, Height};
    FillRect(dc, &footer, brush);
    DeleteObject(brush);
    HPEN border = CreatePen(PS_SOLID, 1, color(state->indicatorError ? ErrorTop : Border));
    SelectObject(dc, border);
    RoundRect(dc, 0, 0, Width, Height, Radius * 2, Radius * 2);
    SelectObject(dc, oldPen);
    DeleteObject(border);
    SelectObject(dc, state->font);
    wchar_t text[512]{};
    GetWindowTextW(window, text, 512);
    RECT body{60, 34, 324, 80};
    DrawTextW(dc, text, -1, &body, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    RestoreDC(dc, saved);
    GetClientRect(window, &rect);
    BitBlt(target, 0, 0, rect.right, rect.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window, &paint);
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

class WinPlatform final : public Platform {
 public:
  WinPlatform() {
    const char* aec = std::getenv("JPET_VOICE_AEC");
    if (!aec || std::strcmp(aec, "0") != 0) state_->echo = std::make_shared<EchoCanceller>();
  }
  ~WinPlatform() override {
    StopCapture(); DisconnectAll(); StopPlayback(); StopRender();
    if (state_->indicator) DestroyWindow(state_->indicator);
    if (state_->font) DeleteObject(state_->font);
    if (state_->titleFont) DeleteObject(state_->titleFont);
    if (state_->shortcutFont) DeleteObject(state_->shortcutFont);
  }
  bool ShortcutHeld() const override { return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0; }

  void Connect(int channel, const std::string& url, const std::string& key) override {
    Disconnect(channel);
    state_->channels[channel].stopping = false;
    const auto generation = ++state_->queue->connections[channel];
    const auto state = state_;
    receiver_[channel] = std::thread([state, channel, generation, url, key] { Receive(state, channel, generation, url, key); });
    sender_[channel] = std::thread([state, channel, generation] {
      auto& net = state->channels[channel];
      while (!net.stopping) {
        std::unique_lock<std::mutex> lock(net.mutex);
        net.changed.wait(lock, [&] { return net.stopping || (net.socket && !net.outgoing.empty()); });
        if (net.stopping) break;
        auto message = std::move(net.outgoing.front());
        net.outgoing.pop_front();
        net.outgoingBytes -= message.size();
        const auto socket = net.socket;
        lock.unlock();
        if (WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
              message.data(), static_cast<DWORD>(message.size())) != NO_ERROR) {
          state->queue->Network(channel, generation, Event::Type::Error, "语音发送失败，请检查网络后重试");
          break;
        }
      }
    });
  }

  void Disconnect(int channel) override {
    auto& net = state_->channels[channel];
    ++state_->queue->connections[channel];
    net.stopping = true;
    HINTERNET socket;
    {
      std::lock_guard<std::mutex> lock(net.mutex);
      socket = net.socket;
      net.socket = nullptr;
      net.outgoing.clear();
      net.outgoingBytes = 0;
    }
    net.changed.notify_all();
    if (socket) WinHttpCloseHandle(socket);
    if (receiver_[channel].joinable()) {
      // A pending synchronous DNS/TLS handshake cannot be canceled via a
      // WebSocket handle yet. It owns shared state and has a 15-second timeout;
      // let it finish off the UI thread. Its generation is already invalidated.
      if (socket) receiver_[channel].join();
      else receiver_[channel].detach();
    }
    if (sender_[channel].joinable()) sender_[channel].join();
    state_->queue->Clear(channel);
  }

  void Send(int channel, const std::string& message) override {
    auto& net = state_->channels[channel];
    std::lock_guard<std::mutex> lock(net.mutex);
    if (net.stopping) return;
    if (net.outgoingBytes + message.size() > 4 * 1024 * 1024) {
      state_->queue->Push(Event::Type::Error, "语音网络发送缓慢，请重新说话"); return;
    }
    net.outgoingBytes += message.size();
    net.outgoing.push_back(message);
    net.changed.notify_all();
  }

  void StartCapture() override {
    if (microphone_.joinable()) {
      if (state_->captureRunning) return;
      microphone_.join();  // The previous stream ended on a device error.
    }
    // The speaker stream runs while the microphone is open so AEC3 always
    // receives a continuous reference, silence included.
    StartRender();
    if (state_->echo) state_->echo->EndCapture();
    state_->captureStop = false;
    const auto generation = ++state_->queue->capture;
    state_->queue->capturing = true;
    state_->captureRunning = true;
    const auto state = state_;
    microphone_ = std::thread([state, generation] {
      CaptureLoop(state, generation);
      state->captureRunning = false;
    });
  }

  void StopCapture() override {
    if (!microphone_.joinable()) return;
    // The worker drains the packets already captured before it exits, so the
    // final words of this turn still reach the session.
    state_->captureStop = true;
    microphone_.join();
    state_->queue->capturing = false;
    lastAudio_ = AudioClock::now();
  }

  std::vector<Event> Poll() override {
    if (renderer_.joinable() && state_->renderFailed) {
      StopRender();
      StopPlayback();  // Replies queued after the failure would never play.
    }
    // Release the speaker stream once idle so it does not keep the device
    // awake (an open stream can block system sleep).
    if (renderer_.joinable() && !microphone_.joinable()) {
      if (IsPlaying()) lastAudio_ = AudioClock::now();
      else if (AudioClock::now() - lastAudio_ > std::chrono::seconds(3)) StopRender();
    }
    return state_->queue->Poll();
  }

  void Play(const std::string& pcm, float volume) override {
    StartRender();
    std::lock_guard<std::mutex> lock(state_->renderMutex);
    state_->volume = std::clamp(volume, 0.0f, 1.0f);
    for (size_t i = 0; i + 1 < pcm.size(); i += 2) {
      int16_t sample;
      std::memcpy(&sample, pcm.data() + i, sizeof(sample));
      state_->pending.push_back(sample);
    }
    lastAudio_ = AudioClock::now();
  }

  bool IsPlaying() const override {
    std::lock_guard<std::mutex> lock(state_->renderMutex);
    return !state_->pending.empty() || AudioClock::now() < state_->audibleUntil;
  }

  uint64_t PlayedSamples() const override { return state_->played; }

  void StopPlayback() override {
    // At most one device buffer (40 ms) of the reply still plays after this.
    std::lock_guard<std::mutex> lock(state_->renderMutex);
    state_->pending.clear();
    state_->audibleUntil = AudioClock::now();
    state_->played = 0;
  }

  void ShowIndicator(GLFWwindow* window, const std::string& text, bool error) override {
    if (text.empty()) { if (state_->indicator) ShowWindow(state_->indicator, SW_HIDE); return; }
    if (!state_->indicator) {
      WNDCLASSW type{};
      type.lpfnWndProc = IndicatorProc;
      type.hInstance = GetModuleHandleW(nullptr);
      type.lpszClassName = L"JPetVoiceIndicator";
      RegisterClassW(&type);
      state_->font = CreateFontW(-13, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
      state_->titleFont = CreateFontW(-11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
      state_->shortcutFont = CreateFontW(-10, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
      state_->indicator = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
          type.lpszClassName, L"", WS_POPUP, 0, 0, Indicator::Width, Indicator::Height, nullptr, nullptr, type.hInstance, state_.get());
      SetWindowRgn(state_->indicator, CreateRoundRectRgn(0, 0, Indicator::Width, Indicator::Height,
          Indicator::Radius * 2, Indicator::Radius * 2), TRUE);
    }
    const auto wideText = Wide(text);
    const bool changed = state_->indicatorError != error || state_->indicatorText != wideText;
    state_->indicatorError = error;
    if (state_->indicatorText != wideText) {
      state_->indicatorText = wideText;
      SetWindowTextW(state_->indicator, wideText.c_str());
    }
    RECT pet, screen;
    GetWindowRect(glfwGetWin32Window(window), &pet);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromRect(&pet, MONITOR_DEFAULTTONEAREST), &monitor);
    screen = monitor.rcWork;
    const UINT dpi = GetDpiForWindow(glfwGetWin32Window(window));
    const int width = MulDiv(Indicator::Width, dpi, 96), height = MulDiv(Indicator::Height, dpi, 96);
    RECT previous;
    GetWindowRect(state_->indicator, &previous);
    if (previous.right - previous.left != width || previous.bottom - previous.top != height)
      SetWindowRgn(state_->indicator, CreateRoundRectRgn(0, 0, width, height,
          MulDiv(Indicator::Radius * 2, dpi, 96), MulDiv(Indicator::Radius * 2, dpi, 96)), TRUE);
    const LONG x = std::clamp((pet.left + pet.right - width) / 2, screen.left, screen.right - width);
    const LONG y = std::clamp(pet.top - height - MulDiv(8, dpi, 96), screen.top, screen.bottom - height);
    SetWindowPos(state_->indicator, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (changed) InvalidateRect(state_->indicator, nullptr, FALSE);
  }

 private:
  void StartRender() {
    if (renderer_.joinable()) {
      if (!state_->renderFailed) return;
      StopRender();
    }
    state_->renderStop = false;
    state_->renderFailed = false;
    lastAudio_ = AudioClock::now();
    const auto state = state_;
    renderer_ = std::thread([state] { RenderLoop(state); });
  }

  void StopRender() {
    if (!renderer_.joinable()) return;
    state_->renderStop = true;
    renderer_.join();
  }

  static void CaptureLoop(const std::shared_ptr<WinState>& state, uint64_t generation) {
    ComScope com;
    const auto fail = [&](const std::string& message, HRESULT hr) {
      state->queue->Captured(generation, Event::Type::Diagnostic, "[Voice] Capture failed hr=" + Hresult(hr));
      state->queue->Captured(generation, Event::Type::Error, message);
    };
    constexpr REFERENCE_TIME Buffer = 200000;  // 20 ms.
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    HRESULT hr = event ? OpenStream(eCapture, EchoCanceller::CaptureRate, Buffer, event, client) : E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&capture));
    if (SUCCEEDED(hr)) hr = client->Start();
    if (FAILED(hr)) {
      fail(hr == E_ACCESSDENIED ? "无法打开麦克风，请在 Windows 隐私设置中允许 JPet 使用麦克风"
                                : "无法打开麦克风，请检查 Windows 麦克风权限和输入设备", hr);
      if (event) CloseHandle(event);
      return;
    }
    const int captureLatency = LatencyMs(client.Get(), Buffer);
    if (state->echo) state->echo->SetStreamDelay(captureLatency + state->renderLatencyMs);
    state->queue->Captured(generation, Event::Type::Diagnostic, "[Voice] Capture wasapi rate=16000 latency_ms=" +
        std::to_string(captureLatency) + " aec=" + (state->echo ? "on" : "off"));
    auto reported = AudioClock::now();
    std::string chunk;
    for (;;) {
      const bool stopping = state->captureStop;
      if (!stopping) WaitForSingleObject(event, 100);
      chunk.clear();
      UINT32 packet = 0;
      while (SUCCEEDED(hr = capture->GetNextPacketSize(&packet)) && packet) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        if (FAILED(hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) chunk.append(frames * sizeof(int16_t), '\0');
        else chunk.append(reinterpret_cast<const char*>(data), frames * sizeof(int16_t));
        capture->ReleaseBuffer(frames);
      }
      if (FAILED(hr)) { fail("麦克风录音中断，请重新说话", hr); break; }
      if (state->echo && !chunk.empty()) chunk = state->echo->Capture(chunk);
      if (!chunk.empty()) state->queue->Captured(generation, Event::Type::Microphone, std::move(chunk));
      if (state->echo && AudioClock::now() - reported > std::chrono::seconds(5)) {
        reported = AudioClock::now();
        state->queue->Captured(generation, Event::Type::Diagnostic, "[Voice] AEC " + state->echo->Summary());
      }
      if (stopping) break;
    }
    client->Stop();
    CloseHandle(event);
  }

  static void RenderLoop(const std::shared_ptr<WinState>& state) {
    ComScope com;
    constexpr REFERENCE_TIME Buffer = 400000;  // 40 ms.
    constexpr int Rate = EchoCanceller::RenderRate;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;
    UINT32 bufferFrames = 0;
    HRESULT hr = event ? OpenStream(eRender, Rate, Buffer, event, client) : E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) hr = client->GetBufferSize(&bufferFrames);
    if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&render));
    if (SUCCEEDED(hr)) hr = client->Start();
    if (SUCCEEDED(hr)) state->renderLatencyMs = LatencyMs(client.Get(), Buffer);
    std::vector<int16_t> block;
    while (SUCCEEDED(hr) && !state->renderStop) {
      WaitForSingleObject(event, 100);
      UINT32 padding = 0;
      if (FAILED(hr = client->GetCurrentPadding(&padding))) break;
      const UINT32 available = bufferFrames - padding;
      if (!available) continue;
      // Fill the whole free space, with silence when no reply is queued, so the
      // stream and the AEC reference stay continuous.
      block.assign(available, 0);
      {
        std::lock_guard<std::mutex> lock(state->renderMutex);
        const size_t audible = (std::min)(static_cast<size_t>(available), state->pending.size());
        for (size_t i = 0; i < audible; ++i)
          block[i] = static_cast<int16_t>(std::lround(state->pending[i] * state->volume));
        state->pending.erase(state->pending.begin(), state->pending.begin() + audible);
        state->played += audible;
        if (audible) state->audibleUntil = AudioClock::now() +
            std::chrono::milliseconds((padding + audible) * 1000 / Rate);
      }
      BYTE* data = nullptr;
      if (FAILED(hr = render->GetBuffer(available, &data))) break;
      std::memcpy(data, block.data(), available * sizeof(int16_t));
      render->ReleaseBuffer(available, 0);
      if (state->echo) state->echo->Render(block.data(), block.size());
    }
    if (FAILED(hr)) {
      state->queue->Push(Event::Type::Diagnostic, "[Voice] Render failed hr=" + Hresult(hr));
      state->queue->Push(Event::Type::Error, "无法播放轴伊的回复，请检查声音输出设备");
      std::lock_guard<std::mutex> lock(state->renderMutex);
      state->pending.clear();
      state->audibleUntil = {};
    }
    if (client) client->Stop();
    if (event) CloseHandle(event);
    state->renderFailed = FAILED(hr);
  }

  std::shared_ptr<WinState> state_ = std::make_shared<WinState>();
  std::thread receiver_[Channels], sender_[Channels], microphone_, renderer_;
  AudioClock::time_point lastAudio_{};
};
std::wstring CredentialTarget(const std::string& profile) { return L"JPet/Qwen/" + Wide(profile); }
}  // namespace

std::unique_ptr<Platform> MakePlatform() { return std::make_unique<WinPlatform>(); }

bool SaveApiKey(const std::string& profile, const std::string& key, std::string& error) {
  auto target = CredentialTarget(profile);
  if (key.empty()) {
    if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND) return true;
  } else {
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.CredentialBlobSize = static_cast<DWORD>(key.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(key.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (CredWriteW(&credential, 0)) return true;
  }
  error = "无法保存 API Key，请检查 Windows 凭据管理器";
  return false;
}

std::string LoadApiKey(const std::string& profile, std::string& error) {
  PCREDENTIALW credential = nullptr;
  if (!CredReadW(CredentialTarget(profile).c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
    if (GetLastError() != ERROR_NOT_FOUND) error = "无法读取 API Key，请检查 Windows 凭据管理器";
    return {};
  }
  const std::string key(reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize);
  CredFree(credential);
  return key;
}
bool SaveChatApiKey(const std::string& profile, const std::string& key, std::string& error) {
  auto target = L"JPet/Chat/" + Wide(profile);
  if (key.empty()) {
    if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND) return true;
  } else {
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.CredentialBlobSize = static_cast<DWORD>(key.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(key.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (CredWriteW(&credential, 0)) return true;
  }
  error = "无法保存大模型 API Key，请检查 Windows 凭据管理器";
  return false;
}

std::string LoadChatApiKey(const std::string& profile, std::string& error) {
  PCREDENTIALW credential = nullptr;
  if (!CredReadW((L"JPet/Chat/" + Wide(profile)).c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
    if (GetLastError() != ERROR_NOT_FOUND) error = "无法读取大模型 API Key，请检查 Windows 凭据管理器";
    return {};
  }
  const std::string key(reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize);
  CredFree(credential);
  return key;
}

}  // namespace Voice
