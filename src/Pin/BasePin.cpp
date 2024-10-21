#include "BasePin.h"
#include "Base/base.h"
using namespace hrtc;

RtcResult hrtc::BasePin::ConnectPin(IPin * receivePin)
{
    if(this == receivePin){
        HRTC_ASSERT_MSG_DEBUG(false,"can not connect to self");
        return HRTC_CODE_ERROR_CONNECT_TO_SELF;
    }
    if(!receivePin){
        HRTC_ASSERT_MSG_DEBUG(false,"receivePin is nullptr");
        return HRTC_CODE_ERROR_NULLPTR;
    }
    
    //Check Direction.
    if(Direction::Output != m_direction){
        HRTC_ASSERT_MSG_DEBUG(false,"input pin cannot be called with connect");
        return HRTC_CODE_ERROR_PIN_WRONG_DIRECTION;
    }

    if( Direction::Input != receivePin->GetDirection()){
        HRTC_ASSERT_MSG_DEBUG(false,"output pin cannot be connected");
        return HRTC_CODE_ERROR_PIN_WRONG_DIRECTION;
    }

    auto result = dynamic_cast<BasePin*>(receivePin)->OnPinConnect(this);
    if(HRTC_FAILED(result)){
        return result;
    }

    return OnPinConnect(dynamic_cast<BasePin*>(receivePin));
}

bool hrtc::BasePin::Connected() const
{
    return !m_connectedPins.empty();
}

RtcResult hrtc::BasePin::OnPinConnect(BasePin * outputPin)
{ 
    if(m_connectedPins.find(outputPin) != m_connectedPins.end()){
        HRTC_ASSERT_MSG_DEBUG(false, "Duplicated pin, cannot complete connect");
        return HRTC_CODE_ERROR_DUPLICATED;
    }
    m_connectedPins.insert(outputPin);
    return HRTC_CODE_OK;
}

RtcResult hrtc::BasePin::OnPinDisconnect(BasePin * pin)
{
    auto it = m_connectedPins.find(pin);
    
    if(it != m_connectedPins.end()){
        m_connectedPins.erase(it);   
    }
    else {
        HRTC_ASSERT_MSG_DEBUG(false,"pin not found");
        return HRTC_CODE_ERROR_NOT_FOUND;
    }
    return HRTC_CODE_OK;
}

RtcResult hrtc::BasePin::DisconnectPin(IPin * pin)
{
    if(this == pin){
        HRTC_ASSERT_MSG_DEBUG(false,"can not disconnect to self");
        return HRTC_CODE_ERROR_INVALID_PIN;
    }

    if(!pin){
        HRTC_ASSERT_MSG_DEBUG(false,"pin is nullptr");
        return HRTC_CODE_ERROR_NULLPTR;
    }

    auto result = dynamic_cast<BasePin*>(pin)->OnPinDisconnect(this);
    if(HRTC_FAILED(result)){
        return result;
    }
    return OnPinDisconnect(dynamic_cast<BasePin*>(pin));
}

RtcResult hrtc::BasePin::DisconnectAllPin()
{
    return HRTC_CODE_ERROR_NOT_SUPPORTED;
}

IPin::Direction hrtc::BasePin::GetDirection() const
{
    return m_direction;
}

hrtc::BasePin::BasePin(Direction direction) : m_direction(direction)
{
}
