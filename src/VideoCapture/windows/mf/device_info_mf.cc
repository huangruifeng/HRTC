
#include <windows.h>
#include <cfgmgr32.h>  // for MAX_DEVICE_ID_LEN, CM_Get_Parent and CM_Get_Device_ID
#include <devguid.h>  // for GUID_DEVCLASS_CDROM etc
#include <mmdeviceapi.h>// for StringFromGUID2
#include <devpkey.h>
#include <setupapi.h>
#define INITGUID
#include <tchar.h>
#include <mfapi.h>
#include <codecvt>
#include <mutex>
#include <map>
#include "VideoCapture/windows/mf/device_info_mf.h"
#pragma comment(lib, "setupapi.lib")
#ifdef DEFINE_DEVPROPKEY
#undef DEFINE_DEVPROPKEY
#endif
#ifdef INITGUID

#define DEFINE_DEVPROPKEY(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, pid) EXTERN_C const DEVPROPKEY DECLSPEC_SELECTANY name = { { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }, pid }
#else
#define DEFINE_DEVPROPKEY(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, pid) EXTERN_C const DEVPROPKEY name
#endif // INITGUID

// include DEVPKEY_Device_BusReportedDeviceDesc from WinDDK\7600.16385.1\inc\api\devpkey.h
//DEFINE_DEVPROPKEY(DEVPKEY_Device_ContainerId, 0x8c7ed206, 0x3f8a, 0x4827, 0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c, 2);     // DEVPROP_TYPE_GUID
//DEFINE_DEVPROPKEY(DEVPKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 14);    // DEVPROP_TYPE_STRING

//save testified source with key value pair of device id and result
std::map<std::wstring, bool> Testified_MediaSources_Map;
std::mutex MapCs;

#define CAPTURE_DEVICE_EXCEPTION(err_code, err_msg)                       \
  if (_exceptionObserver) {                                               \
    std::string ob_err_msg = "";                                          \
    if (UINT64_MAX == err_code) {                                         \
      ob_err_msg = err_msg;                                               \
    } else {                                                              \
      ob_err_msg =                                                        \
          err_msg + std::string(", hresult=") + std::to_string(err_code); \
    }                                                                     \
    _exceptionObserver->onCaptureDeviceException("", "", "", err_code,    \
                                                 ob_err_msg);             \
  }

