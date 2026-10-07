#include "VoiceDesktop.hpp"
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <vector>

namespace Voice {
namespace {
struct Monitor { RECT rect; bool primary; };
BOOL CALLBACK Collect(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
  MONITORINFO info{sizeof(MONITORINFO)};
  if (GetMonitorInfoW(monitor, &info)) reinterpret_cast<std::vector<Monitor>*>(context)->push_back({info.rcMonitor, (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
  return TRUE;
}
}
bool CaptureDesktop(int display, DesktopImage& image, std::string& error) {
  std::vector<Monitor> monitors;
  EnumDisplayMonitors(nullptr, nullptr, Collect, reinterpret_cast<LPARAM>(&monitors));
  std::stable_sort(monitors.begin(), monitors.end(), [](const auto& a, const auto& b) {
    if (a.primary != b.primary) return a.primary;
    return a.rect.left != b.rect.left ? a.rect.left < b.rect.left : a.rect.top < b.rect.top;
  });
  image.displayCount = static_cast<int>(monitors.size());
  if (display < 0 || display >= image.displayCount) { error = "指定显示器不存在；display=0为主屏"; return false; }
  const RECT rect = monitors[display].rect;
  const int width = rect.right - rect.left, height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0) { error = "显示器尺寸无效"; return false; }
  const double scale = std::min(1.0, 1920.0 / std::max(width, height));
  image.width = std::max(1, static_cast<int>(width * scale));
  image.height = std::max(1, static_cast<int>(height * scale));
  Gdiplus::GdiplusStartupInput input;
  ULONG_PTR token = 0;
  if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) { error = "无法初始化截图编码"; return false; }
  bool success = false;
  HDC screen = GetDC(nullptr), memory = screen ? CreateCompatibleDC(screen) : nullptr;
  HBITMAP bitmap = screen ? CreateCompatibleBitmap(screen, image.width, image.height) : nullptr;
  if (memory && bitmap) {
    auto previous = SelectObject(memory, bitmap);
    SetStretchBltMode(memory, HALFTONE);
    const auto copied = StretchBlt(memory, 0, 0, image.width, image.height, screen, rect.left, rect.top, width, height, SRCCOPY | CAPTUREBLT);
    SelectObject(memory, previous);
    if (copied) {
      Gdiplus::Bitmap captured(bitmap, nullptr);
      UINT count = 0, bytes = 0;
      Gdiplus::GetImageEncodersSize(&count, &bytes);
      std::vector<unsigned char> storage(bytes);
      auto codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
      if (bytes && Gdiplus::GetImageEncoders(count, bytes, codecs) == Gdiplus::Ok) {
        for (UINT i = 0; i < count; ++i) if (wcscmp(codecs[i].MimeType, L"image/jpeg") == 0) {
          for (ULONG quality : {80UL, 55UL, 30UL}) {
            IStream* stream = nullptr;
            if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) break;
            Gdiplus::EncoderParameters parameters;
            parameters.Count = 1;
            parameters.Parameter[0] = {Gdiplus::EncoderQuality, 1, Gdiplus::EncoderParameterValueTypeLong, &quality};
            if (captured.Save(stream, &codecs[i].Clsid, &parameters) == Gdiplus::Ok) {
              STATSTG stat{};
              if (SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart > 0 && stat.cbSize.QuadPart <= 512 * 1024) {
                std::string data(static_cast<size_t>(stat.cbSize.QuadPart), '\0');
                LARGE_INTEGER start{};
                ULONG read = 0;
                if (SUCCEEDED(stream->Seek(start, STREAM_SEEK_SET, nullptr)) && SUCCEEDED(stream->Read(data.data(), static_cast<ULONG>(data.size()), &read)) && read == data.size()) {
                  image.jpeg = std::move(data); success = true;
                }
              }
            }
            stream->Release();
            if (success) break;
          }
          break;
        }
      }
    }
  }
  if (bitmap) DeleteObject(bitmap);
  if (memory) DeleteDC(memory);
  if (screen) ReleaseDC(nullptr, screen);
  Gdiplus::GdiplusShutdown(token);
  if (!success) error = "桌面截图失败，当前桌面可能不允许捕获";
  return success;
}
} // namespace Voice
