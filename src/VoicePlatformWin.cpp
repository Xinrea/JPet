#include "VoicePlatform.hpp"
#include "VoiceEventQueue.hpp"
#include "VoiceIndicatorStyle.hpp"

#include <windows.h>
#include <winhttp.h>
#include <wincred.h>
#include <mmsystem.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <list>
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

struct InputBuffer {
  WAVEHDR header{};
  std::array<char, 640> bytes{};  // 20 ms, 16 kHz mono s16le.
};
struct OutputBuffer {
  WAVEHDR header{};
  std::string bytes;
};

struct WinState {
  std::shared_ptr<EventQueue> queue = std::make_shared<EventQueue>();
  std::atomic<bool> stopping{true};
  std::mutex networkMutex;
  std::condition_variable networkChanged;
  HINTERNET socket = nullptr;
  std::deque<std::string> outgoing;
  size_t outgoingBytes = 0;
  HWAVEIN input = nullptr;
  HANDLE inputEvent = nullptr;
  std::array<InputBuffer, 8> inputBuffers;
  HWAVEOUT output = nullptr;
  std::list<std::unique_ptr<OutputBuffer>> outputBuffers;
  HWND indicator = nullptr;
  bool indicatorError = false;
  std::wstring indicatorText;
  HFONT font = nullptr;
  HFONT titleFont = nullptr;
  HFONT shortcutFont = nullptr;
};

