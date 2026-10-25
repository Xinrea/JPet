#include "VoicePlatform.hpp"
#include "VoiceEcho.hpp"
#include "VoiceEventQueue.hpp"
#include "VoiceIndicatorStyle.hpp"

#include <windows.h>
#include <wincred.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <algorithm>
#include <chrono>
#include <cmath>
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

std::string Narrow(const wchar_t* value) {
  const int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (length <= 1) return {};
  std::string out(length - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), length, nullptr, nullptr);
  return out;
}

using Microsoft::WRL::ComPtr;
using AudioClock = std::chrono::steady_clock;

struct ComScope {
  const bool owned = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
  ~ComScope() { if (owned) CoUninitialize(); }
};

std::string DeviceName(IMMDevice* device) {
  ComPtr<IPropertyStore> properties;
  PROPVARIANT value;
  PropVariantInit(&value);
  std::string name;
  if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) &&
      SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
    name = Narrow(value.pwszVal);
  PropVariantClear(&value);
  return name;
}

// Opens endpoint `id` in shared, event-driven mode: the default endpoint when
// `id` is empty or the device is no longer active. The audio engine converts
// between the device mix format and 16-bit mono at `rate`, so neither
// direction needs its own resampler. `description` names the opened device.
HRESULT OpenStream(EDataFlow flow, const std::string& id, DWORD rate, REFERENCE_TIME duration, HANDLE event,
                   ComPtr<IAudioClient>& client, std::string& description) {
  ComPtr<IMMDeviceEnumerator> enumerator;
  ComPtr<IMMDevice> device;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
  DWORD active = 0;
  if (SUCCEEDED(hr) && !id.empty() && (FAILED(enumerator->GetDevice(Wide(id).c_str(), &device)) ||
      FAILED(device->GetState(&active)) || active != DEVICE_STATE_ACTIVE))
    device.Reset();
  const bool fallback = !id.empty() && !device;
  if (SUCCEEDED(hr) && !device) hr = enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device);
  if (SUCCEEDED(hr)) description = "\"" + DeviceName(device.Get()) + "\"" + (fallback ? " (saved device missing, default)" : "");
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