namespace hrtc {
namespace videocapturemodule {

// static
DeviceInfoMF* DeviceInfoMF::Create() {
  DeviceInfoMF* mfInfo = new DeviceInfoMF();
  if (!mfInfo || mfInfo->Init() != 0) {
    //LOG(LS_ERROR)<<"Failed to create media foundation device info";
    delete mfInfo;
    mfInfo = NULL;
  }
  return mfInfo;
}

DeviceInfoMF::DeviceInfoMF() : _mfAttribute(NULL) ,_CoUninitializeIsRequired(true){
  HRESULT hr = CoInitializeEx(
      NULL, COINIT_MULTITHREADED);  // Use COINIT_MULTITHREADED since Voice
                                    // Engine uses COINIT_MULTITHREADED
  if (FAILED(hr)) {
    // Avoid calling CoUninitialize() since CoInitializeEx() failed.
    _CoUninitializeIsRequired = FALSE;

    if (hr == RPC_E_CHANGED_MODE) {
      // Calling thread has already initialized COM to be used in a
      // single-threaded apartment (STA). We are then prevented from using STA.
      // Details: hr = 0x80010106 <=> "Cannot change thread mode after it is
      // set".
      //
      // LOG(LS_INFO) << __FUNCTION__
      //              << ": CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)"
      //              << " => RPC_E_CHANGED_MODE, error 0x" << std::hex << hr;
    }
  }
}

DeviceInfoMF::~DeviceInfoMF() {
  SafeRelease(&_mfAttribute);
  if (_CoUninitializeIsRequired) {
    CoUninitialize();
  }
}

int32_t DeviceInfoMF::Init() {
  HRESULT hr = MFCreateAttributes(&_mfAttribute, 1);
  if (FAILED(hr)) {
    //CAPTURE_DEVICE_EXCEPTION(hr, "Failed on MFCreateAttributes() during DeviceInfoMF::Init()")
    //LOG(LS_INFO) << "Failed to create MFAttributes, error 0x" << std::hex << hr;
    return -1;
  }

  hr = _mfAttribute->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                             MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (FAILED(hr)) {
    //CAPTURE_DEVICE_EXCEPTION(hr, "Failed on IMFAttributes::SetGUID() during DeviceInfoMF::Init()")
    //LOG(LS_INFO) << "Failed to Set MFAttributes GUID, error 0x" << std::hex
    //             << hr;
    return -1;
  }
  return TestifyMediaSource() ? 0 : -1;
}

uint32_t DeviceInfoMF::NumberOfDevices() {
  std::lock_guard<std::mutex> lock(_apiLock);
  auto deviceCount = GetDeviceInfo(0, 0, 0, 0, 0, 0, 0);
  //LOG(LS_INFO)<<"get capture device count: "<<deviceCount;
  return deviceCount;
}

int32_t DeviceInfoMF::GetDeviceName(uint32_t deviceNumber,
                                    char* deviceNameUTF8,
                                    uint32_t deviceNameLength,
                                    char* deviceUniqueIdUTF8,
                                    uint32_t deviceUniqueIdUTF8Length,
                                    char* productUniqueIdUTF8,
                                    uint32_t productUniqueIdUTF8Length) {
  std::lock_guard<std::mutex> lock(_apiLock);
  const int32_t result = GetDeviceInfo(
      deviceNumber, deviceNameUTF8, deviceNameLength, deviceUniqueIdUTF8,
      deviceUniqueIdUTF8Length, productUniqueIdUTF8, productUniqueIdUTF8Length);
  return result > (int32_t)deviceNumber ? 0 : -1;
}

int32_t DeviceInfoMF::DisplayCaptureSettingsDialogBox(
    const char* deviceUniqueIdUTF8,
    const char* dialogTitleUTF8,
    void* parentWindow,
    uint32_t positionX,
    uint32_t positionY) {
  return -1;
}

int32_t DeviceInfoMF::GetDeviceInfo(uint32_t deviceNumber,
                                    char* deviceNameUTF8,
                                    uint32_t deviceNameLength,
                                    char* deviceUniqueIdUTF8,
                                    uint32_t deviceUniqueIdUTF8Length,
                                    char* productUniqueIdUTF8,
                                    uint32_t productUniqueIdUTF8Length) {
  IMFActivate** ppDevices = NULL;
  UINT32 count;
  int index = 0;
  HRESULT hr = MFEnumDeviceSources(_mfAttribute, &ppDevices, &count);
  if (FAILED(hr))
  {
    //LOG(LS_ERROR)<<"error enumerate device source when getting device info, error:"<<hr;
    goto done;
  }
  //LOG(LS_INFO)<<"Enumerated capture device count: "<<count;
  for (int i = 0; i < count; i++) {
    WCHAR* name = NULL;
    UINT32 nameLength;
    hr = ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                                          &name, &nameLength);
    if (SUCCEEDED(hr)) {
      // ignore all VFW drivers
      if ((wcsstr(name, (L"(VFW)")) == NULL) &&
          (_wcsnicmp(name, (L"Google Camera Adapter"), 21) != 0)) {
        if (index == static_cast<int>(deviceNumber)) {
          int convResult = 0;
          // set friendly name
          if (deviceNameLength > 0) {
            convResult =
                WideCharToMultiByte(CP_UTF8, 0, name, -1, (char*)deviceNameUTF8,
                                    deviceNameLength, NULL, NULL);
            if (convResult == 0) {
              //LOG(LS_INFO) << "Failed to convert device name to UTF8, "
              //             << "error = " << GetLastError();
              index = 0;
              goto done;
            }
          }
          if (deviceUniqueIdUTF8Length > 0) {
            hr = ppDevices[i]->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &name,
                &nameLength);
            if (FAILED(hr)) {
              strncpy_s((char*)deviceUniqueIdUTF8, deviceUniqueIdUTF8Length,
                        (char*)deviceNameUTF8, convResult);
              //LOG(LS_INFO) << "Failed to get "
              //             << "deviceUniqueIdUTF8 using "
              //             << "deviceNameUTF8";
            } else {
              convResult = WideCharToMultiByte(
                  CP_UTF8, 0, name, -1, (char*)deviceUniqueIdUTF8, deviceUniqueIdUTF8Length, NULL, NULL);
              if (convResult == 0) {
                //LOG(LS_INFO) << "Failed to convert device unique id "
                //             << "to UTF8, error = " << GetLastError();
                index = 0;
                goto done;
              }
            }
          }
          if (deviceUniqueIdUTF8 && deviceNameUTF8) {
            //LOG(LS_INFO) << "get device info of index : " << index
            //             << ",camera guid :" << std::string(deviceUniqueIdUTF8)
            //             << ",name" << std::string(deviceNameUTF8);
          }
        }
        ++index;
      }
    }
    else{
      //LOG(LS_WARNING)<<"error getting capture device friendly name, error"<<hr;
    }
  }

done:
  for (DWORD i = 0; i < count; i++) {
    SafeRelease(&ppDevices[i]);
  }
  CoTaskMemFree(ppDevices);
  return index;
}

