#pragma once

#include <string>
#include <vector>

#include "ScreenCapture/ScreenCapture.h"

namespace hrtc {
namespace videocapturemodule {

// 枚举屏幕(显示器)与窗口采集源
std::vector<ScreenSource> EnumerateScreenSourcesWin();

// sourceId 编码/解析: "screen:<hex handle>" / "window:<hex handle>"
std::string MakeScreenSourceId(const char* prefix, const void* handle);
bool ParseScreenSourceId(const std::string& sourceId, void** handle);

// 宽字符转 UTF-8
std::string WToUtf8(const std::wstring& wide);

}  // namespace videocapturemodule
}  // namespace hrtc