struct WinState {
  std::shared_ptr<EventQueue> queue = std::make_shared<EventQueue>();
  // Null when JPET_VOICE_AEC=0, for comparing raw microphone input.
  std::shared_ptr<EchoCanceller> echo;
  std::atomic<bool> captureStop{false}, captureRunning{false};
  std::atomic<int> renderLatencyMs{0};
  // Endpoint ids chosen in the voice settings (empty: default), read when a stream opens.
  std::mutex deviceMutex;
  std::string inputDevice, outputDevice;
  std::string Device(bool input) {
    std::lock_guard<std::mutex> lock(deviceMutex);
    return input ? inputDevice : outputDevice;
  }
  // Reply PCM (24 kHz mono) waiting for the render thread, guarded by renderMutex.
  std::mutex renderMutex;
  std::deque<int16_t> pending;
  float volume = 1;
  AudioClock::time_point audibleUntil{};
  std::atomic<bool> renderStop{false}, renderFailed{false};
  std::atomic<uint64_t> played{0};  // Reply samples written to the device since StopPlayback.
  HWND indicator = nullptr;
  bool indicatorError = false;
  std::wstring indicatorText, indicatorShortcutHint;
  int indicatorPulseFrame = -1;
  int indicatorHeight = Indicator::MinHeight;
  HFONT font = nullptr;
  HFONT titleFont = nullptr;
  HFONT shortcutFont = nullptr;
};

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
    const int height = state->indicatorHeight;
    SetWindowExtEx(dc, Width, height, nullptr);
    SetViewportExtEx(dc, rect.right, rect.bottom, nullptr);
    rect = {0, 0, Width, height};
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
    RECT title{14, 5, 150, 24};
    DrawTextW(dc, L"JPet · 语音对话", -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SelectObject(dc, GetStockObject(NULL_PEN));
    SetDCBrushColor(dc, RGB(255, 255, 255));
    RoundRect(dc, 150, 5, Width - 14, 23, 14, 14);
    SetTextColor(dc, color(state->indicatorError ? Error : Ink));
    SelectObject(dc, state->shortcutFont);
    RECT shortcut{150, 5, Width - 14, 23};
    DrawTextW(dc, state->indicatorShortcutHint.c_str(), -1, &shortcut,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if (state->indicatorPulseFrame >= 0) {
      // Expand outward while fading into the body background over one pulse.
      const int frame = state->indicatorPulseFrame;
      const int radius = 18 + (5 * frame + 15) / 30;
      const auto fade = [frame](int a, int b) { return a + (b - a) * frame / 30; };
      HPEN pulsePen = CreatePen(PS_SOLID, 2,
          RGB(fade(Accent.red, Background.red), fade(Accent.green, Background.green),
              fade(Accent.blue, Background.blue)));
      const auto previousPen = SelectObject(dc, pulsePen);
      SelectObject(dc, GetStockObject(NULL_BRUSH));
      Ellipse(dc, 31 - radius, 56 - radius, 31 + radius, 56 + radius);
      SelectObject(dc, previousPen);
      DeleteObject(pulsePen);
      SelectObject(dc, GetStockObject(DC_BRUSH));
    }
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
    RECT footer{0, height - 5, Width, height};
    FillRect(dc, &footer, brush);
    DeleteObject(brush);
    HPEN border = CreatePen(PS_SOLID, 1, color(state->indicatorError ? ErrorTop : Border));
    SelectObject(dc, border);
    RoundRect(dc, 0, 0, Width, height, Radius * 2, Radius * 2);
    SelectObject(dc, oldPen);
    DeleteObject(border);
    SelectObject(dc, state->font);
    SetTextColor(dc, color(state->indicatorError ? Error : Ink));
    RECT body{BodyLeft, BodyTop, Width - BodyRight, height - BodyBottom};
    DrawTextW(dc, state->indicatorText.c_str(), -1, &body,
        DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
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
    StopCapture(); StopPlayback(); StopRender();
    if (state_->indicator) DestroyWindow(state_->indicator);
    if (state_->font) DeleteObject(state_->font);
    if (state_->titleFont) DeleteObject(state_->titleFont);
    if (state_->shortcutFont) DeleteObject(state_->shortcutFont);
  }
  bool KeyHeld(const Key& key) const override {
    constexpr auto vk = [](const char* code) { return KeyCode(code, false); };
    static_assert(vk("Control") == VK_CONTROL && vk("ControlLeft") == VK_LCONTROL && vk("ControlRight") == VK_RCONTROL &&
        vk("Alt") == VK_MENU && vk("AltLeft") == VK_LMENU && vk("AltRight") == VK_RMENU && vk("Shift") == VK_SHIFT &&
        vk("ShiftLeft") == VK_LSHIFT && vk("ShiftRight") == VK_RSHIFT && vk("MetaLeft") == VK_LWIN && vk("MetaRight") == VK_RWIN &&
        vk("F1") == VK_F1 && vk("F12") == VK_F12 && vk("F13") == VK_F13 && vk("F24") == VK_F24 &&
        vk("Pause") == VK_PAUSE && vk("ScrollLock") == VK_SCROLL && vk("KeyA") == 'A' && vk("Digit0") == '0' &&
        vk("Backquote") == VK_OEM_3 && vk("Minus") == VK_OEM_MINUS && vk("Equal") == VK_OEM_PLUS &&
        vk("BracketLeft") == VK_OEM_4 && vk("BracketRight") == VK_OEM_6 && vk("Backslash") == VK_OEM_5 &&
        vk("Semicolon") == VK_OEM_1 && vk("Quote") == VK_OEM_7 && vk("Comma") == VK_OEM_COMMA &&
        vk("Period") == VK_OEM_PERIOD && vk("Slash") == VK_OEM_2 && vk("Space") == VK_SPACE && vk("Tab") == VK_TAB &&
        vk("Enter") == VK_RETURN && vk("Backspace") == VK_BACK && vk("Escape") == VK_ESCAPE && vk("Insert") == VK_INSERT &&
        vk("Delete") == VK_DELETE && vk("Home") == VK_HOME && vk("End") == VK_END && vk("PageUp") == VK_PRIOR &&
        vk("PageDown") == VK_NEXT && vk("ArrowLeft") == VK_LEFT && vk("ArrowUp") == VK_UP && vk("ArrowRight") == VK_RIGHT &&
        vk("ArrowDown") == VK_DOWN && vk("Numpad0") == VK_NUMPAD0 && vk("Numpad9") == VK_NUMPAD9 &&
        vk("NumpadMultiply") == VK_MULTIPLY && vk("NumpadAdd") == VK_ADD && vk("NumpadSubtract") == VK_SUBTRACT &&
        vk("NumpadDecimal") == VK_DECIMAL && vk("NumpadDivide") == VK_DIVIDE,
        "shortcut keys match the Windows virtual keys");
    return (GetAsyncKeyState(static_cast<int>(key.windows)) & 0x8000) != 0;
  }
  bool ShortcutReadable(const Shortcut&, bool) override { return true; }

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

  void ShowIndicator(GLFWwindow* window, const Indicator::Content& content) override {
    if (content.text.empty()) {
      if (state_->indicator) ShowWindow(state_->indicator, SW_HIDE);
      state_->indicatorPulseFrame = -1;
      return;
    }
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
          type.lpszClassName, L"", WS_POPUP, 0, 0, Indicator::Width, Indicator::MinHeight, nullptr, nullptr, type.hInstance, state_.get());
      SetWindowRgn(state_->indicator, CreateRoundRectRgn(0, 0, Indicator::Width, Indicator::MinHeight,
          Indicator::Radius * 2, Indicator::Radius * 2), TRUE);
    }
    const auto wideText = Wide(content.text);
    const auto wideHint = Wide(content.shortcutHint);
    if (state_->indicatorText != wideText) {
      // Measure with the same font, width and wrapping flags used by WM_PAINT.
      HDC dc = GetDC(state_->indicator);
      const auto previousFont = SelectObject(dc, state_->font);
      RECT body{0, 0, Indicator::BodyWidth, 0};
      DrawTextW(dc, wideText.c_str(), -1, &body, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
      state_->indicatorHeight = Indicator::HeightForText(body.bottom - body.top);
      SelectObject(dc, previousFont);
      ReleaseDC(state_->indicator, dc);
    }
    const int frame = content.microphoneOn && !content.error ? Indicator::PulseFrame() : -1;
    const bool changed = state_->indicatorError != content.error || state_->indicatorText != wideText ||
        state_->indicatorShortcutHint != wideHint || state_->indicatorPulseFrame != frame;
    state_->indicatorError = content.error;
    state_->indicatorText = wideText;
    state_->indicatorShortcutHint = wideHint;
    state_->indicatorPulseFrame = frame;
    RECT pet, screen;
    GetWindowRect(glfwGetWin32Window(window), &pet);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromRect(&pet, MONITOR_DEFAULTTONEAREST), &monitor);
    screen = monitor.rcWork;
    const UINT dpi = GetDpiForWindow(glfwGetWin32Window(window));
    const int width = MulDiv(Indicator::Width, dpi, 96), height = MulDiv(state_->indicatorHeight, dpi, 96);
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

  void SetDevices(const std::string& input, const std::string& output) override {
    bool outputChanged;
    {
      std::lock_guard<std::mutex> lock(state_->deviceMutex);
      outputChanged = state_->outputDevice != output;
      state_->inputDevice = input;
      state_->outputDevice = output;
    }
    // The microphone reopens on every StartCapture; the speaker stream stays
    // open between replies, so close it to reopen on the new device.
    if (outputChanged && !microphone_.joinable()) {
      StopRender();
      StopPlayback();
    }
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
    std::string device;
    HRESULT hr = event ? OpenStream(eCapture, state->Device(true), EchoCanceller::CaptureRate, Buffer, event, client, device)
                       : E_OUTOFMEMORY;
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
        std::to_string(captureLatency) + " aec=" + (state->echo ? "on" : "off") + " device=" + device);
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
    std::string device;
    HRESULT hr = event ? OpenStream(eRender, state->Device(false), Rate, Buffer, event, client, device) : E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) hr = client->GetBufferSize(&bufferFrames);
    if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&render));
    if (SUCCEEDED(hr)) hr = client->Start();
    if (SUCCEEDED(hr)) {
      state->renderLatencyMs = LatencyMs(client.Get(), Buffer);
      state->queue->Push(Event::Type::Diagnostic, "[Voice] Render wasapi rate=24000 latency_ms=" +
          std::to_string(state->renderLatencyMs.load()) + " device=" + device);
    }
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
  std::thread microphone_, renderer_;
  AudioClock::time_point lastAudio_{};
};
// Legacy; only removed.
std::wstring BailianTarget(const std::string& profile) { return L"JPet/Qwen/" + Wide(profile); }
// Chat service keys: the third-party key keeps the former custom endpoint's credential.
std::wstring ChatTarget(const std::string& profile, const std::string& provider) {
  return L"JPet/Chat/" + (provider == "thirdparty" ? std::wstring{} : Wide(provider) + L"/") + Wide(profile);
}

