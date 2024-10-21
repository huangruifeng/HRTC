#pragma once
#include "Pin/BasePin.h"
namespace hrtc {
class IMediaInfo;
class InputPinObserver {
public:
    virtual ~InputPinObserver() =default;
    virtual void OnData(IPin* pin, const IMediaInfo& sample) = 0;
    virtual void OnInputConnect(IPin* pin, IPin* outputPin) = 0;
    virtual void OnInputDisconnect(IPin* pin, IPin* outputPin) = 0;
};

class InputPin : public BasePin {
public:
    InputPin(InputPinObserver* observer);
    virtual ~InputPin() = default;
    RtcResult DisconnectAllPin() override;
    virtual RtcResult OnReceive(const IMediaInfo& info);
    virtual RtcResult Request(IMediaInfo& info);
    bool IsConnectedTo(BasePin* pin);
    InputPinObserver* GetOwner();
protected:
    RtcResult OnPinConnect(BasePin* outputPin) override;
    RtcResult OnPinDisconnect(BasePin* pin)override;
    InputPinObserver* m_owner;
};
}