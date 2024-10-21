#pragma once
#include <Pin/IPin.h>
#include <set>
namespace hrtc {
class BasePin : public IPin {
public:
    virtual RtcResult ConnectPin(IPin* receivePin) override;
    virtual bool Connected() const override;
    virtual RtcResult DisconnectPin(IPin* pin) override;
    virtual RtcResult DisconnectAllPin() override;
    virtual Direction GetDirection() const override;
protected:
    virtual RtcResult OnPinConnect(BasePin* outputPin);
    virtual RtcResult OnPinDisconnect(BasePin* pin);

    BasePin(Direction direction);
    virtual ~BasePin() = default;

    const Direction m_direction;
    std::set<BasePin*> m_connectedPins;
};
}