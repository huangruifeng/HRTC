#include "Pin/InputPin.h"
#include "Pin/OutputPin.h"
#include "Base/base.h"
using namespace hrtc;
hrtc::InputPin::InputPin(InputPinObserver * observer): BasePin(Direction::Input),m_owner(observer)
{
}

RtcResult hrtc::InputPin::DisconnectAllPin()
{
    auto pins  = m_connectedPins;
    for(auto&pin:pins){
        DisconnectPin(pin);
    }
    return HRTC_CODE_OK;
}

RtcResult hrtc::InputPin::OnReceive(const IMediaInfo & info)
{
    HRTC_ASSERT_MSG_DEBUG(m_owner!=nullptr,"m_owner is nullptr");
    if(m_owner){
        m_owner->OnData(this,info);
    }
    return HRTC_CODE_OK;
}

RtcResult hrtc::InputPin::Request(IMediaInfo & info)
{
    for (const auto& pin : m_connectedPins) {
        dynamic_cast<OutputPin*>(pin)->OnRequest(info);
    }
    return HRTC_CODE_OK;
}

bool hrtc::InputPin::IsConnectedTo(BasePin * pin)
{
    return m_connectedPins.find(pin) != m_connectedPins.end();
}

InputPinObserver * hrtc::InputPin::GetOwner()
{
    return m_owner;
}

RtcResult hrtc::InputPin::OnPinConnect(BasePin * outputPin)
{
    if(!dynamic_cast<OutputPin*>(outputPin)) {
        HRTC_ASSERT_MSG_DEBUG(false,"invalid pin implement")
        return HRTC_CODE_ERROR_NOT_SUPPORTED;
    }

    auto result = BasePin::OnPinConnect(outputPin);
    if(HRTC_FAILED(result)){
        return result;
    }

    if(!m_owner){
        m_owner->OnInputConnect(this,outputPin);
    }

    return HRTC_CODE_OK;
}

RtcResult hrtc::InputPin::OnPinDisconnect(BasePin * pin)
{
    if(!dynamic_cast<OutputPin*>(pin)) {
        HRTC_ASSERT_MSG_DEBUG(false,"invalid pin implement");
        return HRTC_CODE_ERROR_NOT_SUPPORTED;
    }

    auto result = BasePin::OnPinDisconnect(pin);
    if(HRTC_FAILED(result)){
        return result;
    }

    if(m_owner){
        m_owner->OnInputDisconnect(this,pin);
    }
    return HRTC_CODE_OK;
}