int32_t DeviceInfoMF::CreateCapabilityMap(const char* deviceUniqueIdUTF8) {
  // Reset old capability list
  _captureCapabilities.clear();

  const int32_t deviceUniqueIdUTF8Length =
      (int32_t)strlen((char*)deviceUniqueIdUTF8);
  if (deviceUniqueIdUTF8Length > kVideoCaptureUniqueNameLength) {
    //LOG(LS_INFO) << "Device name too long";
    return -1;
  }
  //LOG(LS_INFO) << "CreateCapabilityMap called for device "
  //             << deviceUniqueIdUTF8;

  IMFMediaSource* source = DeviceInfoMF::GetDeviceMediaSource(deviceUniqueIdUTF8);
  if (!source)
    return -1;

  IMFPresentationDescriptor* mfPresentDesc = NULL;
  IMFStreamDescriptor* mfStreamDesc = NULL;
  IMFMediaTypeHandler* mfHandler = NULL;

  HRESULT hr = source->CreatePresentationDescriptor(&mfPresentDesc);
  if (FAILED(hr)) {
    //CAPTURE_DEVICE_EXCEPTION(hr, "Failed on CreatePresentationDescriptor() during DeviceInfoMF::CreateCapabilityMap()")
    //LOG(LS_WARNING) << "Call CreatePresentationDescriptor failed for "
    //                << deviceUniqueIdUTF8 << " error: " << GetLastError();
    goto done;
  }

  // for video capture device, it should be only one stream
  BOOL pSelected;
  hr = mfPresentDesc->GetStreamDescriptorByIndex(0, &pSelected, &mfStreamDesc);
  if (FAILED(hr)) {
    //CAPTURE_DEVICE_EXCEPTION(hr, "Failed on GetStreamDescriptorByIndex() during DeviceInfoMF::CreateCapabilityMap()")
    //LOG(LS_WARNING) << "Call GetStreamDescriptorByIndex failed for "
    //                << deviceUniqueIdUTF8 << " error: " << GetLastError();
    goto done;
  }

  hr = mfStreamDesc->GetMediaTypeHandler(&mfHandler);
  if (FAILED(hr)) {
    //CAPTURE_DEVICE_EXCEPTION(hr, "Failed on GetMediaTypeHandler() during DeviceInfoMF::CreateCapabilityMap()")
    //LOG(LS_WARNING) << "Call GetMediaTypeHandler failed for "
    //                << deviceUniqueIdUTF8 << " error: " << GetLastError();
    goto done;
  }

  DWORD count;
  hr = mfHandler->GetMediaTypeCount(&count);
  if (FAILED(hr)) {
    //CAPTURE_DEVICE_EXCEPTION(hr, "Failed on GetMediaTypeCount() during DeviceInfoMF::CreateCapabilityMap()")
    //LOG(LS_WARNING) << "Call GetMediaTypeCount failed for "
    //                << deviceUniqueIdUTF8 << " error: " << GetLastError();
    goto done;
  }

  for (DWORD i = 0; i < count; i++) {
    IMFMediaType* mfType = NULL;
    hr = mfHandler->GetMediaTypeByIndex(i, &mfType);
    if (SUCCEEDED(hr)) {
      GUID majorType = GUID_NULL;
      hr = mfType->GetMajorType(&majorType);
      if (SUCCEEDED(hr) && majorType == MFMediaType_Video) {
        VideoCaptureCapabilityWindowsMf capability;
        // get framesize
        UINT32 width = 0;
        UINT32 height = 0;
        MFGetAttributeSize(mfType, MF_MT_FRAME_SIZE, &width, &height);
        capability.width = width;
        capability.height = height;
        capability.capabilityIndex = i;

        // get maxFps
        UINT32 punNumerator = 0;
        UINT32 punDenominator = 0;
        hr = MFGetAttributeRatio(mfType, MF_MT_FRAME_RATE_RANGE_MAX,
                                 &punNumerator, &punDenominator);
        if (SUCCEEDED(hr) && punDenominator != 0) {
          capability.maxFPS = punNumerator / punDenominator;
        }

        // get videoType
        GUID subType = GUID_NULL;
        hr = mfType->GetGUID(MF_MT_SUBTYPE, &subType);
        if (FAILED(hr)) {
          //LOG(LS_WARNING) << "Device can't get subType: "
          //                << ", error " << GetLastError() << ", width "
          //                << capability.width << ", height "
          //                << capability.height;
          SafeRelease(&mfType);
          continue;
        }

        // can't switch MEDIATYPE :~(
        if (subType == MFVideoFormat_I420) {
          capability.videoType = VideoType::kI420;
        } else if (subType == MFVideoFormat_IYUV) {
          capability.videoType = VideoType::kIYUV;
        } else if (subType == MFVideoFormat_RGB24) {
          capability.videoType = VideoType::kRGB24;
        } else if (subType == MFVideoFormat_YUY2) {
          capability.videoType = VideoType::kYUY2;
        } else if (subType == MFVideoFormat_RGB565) {
          capability.videoType = VideoType::kRGB565;
        } else if (subType == MFVideoFormat_MJPG) {
          capability.videoType = VideoType::kMJPEG;
        } else if (subType == MFVideoFormat_NV12) {
          capability.videoType = VideoType::kNV12;
        } else if (subType == MFVideoFormat_DVSL ||
                   subType == MFVideoFormat_DVSD ||
                   subType ==
                       MFVideoFormat_DVHD)  // If this is an external DV camera
        {
          capability.videoType =
              VideoType::kYUY2;  // MS DV filter seems to create this type
        } else if (subType ==
                   MFVideoFormat_UYVY)  // Seen used by Declink capture cards
        {
          capability.videoType = VideoType::kUYVY;
        } else {
          WCHAR strGuid[39];
          StringFromGUID2(subType, strGuid, 39);
          //LOG(LS_WARNING) << "Device support unknown media type " << strGuid
          //                << ", width " << capability.width << ", height "
          //                << capability.height;
          SafeRelease(&mfType);
          continue;
        }

        _captureCapabilities.push_back(capability);
        _captureCapabilitiesWindows.push_back(capability);
        //LOG(LS_INFO) << "Camera capability, width:" << capability.width
        //             << " height:" << capability.height
        //             << " type:" << static_cast<int>(capability.videoType)
        //             << " fps:" << capability.maxFPS;
      }
    }

    SafeRelease(&mfType);
  }

done:
  SafeRelease(&mfPresentDesc);
  SafeRelease(&mfStreamDesc);
  SafeRelease(&mfHandler);
  if(source)
  {
    source->Shutdown();
  }
  SafeRelease(&source);

  // Store the new used device name
  _lastUsedDeviceNameLength = deviceUniqueIdUTF8Length;
  _lastUsedDeviceName =
      (char*)realloc(_lastUsedDeviceName, _lastUsedDeviceNameLength + 1);
  memcpy(_lastUsedDeviceName, deviceUniqueIdUTF8,
         _lastUsedDeviceNameLength + 1);
  //LOG(LS_INFO) << "CreateCapabilityMap " << _captureCapabilities.size();
  return static_cast<int32_t>(_captureCapabilities.size());
}

