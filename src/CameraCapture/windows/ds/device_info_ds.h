#ifndef MODULES_VIDEO_CAPTURE_MAIN_SOURCE_WINDOWS_DEVICE_INFO_DS_H_
#define MODULES_VIDEO_CAPTURE_MAIN_SOURCE_WINDOWS_DEVICE_INFO_DS_H_

#include <dshow.h>

#include "CameraCapture/VideoDeviceBase.h"

namespace hrtc {
namespace videocapturemodule {
struct VideoCapabilityWindows : public VideoCapability {
  uint32_t directShowCapabilityIndex;
  bool supportFrameRateControl;
  VideoCapabilityWindows() {
    directShowCapabilityIndex = 0;
    supportFrameRateControl = false;
  }
};

class DeviceInfoDS : public IDeviceInfo {
 public:
  // Factory function.
  static DeviceInfoDS* Create();

  DeviceInfoDS();
  ~DeviceInfoDS() override;

  int32_t Init();
  uint32_t NumberOfDevices() override;

  /*
   * Returns the available capture devices.
   */
  int32_t GetDeviceName(uint32_t deviceNumber,
                        std::string& deviceName,
                        std::string& deviceUniqueId) override;

  int32_t NumberOfCapabilities(const std::string& deviceUniqueId) override;
  int32_t GetCapability(const std::string& deviceUniqueId,
                        uint32_t deviceCapabilityNumber,
                        VideoCapability& capability) override;

  // Windows specific

  /* Gets a capture device filter
   The user of this API is responsible for releasing the filter when it not
   needed.
   */
  IBaseFilter* GetDeviceFilter(const char* deviceUniqueIdUTF8,
                               char* productUniqueIdUTF8 = NULL,
                               uint32_t productUniqueIdUTF8Length = 0);

  int32_t GetWindowsCapability(
      int32_t capabilityIndex,
      VideoCapabilityWindows& windowsCapability);

  static void GetProductId(const char* devicePath,
                           char* productUniqueIdUTF8,
                           uint32_t productUniqueIdUTF8Length);

 protected:
  int32_t GetDeviceInfo(uint32_t deviceNumber,
                        char* deviceNameUTF8,
                        uint32_t deviceNameLength,
                        char* deviceUniqueIdUTF8,
                        uint32_t deviceUniqueIdUTF8Length,
                        char* productUniqueIdUTF8,
                        uint32_t productUniqueIdUTF8Length);

 private:
  // 填充指定设备的能力列表, 返回能力数量
  int32_t CreateCapabilityMap(const std::string& deviceUniqueId);

  ICreateDevEnum* _dsDevEnum;
  IEnumMoniker* _dsMonikerDevEnum;
  bool _CoUninitializeIsRequired;
  std::vector<VideoCapabilityWindows> _captureCapabilitiesWindows;

  // 能力缓存 (与 _captureCapabilitiesWindows 同步填充)
  std::vector<VideoCapability> _captureCapabilities;
  std::mutex _apiLock;
  std::string _lastUsedDeviceName;
};
}  // namespace videocapturemodule
}  // namespace hrtc
#endif  // MODULES_VIDEO_CAPTURE_MAIN_SOURCE_WINDOWS_DEVICE_INFO_DS_H_
