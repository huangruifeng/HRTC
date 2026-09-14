#include "ScreenCapture/windows/screen/screen_source_list_win.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hrtc {
namespace videocapturemodule {

namespace {

struct MonitorEnumContext {
  std::vector<ScreenSource>* sources;
};

BOOL CALLBACK MonitorEnumProc(HMONITOR monitor, HDC, LPRECT, LPARAM lparam) {
  auto* context = reinterpret_cast<MonitorEnumContext*>(lparam);
  MONITORINFOEXW info;
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info))
    return TRUE;

  ScreenSource source;
  source.type = ScreenSourceType::kScreen;
  source.sourceId = MakeScreenSourceId("screen", monitor);
  source.sourceName = WToUtf8(info.szDevice);
  source.width = info.rcMonitor.right - info.rcMonitor.left;
  source.height = info.rcMonitor.bottom - info.rcMonitor.top;
  source.windowId = reinterpret_cast<uint64_t>(monitor);
  context->sources->push_back(source);
  return TRUE;
}

struct WindowEnumContext {
  std::vector<ScreenSource>* sources;
};

BOOL CALLBACK WindowEnumProc(HWND hwnd, LPARAM lparam) {
  auto* context = reinterpret_cast<WindowEnumContext*>(lparam);
  // 过滤: 不可见 / 无标题 / 空客户区窗口
  if (!IsWindowVisible(hwnd))
    return TRUE;
  if (GetWindowTextLengthW(hwnd) == 0)
    return TRUE;

  RECT rect;
  if (!GetClientRect(hwnd, &rect))
    return TRUE;
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0)
    return TRUE;

  wchar_t title[256];
  const int titleLength = GetWindowTextW(hwnd, title, 256);

  ScreenSource source;
  source.type = ScreenSourceType::kWindow;
  source.sourceId = MakeScreenSourceId("window", hwnd);
  source.sourceName = WToUtf8(std::wstring(title, titleLength));
  source.width = width;
  source.height = height;
  source.windowId = reinterpret_cast<uint64_t>(hwnd);
  context->sources->push_back(source);
  return TRUE;
}

}  // namespace

std::string WToUtf8(const std::wstring& wide) {
  if (wide.empty())
    return std::string();
  const int length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr,
                                         0, nullptr, nullptr);
  if (length <= 0)
    return std::string();
  std::string utf8(length - 1, 0);
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], length, nullptr,
                      nullptr);
  return utf8;
}

std::string MakeScreenSourceId(const char* prefix, const void* handle) {
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "%s:%p", prefix, handle);
  return buffer;
}

bool ParseScreenSourceId(const std::string& sourceId, void** handle) {
  const char* colon = strchr(sourceId.c_str(), ':');
  if (!colon)
    return false;
  *handle = reinterpret_cast<void*>(strtoull(colon + 1, nullptr, 16));
  return *handle != nullptr;
}

std::vector<ScreenSource> EnumerateScreenSourcesWin() {
  std::vector<ScreenSource> sources;

  MonitorEnumContext monitorContext{&sources};
  EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc,
                      reinterpret_cast<LPARAM>(&monitorContext));

  WindowEnumContext windowContext{&sources};
  EnumWindows(WindowEnumProc, reinterpret_cast<LPARAM>(&windowContext));

  return sources;
}

}  // namespace videocapturemodule
}  // namespace hrtc
