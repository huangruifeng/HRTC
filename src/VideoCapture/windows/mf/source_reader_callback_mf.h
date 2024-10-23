#ifndef MODULES_VIDEO_CAPTURE_WINDOWS_SOURCE_READER_CALLBACK_MF_H_
#define MODULES_VIDEO_CAPTURE_WINDOWS_SOURCE_READER_CALLBACK_MF_H_

#include <mfidl.h>
#include <mfreadwrite.h>

#include "Base/VideoBase/VideoCaptureDefines.h"
#include "VideoCapture/windows/mf/source_reader_control_mf.h"
#include <memory>
namespace hrtc {
namespace videocapturemodule {
/*
 Refcounted plain memory buffer
 todo: extract this to rtc base module
 */
class PlainMemoryBuffer  {
 public:
  static std::shared_ptr<PlainMemoryBuffer> Create(size_t size);
  size_t Size() const { return _size; }
  const uint8_t* Data() const { return _data.get(); }
  uint8_t* MutableData() const { return _data.get(); }

  PlainMemoryBuffer(size_t size);

 private:
  const std::unique_ptr<uint8_t> _data;
  const size_t _size;
};

/*
 * Media Foundation SourceReader readsample callback, use this when try to
 * process captured sample through readsample method. Must be set throught
 * IMFAttributes:
 * https://docs.microsoft.com/zh-cn/windows/desktop/api/mfreadwrite/nn-mfreadwrite-imfsourcereadercallback
 */
class SourceReaderCallbackMF : public IMFSourceReaderCallback {
 public:
  explicit SourceReaderCallbackMF(ISourceReaderControlMF* control);
  virtual ~SourceReaderCallbackMF();

  // IUnknown methods
  STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
  STDMETHODIMP_(ULONG) AddRef();
  STDMETHODIMP_(ULONG) Release();

  // IMFSourceReaderCallback methods
  STDMETHODIMP OnReadSample(HRESULT hrStatus,
                            DWORD dwStreamIndex,
                            DWORD dwStreamFlags,
                            LONGLONG llTimestamp,
                            IMFSample* pSample);

  STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) { return S_OK; }

  STDMETHODIMP OnFlush(DWORD) { return S_OK; }

  inline HRESULT currentStatus() { return _currentStatus; };
  inline bool hasCallback() { return _hasCallback; };
 private:
  long _m_nRefCount;  // Reference count.
  bool _hasCallback;
  ISourceReaderControlMF* _control;
  VideoCaptureCapability _resultingCapability;
  HRESULT _currentStatus;
  uint64_t _sampleReadErrorSinceLastOne;
  int64_t _lastError;
  std::shared_ptr<PlainMemoryBuffer> _intermediateBuffer;
  uint64_t _lastLogTimeMs;
  size_t _frameReceived;
};

}  // namespace videocapturemodule
}  // namespace webrtc

#endif  // !MODULES_VIDEO_CAPTURE_WINDOWS_SOURCE_READER_CALLBACK_MF_H_
