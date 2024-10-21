#pragma once
#include <Base/ErrorCode.h>
namespace hrtc {
class IPin {
public:
    enum class Direction {
        Input,
        Output
    };
public:
    virtual ~IPin() = default;
    virtual RtcResult ConnectPin(IPin* receivePin) = 0;
    virtual bool Connected() const = 0;
    virtual RtcResult DisconnectPin(IPin* pin) = 0;
    virtual RtcResult DisconnectAllPin() = 0;
    virtual Direction GetDirection() const = 0;
};
}