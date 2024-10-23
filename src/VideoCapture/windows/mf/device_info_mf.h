#pragma once

#include "VideoCapture/DeviceInfoImpl.h"
#include "VideoCapture/windows/mf/help_types_general.h"

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

// Provides video capture device information using the Media Foundation API.
class DeviceInfoMF : public DeviceInfoImpl {
 public:

  // Factory function.
  static DeviceInfoMF* Create();

  DeviceInfoMF();
  virtual ~DeviceInfoMF();

  int32_t Init();
  virtual uint32_t NumberOfDevices();

  virtual int32_t GetDeviceName(uint32_t deviceNumber, char* deviceNameUTF8,
                                uint32_t deviceNameLength,
                                char* deviceUniqueIdUTF8,
                                uint32_t deviceUniqueIdUTF8Length,
                                char* productUniqueIdUTF8,
                                uint32_t productUniqueIdUTF8Length);

  int32_t DisplayCaptureSettingsDialogBox(
      const char* deviceUniqueIdUTF8, const char* dialogTitleUTF8,
      void* parentWindow, uint32_t positionX, uint32_t positionY);

  virtual int32_t CreateCapabilityMap(const char* deviceUniqueIdUTF8);
  int32_t GetWindowsCapability(int32_t capabilityIndex, VideoCaptureCapabilityWindowsMf& windowsCapability);
  IMFMediaSource* GetDeviceMediaSource(const char* deviceUniqueIdUTF8);

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

private:
    IMFAttributes* _mfAttribute;
    std::vector<VideoCaptureCapabilityWindowsMf> _captureCapabilitiesWindows;
    bool _CoUninitializeIsRequired;
};

}  // namespace videocapturemodule
}  // namespace webrtc
