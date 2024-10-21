#pragma once
#include "Pin/BasePin.h"
#include "Base/SpinLock.h"
#include <map>
#include <functional>
namespace hrtc {
class IMediaInfo;
class OutputPinObserver {
public:
    virtual ~OutputPinObserver() =default;
    virtual void OnReqeust(IPin* pin,IMediaInfo& sample) = 0;
    virtual void OnOutputConnect(IPin* pin, IPin* outputPin) = 0;
    virtual void OnOutputDisconnect(IPin* pin, IPin* outputPin) = 0;
};

class OutputPin : public BasePin {
public:
    OutputPin(OutputPinObserver* observer);
    virtual ~OutputPin() = default;
    RtcResult DisconnectAllPin() override;
    virtual RtcResult Send(const IMediaInfo& info);
    virtual RtcResult OnRequest(IMediaInfo& info);
    bool IsConnectedTo(BasePin* pin);
    OutputPinObserver* GetOwner();
protected:
    RtcResult OnPinConnect(BasePin* outputPin) override;
    RtcResult OnPinDisconnect(BasePin* pin)override;
    //todo add mediaINfo dump
    //std::map<std::string,std::function<void(const IMediaInfo&)>> m_onSamples;
    //hrtc::SpinLock m_onLock;
    OutputPinObserver* m_owner;
};
}