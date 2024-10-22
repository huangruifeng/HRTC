#include <windows.h>
#include "VideoCapture/VideoCaptureFactory.h"

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
    auto deviceInfo = hrtc::VideoCaptureFactory::CreateDeviceInfo();
    auto s = deviceInfo->NumberOfDevices();
    //std::shared_ptr<hrtc::VideoCaptureModule> vcm = hrtc::VideoCaptureFactory::Create();

    return MessageBox(NULL, L"Hello, OneFLOW CFD!",L"Computation", 0);
}