int32_t DeviceInfoMF::GetWindowsCapability(
    const int32_t capabilityIndex,
    VideoCaptureCapabilityWindowsMf& windowsCapability) {
  std::lock_guard<std::mutex> lock(_apiLock);

  if (capabilityIndex < 0 || static_cast<size_t>(capabilityIndex) >=
                                 _captureCapabilitiesWindows.size()) {
    return -1;
  }

  windowsCapability = _captureCapabilitiesWindows[capabilityIndex];
  return 0;
}

IMFMediaSource* DeviceInfoMF::GetDeviceMediaSource(
    const char* deviceUniqueIdUTF8) {
  const int32_t deviceUniqueIdUTF8Length = (int32_t)strlen(
      (char*)deviceUniqueIdUTF8);  // UTF8 is also NULL terminated
  if (deviceUniqueIdUTF8Length > kVideoCaptureUniqueNameLength) {
    //LOG(LS_INFO) << "Device name too long";
    return NULL;
  }

  IMFMediaSource* deviceSource = NULL;
  IMFActivate** ppDevices = NULL;
  UINT32 count;

  //LOG(LS_WARNING) << "Get filter by guid:��" << deviceUniqueIdUTF8 << "��";
  HRESULT hr = MFEnumDeviceSources(_mfAttribute, &ppDevices, &count);
  if (SUCCEEDED(hr) && deviceUniqueIdUTF8Length > 0 && ppDevices) {
    bool deviceFound = false;
    int i = 0;
    do {
      WCHAR* name = NULL;
      UINT32 nameLength;
      hr = ppDevices[i]->GetAllocatedString(
          MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &name,
          &nameLength);
      if (FAILED(hr)) {
        hr = ppDevices[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLength);
      }
      if (SUCCEEDED(hr)) {
        char tempDevicePathUTF8[256];
        tempDevicePathUTF8[0] = 0;

        WideCharToMultiByte(CP_UTF8, 0, name, -1, tempDevicePathUTF8,
                            sizeof(tempDevicePathUTF8), NULL, NULL);
        if (strncmp(tempDevicePathUTF8, (const char*)deviceUniqueIdUTF8,
                    deviceUniqueIdUTF8Length) == 0) {
          //LOG(LS_INFO) << "we need found target guid:" << deviceUniqueIdUTF8
          //             << ",mf device guid:" << tempDevicePathUTF8;
          deviceFound = true;
          hr = ppDevices[i]->ActivateObject(IID_PPV_ARGS(&deviceSource));
          if (FAILED(hr)){

          }
            //LOG(LS_WARNING)
            //    << "Device found but ActivateObject failed, error:" << hr;
        } else {
          //LOG(LS_WARNING) << "Skip filter compare guid:��" << tempDevicePathUTF8<<"��";
        }
      } else {
        //LOG(LS_ERROR) << "GetAllocatedString SYMBOLIC_LINK failed,hr:" << hr;
      }

      ++i;
    } while (i < count && !deviceFound);
  }

  if (ppDevices) {
    for (DWORD i = 0; i < count; i++) {
      SafeRelease(&ppDevices[i]);
    }
    CoTaskMemFree(ppDevices);
  }
  return deviceSource;
}

