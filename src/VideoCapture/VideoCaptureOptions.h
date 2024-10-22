/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#pragma once


namespace hrtc {

// An object that stores initialization parameters for video capturers
class  VideoCaptureOptions {
 public:
  VideoCaptureOptions();
  VideoCaptureOptions(const VideoCaptureOptions& options);
  VideoCaptureOptions(VideoCaptureOptions&& options);
  ~VideoCaptureOptions();

  VideoCaptureOptions& operator=(const VideoCaptureOptions& options);
  VideoCaptureOptions& operator=(VideoCaptureOptions&& options);

  enum class Status {
    SUCCESS,
    UNINITIALIZED,
    UNAVAILABLE,
    DENIED,
    ERROR,
    MAX_VALUE = ERROR
  };

  class Callback {
   public:
    virtual void OnInitialized(Status status) = 0;

   protected:
    virtual ~Callback() = default;
  };

  void Init(Callback* callback);

#if defined(HRTC_LINUX)
  bool allow_v4l2() const { return allow_v4l2_; }
  void set_allow_v4l2(bool allow) { allow_v4l2_ = allow; }
#endif


 private:
#if defined(HRTC_LINUX)
  bool allow_v4l2_ = false;
#endif
};

}  // namespace hrtc