void Receive(const std::shared_ptr<WinState>& state, uint64_t generation,
             const std::string& url, const std::string& key) {
  const auto fail = [&](const std::string& message) {
    state->queue->Network(generation, Event::Type::Error, message);
  };
  const std::wstring address = Wide(url);
  URL_COMPONENTS components{};
  components.dwStructSize = sizeof(components);
  components.dwHostNameLength = components.dwUrlPathLength = components.dwExtraInfoLength = static_cast<DWORD>(-1);
  // WinHTTP cracks HTTPS URLs; the WebSocket upgrade is requested below.
  const std::wstring https = L"https" + address.substr(3);
  if (!WinHttpCrackUrl(https.c_str(), 0, 0, &components)) { fail("千问语音地址无效，请检查业务空间 ID"); return; }
  const std::wstring host(components.lpszHostName, components.dwHostNameLength);
  const std::wstring path = std::wstring(components.lpszUrlPath, components.dwUrlPathLength) +
      std::wstring(components.lpszExtraInfo, components.dwExtraInfoLength);
  HttpHandle session{WinHttpOpen(L"JPet voice", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session.value) { fail("无法初始化千问语音连接"); return; }
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
    fail("千问语音连接失败，请检查网络和北京地域的业务空间设置"); return;
  }
  DWORD status = 0, length = sizeof(status);
  WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX);
  if (status != 101) {
    fail(url.find("/v1/ai/realtime") != std::string::npos
        ? (status == 401 || status == 403 ? "PowerLive 登录已失效，请在设置中重新登录" : status == 429 ? "今日 AI 额度不足，北京时间零点重置；可以切换自定义服务继续使用" : status == 409 ? "此账号已有语音连接，请先结束其他设备的对话" : "JPet AI 服务暂时不可用，请稍后重试")
        : status == 401 || status == 403 ? "千问认证失败，请检查 API Key、业务空间 ID 和模型权限" :
        status == 429 ? "千问请求过于频繁或额度不足，请稍后重试" : "千问语音连接失败，请检查业务空间设置");
    return;
  }
  HttpHandle socket{WinHttpWebSocketCompleteUpgrade(request.value, 0)};
  if (!socket.value) { fail("无法建立千问语音 WebSocket 连接"); return; }
  {
    std::lock_guard<std::mutex> lock(state->networkMutex);
    if (state->stopping || state->queue->connection != generation) return;
    state->socket = socket.value;
    socket.value = nullptr;  // Disconnect owns closing this handle.
  }
  state->networkChanged.notify_all();
  std::array<char, 16384> buffer;
  std::string message;
  while (!state->stopping && state->queue->connection == generation) {
    HINTERNET active;
    { std::lock_guard<std::mutex> lock(state->networkMutex); active = state->socket; }
    if (!active) break;
    DWORD bytes = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
    const auto result = WinHttpWebSocketReceive(active, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, &type);
    if (result != NO_ERROR || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
      fail("千问语音连接已断开，请按 Ctrl 重新开启麦克风"); break;
    }
    if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
      if (message.size() + bytes > 4 * 1024 * 1024) { fail("千问返回的语音消息过大"); break; }
      message.append(buffer.data(), bytes);
      if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
        state->queue->Network(generation, Event::Type::Message, std::move(message));
        message.clear();
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
  ~WinPlatform() override {
    StopCapture(); Disconnect(); StopPlayback();
    if (state_->output) waveOutClose(state_->output);
    if (state_->indicator) DestroyWindow(state_->indicator);
    if (state_->font) DeleteObject(state_->font);
    if (state_->titleFont) DeleteObject(state_->titleFont);
    if (state_->shortcutFont) DeleteObject(state_->shortcutFont);
  }
  bool ShortcutHeld() const override { return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0; }

  void Connect(const std::string& url, const std::string& key) override {
    Disconnect();
    state_->stopping = false;
    const auto generation = ++state_->queue->connection;
    const auto state = state_;
    receiver_ = std::thread([state, generation, url, key] { Receive(state, generation, url, key); });
    sender_ = std::thread([state, generation] {
      while (!state->stopping) {
        std::unique_lock<std::mutex> lock(state->networkMutex);
        state->networkChanged.wait(lock, [&] { return state->stopping || (state->socket && !state->outgoing.empty()); });
        if (state->stopping) break;
        auto message = std::move(state->outgoing.front());
        state->outgoing.pop_front();
        state->outgoingBytes -= message.size();
        const auto socket = state->socket;
        lock.unlock();
        if (WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
              message.data(), static_cast<DWORD>(message.size())) != NO_ERROR) {
          state->queue->Network(generation, Event::Type::Error, "千问语音发送失败，请检查网络后重试");
          break;
        }
      }
    });
  }

  void Disconnect() override {
    ++state_->queue->connection;
    state_->stopping = true;
    HINTERNET socket;
    {
      std::lock_guard<std::mutex> lock(state_->networkMutex);
      socket = state_->socket;
      state_->socket = nullptr;
      state_->outgoing.clear();
      state_->outgoingBytes = 0;
    }
    state_->networkChanged.notify_all();
    if (socket) WinHttpCloseHandle(socket);
    if (receiver_.joinable()) {
      // A pending synchronous DNS/TLS handshake cannot be canceled via a
      // WebSocket handle yet. It owns shared state and has a 15-second timeout;
      // let it finish off the UI thread. Its generation is already invalidated.
      if (socket) receiver_.join();
      else receiver_.detach();
    }
    if (sender_.joinable()) sender_.join();
    state_->queue->Clear();
  }

  void Send(const std::string& message) override {
    std::lock_guard<std::mutex> lock(state_->networkMutex);
    if (state_->stopping) return;
    if (state_->outgoingBytes + message.size() > 4 * 1024 * 1024) {
      state_->queue->Push(Event::Type::Error, "语音网络发送缓慢，请重新说话"); return;
    }
    state_->outgoingBytes += message.size();
    state_->outgoing.push_back(message);
    state_->networkChanged.notify_all();
  }

  void StartCapture() override {
    if (state_->input) return;
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = 16000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = 32000;
    state_->inputEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!state_->inputEvent || waveInOpen(&state_->input, WAVE_MAPPER, &format,
          reinterpret_cast<DWORD_PTR>(state_->inputEvent), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
      StopCapture();
      state_->queue->Push(Event::Type::Error, "无法打开麦克风，请检查 Windows 麦克风权限和输入设备");
      return;
    }
    for (auto& buffer : state_->inputBuffers) {
      buffer.header = {};
      buffer.header.lpData = buffer.bytes.data();
      buffer.header.dwBufferLength = static_cast<DWORD>(buffer.bytes.size());
      if (waveInPrepareHeader(state_->input, &buffer.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR ||
          waveInAddBuffer(state_->input, &buffer.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        StopCapture(); state_->queue->Push(Event::Type::Error, "无法准备麦克风录音，请更换输入设备"); return;
      }
    }
    state_->queue->capturing = true;
    if (waveInStart(state_->input) != MMSYSERR_NOERROR) {
      StopCapture(); state_->queue->Push(Event::Type::Error, "无法启动麦克风录音"); return;
    }
    const auto state = state_;
    microphone_ = std::thread([state] {
      for (;;) {
        WaitForSingleObject(state->inputEvent, 20);
        for (auto& buffer : state->inputBuffers) {
          if (!(buffer.header.dwFlags & WHDR_DONE)) continue;
          if (buffer.header.dwBytesRecorded) state->queue->Push(Event::Type::Microphone,
              std::string(buffer.bytes.data(), buffer.header.dwBytesRecorded));
          buffer.header.dwBytesRecorded = 0;
          if (state->queue->capturing && waveInAddBuffer(state->input, &buffer.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            state->queue->capturing = false;
            state->queue->Push(Event::Type::Error, "麦克风录音中断，请重新说话");
          }
        }
        if (!state->queue->capturing) break;
      }
    });
  }

  void StopCapture() override {
    state_->queue->capturing = false;
    if (state_->input) waveInStop(state_->input);
    if (state_->inputEvent) SetEvent(state_->inputEvent);
    if (microphone_.joinable()) microphone_.join();
    if (state_->input) {
      // Reset after the worker exits so it cannot requeue a buffer after reset.
      // Reset also returns the final partial recording buffer for this turn.
      waveInReset(state_->input);
      for (auto& buffer : state_->inputBuffers) {
        if ((buffer.header.dwFlags & WHDR_DONE) && buffer.header.dwBytesRecorded) {
          state_->queue->Push(Event::Type::Microphone,
              std::string(buffer.bytes.data(), buffer.header.dwBytesRecorded));
          buffer.header.dwBytesRecorded = 0;
        }
        if (buffer.header.dwFlags & WHDR_PREPARED) waveInUnprepareHeader(state_->input, &buffer.header, sizeof(WAVEHDR));
      }
      waveInClose(state_->input);
      state_->input = nullptr;
    }
    if (state_->inputEvent) { CloseHandle(state_->inputEvent); state_->inputEvent = nullptr; }
  }

  std::vector<Event> Poll() override { return state_->queue->Poll(); }

  void Play(const std::string& pcm, float volume) override {
    if (!state_->output) {
      WAVEFORMATEX format{};
      format.wFormatTag = WAVE_FORMAT_PCM;
      format.nChannels = 1;
      format.nSamplesPerSec = 24000;
      format.wBitsPerSample = 16;
      format.nBlockAlign = 2;
      format.nAvgBytesPerSec = 48000;
      if (waveOutOpen(&state_->output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        state_->queue->Push(Event::Type::Error, "无法播放千问回复，请检查声音输出设备"); return;
      }
    }
    for (auto it = state_->outputBuffers.begin(); it != state_->outputBuffers.end();) {
      if ((*it)->header.dwFlags & WHDR_DONE) {
        waveOutUnprepareHeader(state_->output, &(*it)->header, sizeof(WAVEHDR));
        it = state_->outputBuffers.erase(it);
      } else ++it;
    }
    const DWORD gain = static_cast<DWORD>(std::clamp(volume, 0.0f, 1.0f) * 65535);
    waveOutSetVolume(state_->output, gain | (gain << 16));
    auto buffer = std::make_unique<OutputBuffer>();
    buffer->bytes = pcm;
    buffer->header.lpData = buffer->bytes.data();
    buffer->header.dwBufferLength = static_cast<DWORD>(pcm.size());
    if (waveOutPrepareHeader(state_->output, &buffer->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
      state_->queue->Push(Event::Type::Error, "无法准备千问回复音频"); return;
    }
    if (waveOutWrite(state_->output, &buffer->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
      waveOutUnprepareHeader(state_->output, &buffer->header, sizeof(WAVEHDR));
      state_->queue->Push(Event::Type::Error, "无法播放千问回复，请检查声音输出设备"); return;
    }
    state_->outputBuffers.push_back(std::move(buffer));
  }

  bool IsPlaying() const override {
    // Header completion also handles audio lengths that end between milliseconds.
    return std::any_of(state_->outputBuffers.begin(), state_->outputBuffers.end(),
        [](const auto& buffer) { return !(buffer->header.dwFlags & WHDR_DONE); });
  }

  void StopPlayback() override {
    if (state_->output) {
      waveOutReset(state_->output);
      for (const auto& buffer : state_->outputBuffers) waveOutUnprepareHeader(state_->output, &buffer->header, sizeof(WAVEHDR));
    }
    state_->outputBuffers.clear();
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
  std::shared_ptr<WinState> state_ = std::make_shared<WinState>();
  std::thread receiver_, sender_, microphone_;
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
bool SavePowerLiveRefreshToken(const std::string& profile, const std::string& key, std::string& error) {
  auto target = (L"JPet/PowerLive/" + Wide(profile));
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
  error = "无法保存 PowerLive 登录信息，请检查 Windows 凭据管理器";
  return false;
}

std::string LoadPowerLiveRefreshToken(const std::string& profile, std::string& error) {
  PCREDENTIALW credential = nullptr;
  if (!CredReadW((L"JPet/PowerLive/" + Wide(profile)).c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
    if (GetLastError() != ERROR_NOT_FOUND) error = "无法读取 PowerLive 登录信息，请检查 Windows 凭据管理器";
    return {};
  }
  const std::string key(reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize);
  CredFree(credential);
  return key;
}
}  // namespace Voice