bool DeviceInfoMF::TestifyMediaSource(){
  bool result = true;
  IMFMediaSource* deviceSource = NULL;
  IMFActivate** ppDevices = NULL;
  UINT32 count = 0;
  //LOG(LS_INFO) << "start testing media source ";
  HRESULT hr = MFEnumDeviceSources(_mfAttribute, &ppDevices, &count);

  if (SUCCEEDED(hr) && ppDevices) {

    for (DWORD i = 0; i < count; i++){
      std::wstring guid;
      if (GetMediaSourceGuid(ppDevices[i], guid)) {
        //Check whether it has been testified.
        //rtc::CritScope cs(&MapCs);
        auto it = Testified_MediaSources_Map.find(guid);
        if (it != Testified_MediaSources_Map.end()) {
          if (!it->second) {
            //LOG(LS_WARNING)
            //    << "Found tested device and it's not valid, returning";
            result = false;
            break;
          }

        } else {
          //Testify the source and record the result
          //LOG(LS_INFO) << "start testing activate media source :" << i;
          result = TestifyMediaSourceImpl(ppDevices[i]);
          //LOG(LS_INFO) << "done testing activate media source :" << i;
          Testified_MediaSources_Map[guid] = result;
          if (!result) {
            break;
          }
        }

      } else {
        // Unable to find guid, testify directly.
        //LOG(LS_INFO) << "start testing activate media source :" << i;
        result = TestifyMediaSourceImpl(ppDevices[i]);
        //LOG(LS_INFO) << "done testing activate media source :" << i;
        if (!result) {
          break;
        }
      }
    }
  }

  for (DWORD i = 0; i < count; i++) {
    SafeRelease(&ppDevices[i]);
  }

  CoTaskMemFree(ppDevices);
  //LOG(LS_INFO) << "done testing media source ";
  return result;
}

bool DeviceInfoMF::TestifyMediaSourceImpl(IMFActivate* device) {
  bool result = false;
  IMFMediaSource* deviceSource = nullptr;

  if (device) {
    HRESULT hr = device->ActivateObject(IID_PPV_ARGS(&deviceSource));

    result = SUCCEEDED(hr);
    if(!result) {
      //LOG(LS_WARNING) << "Error occur while testing media source, error:"
      //                << std::hex << hr;
    }
    //If the source has been testified, it must be shut down,
    //otherwise it may occupy resources and cause a black screen.
    if (deviceSource) {
      deviceSource->Shutdown();
      deviceSource = nullptr;
    }
  }

  return result;
}

bool DeviceInfoMF::GetMediaSourceGuid(IMFActivate* device, std::wstring& guid) {

  if (device) {
    LPWSTR name;
    UINT32 length;
    HRESULT hr = device->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &name,
        &length);

    if (SUCCEEDED(hr)) {
      guid = std::wstring(name);
      return true;
    }
  }

  return false;
}
}  // namespace videocapturemodule
}  // namespace webrtc
