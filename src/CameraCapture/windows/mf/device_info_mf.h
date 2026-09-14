#pragma once

#include "CameraCapture/VideoDeviceBase.h"
#include "CameraCapture/windows/mf/help_types_general.h"

#include <mfidl.h>

namespace hrtc {
namespace videocapturemodule {

template <class T> void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

// Media Foundation 视频设备信息
class DeviceInfoMF : public IDeviceInfo {
 public:

  // Factory function.
  static DeviceInfoMF* Create();

  DeviceInfoMF();
  virtual ~DeviceInfoMF();

  int32_t Init();
  uint32_t NumberOfDevices() override;

  int32_t GetDeviceName(uint32_t deviceNumber,
                        std::string& deviceName,
                        std::string& deviceUniqueId) override;

  int32_t NumberOfCapabilities(const std::string& deviceUniqueId) override;
  int32_t GetCapability(const std::string& deviceUniqueId,
                        uint32_t deviceCapabilityNumber,
                        VideoCapability& capability) override;

  int32_t GetWindowsCapability(int32_t capabilityIndex, VideoCapabilityWindowsMf& windowsCapability);
  IMFMediaSource* GetDeviceMediaSource(const std::string& deviceUniqueId);

protected:
    int32_t GetDeviceInfo(uint32_t deviceNumber,
                          char* deviceNameUTF8,
                          uint32_t deviceNameLength,
                          char* deviceUniqueIdUTF8,
                          uint32_t deviceUniqueIdUTF8Length,
                          char* productUniqueIdUTF8,
                          uint32_t productUniqueIdUTF8Length);
    /**
     * test if media source works;
     * error may occur under some occasions, if that happen, init should fail and let caller have a change to switch to a fallback solution
     */
    bool TestifyMediaSource();

private:
    bool TestifyMediaSourceImpl(IMFActivate* device);

    bool GetMediaSourceGuid(IMFActivate* device, std::wstring &guid);

    // 填充指定设备的能力列表, 返回能力数量
    int32_t CreateCapabilityMap(const std::string& deviceUniqueId);

private:
    IMFAttributes* _mfAttribute;
    std::vector<VideoCapabilityWindowsMf> _captureCapabilitiesWindows;
    bool _CoUninitializeIsRequired;

    // 能力缓存 (与 _captureCapabilitiesWindows 同步填充)
    std::vector<VideoCapability> _captureCapabilities;
    std::mutex _apiLock;
    std::string _lastUsedDeviceName;
};

}  // namespace videocapturemodule
}  // namespace hrtc
