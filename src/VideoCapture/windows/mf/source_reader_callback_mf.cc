#include "VideoCapture/windows/mf/source_reader_callback_mf.h"
#include "Base/TimeUtils.h"
#include <Mferror.h>
#include <Shlwapi.h>
#include "VideoCapture/windows/mf/device_info_mf.h"
#include "VideoCapture/windows/ds/help_functions_ds.h"
constexpr int kMaxLogTimesMs = 5000;

namespace hrtc {
namespace videocapturemodule {

std::shared_ptr<PlainMemoryBuffer> PlainMemoryBuffer::Create(size_t size) {
  return std::make_shared<PlainMemoryBuffer>(size);
}

PlainMemoryBuffer::PlainMemoryBuffer(size_t size)
  :_data(static_cast<uint8_t*>(malloc(size))), _size(size){
}

SourceReaderCallbackMF::SourceReaderCallbackMF(
    ISourceReaderControlMF* control)
    : _m_nRefCount(1),
      _control(control),
      _currentStatus(-1),
      _hasCallback(false),
      _resultingCapability(control->RunningCapability()),
      _lastError(0),
      _sampleReadErrorSinceLastOne(0),
      _intermediateBuffer(nullptr),
      _lastLogTimeMs(hrtc::TimeMillis()),
      _frameReceived(0){
   //LOG(LS_INFO) << "create media foundation source reader, this: " << this;
}

SourceReaderCallbackMF::~SourceReaderCallbackMF() {
  //LOG(LS_INFO) << "delete media foundation source reader, this: " << this;
}

HRESULT SourceReaderCallbackMF::QueryInterface(const IID& iid, void** ppv) {
  static const QITAB qit[] = {
      QITABENT(SourceReaderCallbackMF, IMFSourceReaderCallback),
      {0},
  };
  return QISearch(this, qit, iid, ppv);
}

ULONG SourceReaderCallbackMF::AddRef() {
  return InterlockedIncrement(&_m_nRefCount);
}

ULONG SourceReaderCallbackMF::Release() {
  ULONG uCount = InterlockedDecrement(&_m_nRefCount);
  if (uCount == 0) {
    //LOG(LS_INFO) << "destroy media foundation source reader, this: " << this;
    delete this;
  }
  // For thread safety, return a temporary variable.
  return uCount;
}

HRESULT SourceReaderCallbackMF::OnReadSample(HRESULT hrStatus,
                                             DWORD dwStreamIndex,
                                             DWORD dwStreamFlags,
                                             LONGLONG llTimestamp,
                                             IMFSample* pSample) {
  _hasCallback = true;

  if (_currentStatus == -1 || _currentStatus != S_OK)
  {
      _currentStatus = hrStatus;
  }

  if (hrStatus == MF_E_INVALIDREQUEST ||
      hrStatus == 0x80070491/*ERROR_NO_MATCH*/ ||
      hrStatus == MF_E_HW_MFT_FAILED_START_STREAMING) {  // 0x80070491 equals
                                                         // ERROR_NO_MATCH in
                                     // facility win32 api
    _control->OnReadSampleFailed(hrStatus);
    return hrStatus;
  }

  if (hrStatus != S_OK) {
    _sampleReadErrorSinceLastOne++;
    if (hrtc::TimeMillis() - _lastError > 10000) {
      _lastError = hrtc::TimeMillis();
      // LOG(LS_ERROR) << "failed to read sample from capture, error samples "
      //     "since last log: "
      //     << _sampleReadErrorSinceLastOne << ", last error: " << hrStatus;
      _sampleReadErrorSinceLastOne = 0;
    }
  }

  if (hrStatus == S_OK) {
    ++_frameReceived;
  }
  const auto now = hrtc::TimeMillis();
  if (now - _lastLogTimeMs >= kMaxLogTimesMs) {
    //LOG(LS_INFO) << "Video Capture Source reveive fps: "
    //             << _frameReceived * 1000 / (now - _lastLogTimeMs + 1);
    _lastLogTimeMs = now;
    _frameReceived = 0;
  }

  std::lock_guard<std::mutex> cs(*_control->GetStartStopCs());
  HRESULT hr = S_OK;
  IMFMediaBuffer* pBuffer = nullptr;
  bool fallback = true;

  if ((dwStreamFlags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) ||
      (dwStreamFlags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) {
    auto newCap = _control->RequestCapabilityUpdate();
    // LOG(LS_INFO) << "OnReadSample media type changed, new size:"
    //              << newCap.width << "x" << newCap.height
    //              << ", old one:"
    //              << _resultingCapability.width << "x" << _resultingCapability.height;

    _resultingCapability = newCap;
  }

  if (_control->IsRunning()) {
    // get frame data
    if (pSample) {
      hr = pSample->GetBufferByIndex(0, &pBuffer);
      if (SUCCEEDED(hr)) {
        IMF2DBuffer* mf2dBuffer = nullptr;
        BYTE* pDatas = nullptr;
        DWORD length = 0;
        BOOL isContinous = FALSE;
        _resultingCapability = _control->RunningCapability();
        // IMF2DBuffer is more efficient than the regular lock buffer
        hr = pBuffer->QueryInterface(IID_IMF2DBuffer, (void**)&mf2dBuffer);
        LONG pitch = 0;
        if (SUCCEEDED(hr) && _resultingCapability.videoType != hrtc::VideoType::kMJPEG &&
            SUCCEEDED(mf2dBuffer->Lock2D(&pDatas, &pitch))) {
          mf2dBuffer->IsContiguousFormat(&isContinous);
          if (isContinous) {
            DWORD cLength;
            mf2dBuffer->GetContiguousLength(&cLength);
            length = CalcBufferSize(_resultingCapability.videoType, _resultingCapability.width, _resultingCapability.height);
            //RTC_DCHECK(cLength == length);
            if(cLength!=length) {
              //LOG(LS_WARNING)<<"Calculated lenght does not match locked length, calLength:"<<length<<", locked:"<<cLength;
              length = cLength;//use locked length
              auto newCap = _control->RequestCapabilityUpdate();
              // LOG(LS_INFO) << "Get currentStream cap, new size:"
              //              << newCap.width << "x" << newCap.height
              //              << ", old one:" << _resultingCapability.width << "x"
              //              << _resultingCapability.height;

              _resultingCapability = newCap;
            }
            bool unlocked = false;
            if (pitch > 0 && (!_intermediateBuffer || (_intermediateBuffer->Size() < length || length <= _intermediateBuffer->Size()/2))) {
              _intermediateBuffer = PlainMemoryBuffer::Create(length);

              if (_intermediateBuffer && _intermediateBuffer->Data())
                memcpy(_intermediateBuffer->MutableData(), pDatas, length);
              pDatas = (BYTE*)_intermediateBuffer->Data();
              mf2dBuffer->Unlock2D();//unlock immediately
              unlocked = true;
            }

            //negative pitch means bottom-up image,usually for rgb image
            if (pitch < 0) {
              _resultingCapability.height = -_resultingCapability.height;

              _control->PushIncomingFrame(
                  pDatas + pitch * (abs(_resultingCapability.height) - 1),
                  length, _resultingCapability);
            } else {
              _control->PushIncomingFrame(pDatas, length, _resultingCapability);
            }
            fallback = false;
            if(!unlocked) {
              mf2dBuffer->Unlock2D();
            }
          }else {
            mf2dBuffer->Unlock2D();
          }
        }
        if (fallback) {
          // regular lock, internal buffer copy may happen if buffer is not
          // continous
          hr = pBuffer->Lock(&pDatas, NULL, &length);
          if (SUCCEEDED(hr)) {
            if (_resultingCapability.videoType == VideoType::kRGB24) {
              _resultingCapability.height = -_resultingCapability.height;
            }
            _control->PushIncomingFrame(pDatas, length, _resultingCapability);
            pBuffer->Unlock();
          }
        }

        RELEASE_AND_CLEAR(mf2dBuffer);
      }
    }
    hr = _control->RequestNextFrame();
    if (FAILED(hr)) {
      //LOG(LS_ERROR) << "Failed to request next frame, hr: " << hr;
    }
  }

  SafeRelease(&pBuffer);
  return hr;
}

}  // namespace videocapturemodule
}  // namespace webrtc