// An empty key removes the saved one.
bool SaveSecret(std::wstring target, const std::string& key) {
  if (key.empty()) return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
  CREDENTIALW credential{};
  credential.Type = CRED_TYPE_GENERIC;
  credential.TargetName = target.data();
  credential.CredentialBlobSize = static_cast<DWORD>(key.size());
  credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(key.data()));
  credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
  return CredWriteW(&credential, 0);
}

// Empty when no key is saved; false only when the credential cannot be read.
bool LoadSecret(const std::wstring& target, std::string& key) {
  PCREDENTIALW credential = nullptr;
  if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) return GetLastError() == ERROR_NOT_FOUND;
  key.assign(reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize);
  CredFree(credential);
  return true;
}
}  // namespace

std::unique_ptr<Platform> MakePlatform() { return std::make_unique<WinPlatform>(); }

AudioDevices ListAudioDevices() {
  ComScope com;
  AudioDevices devices;
  ComPtr<IMMDeviceEnumerator> enumerator;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) return devices;
  for (const auto& [flow, list] : {std::pair{eCapture, &devices.inputs}, std::pair{eRender, &devices.outputs}}) {
    ComPtr<IMMDeviceCollection> collection;
    UINT count = 0;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection)) ||
        FAILED(collection->GetCount(&count)))
      continue;
    for (UINT i = 0; i < count; ++i) {
      ComPtr<IMMDevice> device;
      LPWSTR id = nullptr;
      if (FAILED(collection->Item(i, &device)) || FAILED(device->GetId(&id))) continue;
      list->push_back({Narrow(id), DeviceName(device.Get())});
      CoTaskMemFree(id);
    }
  }
  return devices;
}

void RemoveLegacyApiKey(const std::string& profile) { SaveSecret(BailianTarget(profile), ""); }

bool SaveChatApiKey(const std::string& profile, const std::string& provider, const std::string& key, std::string& error) {
  if (SaveSecret(ChatTarget(profile, provider), key)) return true;
  error = "无法保存大模型 API Key，请检查 Windows 凭据管理器";
  return false;
}

std::string LoadChatApiKey(const std::string& profile, const std::string& provider, std::string& error) {
  std::string key;
  if (!LoadSecret(ChatTarget(profile, provider), key)) error = "无法读取大模型 API Key，请检查 Windows 凭据管理器";
  return key;
}

}  // namespace Voice
