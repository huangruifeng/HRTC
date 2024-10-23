#pragma once

#include <windows.h>
#include <cfgmgr32.h>  // for MAX_DEVICE_ID_LEN, CM_Get_Parent and CM_Get_Device_ID
#include <devguid.h>  // for GUID_DEVCLASS_CDROM etc
#include <mmdeviceapi.h>// for StringFromGUID2
#include <devpkey.h>
#include <setupapi.h>
#include <string>
namespace hrtc
{
namespace videocapturemodule 
{

struct VideoCaptureCapabilityWindowsMf: public VideoCaptureCapability
{
    uint32_t capabilityIndex;
    bool supportFrameRateControl;
    VideoCaptureCapabilityWindowsMf()
    {
        capabilityIndex = 0;
        supportFrameRateControl = false;
    }
};

inline bool IsBuiltInVideoDevice(const std::string& guid) {
  GUID VideoInterfaceGuid = {0x6994AD05,
                             0x93EF,
                             0x11D0,
                             {0xA3, 0xCC, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96}};
  GUID BuildInGuid = {0x00000000,
                      0x0000,
                      0x0000,
                      {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};

  unsigned i;
  DWORD dwSize;
  DEVPROPTYPE ulPropertyType;
  CONFIGRET status;
  HDEVINFO hDevInfo;
  SP_DEVINFO_DATA deviceInfoData;
  SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
  WCHAR szDeviceInstanceID[MAX_DEVICE_ID_LEN];
  WCHAR szDevicePathID[MAX_DEVICE_ID_LEN];
  WCHAR containBuffer[MAX_PATH];

  char containIdChar[MAX_PATH];
  char interfaceIdPathChar[MAX_PATH];

  const int bufferLen = sizeof(containBuffer) / sizeof(containBuffer)[0];
  bool builtIn = false;
  hDevInfo = SetupDiGetClassDevs(&VideoInterfaceGuid, NULL, NULL,
                                 DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (hDevInfo == INVALID_HANDLE_VALUE) {
    //LOG(LS_ERROR) << "SetupDiGetClassDevs failed,error:" << GetLastError();
    return false;
  }

  deviceInfoData.cbSize = sizeof(deviceInfoData);
  deviceInterfaceData.cbSize = sizeof(deviceInterfaceData);
  // Find the ones that are driverless
  for (i = 0;; i++) {
    if (!SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData)) {
      //LOG(LS_ERROR) << "camera SetupDiEnumDeviceInfo failed,error:"
      //              << GetLastError();
      break;
    }

    status = CM_Get_Device_ID(deviceInfoData.DevInst, (PWSTR)szDeviceInstanceID,
                              MAX_PATH, 0);
    if (status != CR_SUCCESS) {
      //LOG(LS_WARNING) << "camera status not success";
      continue;
    }

    // Display device instance ID
    WideCharToMultiByte(CP_UTF8, 0, szDeviceInstanceID, -1, interfaceIdPathChar,
                        MAX_PATH, NULL, NULL);

    std::string deviceId = interfaceIdPathChar;
    if (deviceId.find("VID_") == std::string::npos &&
        deviceId.find("vid_") == std::string::npos) {
      //LOG(LS_INFO) << "device guid : " << interfaceIdPathChar
      //             << " invaild, maybe is virtual camera";
      continue;
    }
    if (SetupDiEnumDeviceInterfaces(hDevInfo, &deviceInfoData,
                                    &VideoInterfaceGuid, 0,
                                    &deviceInterfaceData)) {
      // get some more details etc
      DWORD requiredBufferSize;
      SetupDiGetDeviceInterfaceDetail(hDevInfo, &deviceInterfaceData, NULL, 0,
                                      &requiredBufferSize, NULL);
      PSP_INTERFACE_DEVICE_DETAIL_DATA detailData =
          (PSP_INTERFACE_DEVICE_DETAIL_DATA)malloc(requiredBufferSize);
      detailData->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);
      SP_DEVINFO_DATA did = {sizeof(SP_DEVINFO_DATA)};
      SetupDiGetDeviceInterfaceDetail(hDevInfo, &deviceInterfaceData,
                                      detailData, requiredBufferSize, NULL,
                                      &did);

      char completeGuid[MAX_PATH];
      WideCharToMultiByte(CP_UTF8, 0, detailData->DevicePath, -1, completeGuid,
                          MAX_PATH, NULL, NULL);
      std::string setupGuid = std::string(completeGuid);
      setupGuid = setupGuid.substr(0, setupGuid.find("#{"));
      std::string srcGuidSub = guid.substr(0, guid.find("#{"));
      if (detailData) {
        free(detailData);
      }
      if (srcGuidSub != setupGuid) {
        continue;
      }
    }
    std::string containId = "";
    if (SetupDiGetDeviceProperty(hDevInfo, &deviceInfoData,
                                 &DEVPKEY_Device_ContainerId, &ulPropertyType,
                                 (BYTE*)containBuffer, bufferLen, &dwSize, 0)) {
      WCHAR szDeviceContainerId[MAX_PATH];
      builtIn = !memcmp(&(REFGUID)containBuffer, &BuildInGuid, sizeof(GUID));
      StringFromGUID2((REFGUID)containBuffer, szDeviceContainerId, bufferLen);
      WideCharToMultiByte(CP_UTF8, 0, szDeviceContainerId, -1, containIdChar,
                          MAX_PATH, NULL, NULL);
      containId = containIdChar;
    }
    //LOG(LS_INFO) << "camera guid :" << guid << ",containId" << containId
    //             << ",is build-in:" << (builtIn ? "true" : "false");
  }

  ::SetupDiDestroyDeviceInfoList(hDevInfo);

  return builtIn;
}

}  // namespace videocapturemodule
}  // namespace webrtc
