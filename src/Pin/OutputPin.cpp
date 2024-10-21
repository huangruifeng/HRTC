#include "Pin/InputPin.h"
#include "Pin/OutputPin.h"
#include "Pin/InputPin.h"
#include "Base/base.h"
using namespace hrtc;

hrtc::OutputPin::OutputPin(OutputPinObserver * observer):BasePin(Direction::Output),m_owner(observer)
{
}

RtcResult hrtc::OutputPin::DisconnectAllPin()
{
    auto pairs = m_connectedPins;
    for (auto& pin : pairs) {
        DisconnectPin(pin);
    } 
    return HRTC_CODE_OK;
}

RtcResult hrtc::OutputPin::Send(const IMediaInfo & info)
{
    for (const auto& pin : m_connectedPins) {
        reinterpret_cast<InputPin*>(pin)->OnReceive(info);
    }
    return HRTC_CODE_OK;
}

RtcResult hrtc::OutputPin::OnRequest(IMediaInfo & info)
{
    if(m_owner){
        m_owner->OnReqeust(this,info);
    }
    return HRTC_CODE_OK;
}

bool hrtc::OutputPin::IsConnectedTo(BasePin * pin)
{
    return m_connectedPins.find(pin) != m_connectedPins.end();
}

OutputPinObserver * hrtc::OutputPin::GetOwner()
{
    return m_owner;
}

RtcResult hrtc::OutputPin::OnPinConnect(BasePin * pin)
{
    if(!dynamic_cast<InputPin*>(pin)) {
        HRTC_ASSERT_MSG_DEBUG(false,"invalid pin implement");
        return HRTC_CODE_ERROR_NOT_SUPPORTED;
    }
    auto result = BasePin::OnPinConnect(pin);
    if(HRTC_FAILED(result)){
        return result;
    }

    if(!m_owner){
        m_owner->OnOutputConnect(this,pin);
    }

    return HRTC_CODE_OK;
}

RtcResult hrtc::OutputPin::OnPinDisconnect(BasePin * pin)
{
    if(!dynamic_cast<InputPin*>(pin)) {
        HRTC_ASSERT_MSG_DEBUG(false,"invalid pin implement");
        return HRTC_CODE_ERROR_NOT_SUPPORTED;
    }

    auto result = BasePin::OnPinDisconnect(pin);
    if(HRTC_FAILED(result)){
        return result;
    }

    if(m_owner){
        m_owner->OnOutputDisconnect(this,pin);
    }
    return HRTC_CODE_OK;
}
