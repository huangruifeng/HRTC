#include "Node/BaseNode.h"
#include "Base/base.h"
#include <algorithm>
using namespace hrtc;

BaseNode::NodeType hrtc::BaseNode::GetType() const
{
    return m_type;
}

hrtc::BaseNode::BaseNode(NodeType type, NodeFormatType formatType):m_type(type),m_formatType(formatType),m_configured(false)
{
    CheckPinListStatus();
}

RtcResult hrtc::BaseNode::Connect(IPin * nextPin)
{
    CheckPinListStatus();
    if(!nextPin){
        HRTC_ASSERT_MSG_DEBUG(false,"nextPin is nullptr");
        return HRTC_CODE_ERROR_NULLPTR;
    }
    if(m_outputPins.empty()){
        HRTC_ASSERT_MSG_DEBUG(false,"no available output pin");
        return HRTC_CODE_ERROR_NOT_SUPPORTED;
    }
    
    auto it = std::find_if(m_inputPins.begin(),m_inputPins.end(),[nextPin](auto p){
        return nextPin == p.get();
    });
    if(it != m_inputPins.end()){
        HRTC_ASSERT_MSG_DEBUG(false,"nextPin is part of current node");
        return HRTC_CODE_ERROR_NOT_SUPPORTED;
    }

    return m_outputPins[0].get()->ConnectPin(nextPin); 
}

RtcResult hrtc::BaseNode::Connect(IPin * sourcePin,IPin * nextPin)
{
    CheckPinListStatus();
    if(!sourcePin){
        HRTC_ASSERT_MSG_DEBUG(false,"sourcePin is nullptr");
        return HRTC_CODE_ERROR_NULLPTR;
    }

    if(nextPin){
        HRTC_ASSERT_MSG_DEBUG(false,"nextPin is nullptr");
        return HRTC_CODE_ERROR_NULLPTR;
    }

    auto it = std::find_if(m_outputPins.begin(),m_outputPins.end(),[sourcePin](auto pin)->bool{
        return sourcePin == pin.get();
    });
    if(it != m_outputPins.end()){
        HRTC_ASSERT_MSG_DEBUG(false,"sourcePin is not part of current node");
        return HRTC_CODE_ERROR_INVALID_PIN;
    }

    auto it2 = std::find_if(m_inputPins.begin(),m_inputPins.end(),[nextPin](auto pin)->bool{
        return nextPin == pin.get();
    });
    if(it2 != m_inputPins.end()){
        HRTC_ASSERT_MSG_DEBUG(false,"nextPin is part of current node");
        return HRTC_CODE_ERROR_NOT_SUPPORTED;
    }

    return sourcePin->ConnectPin(nextPin);
}

int32_t hrtc::BaseNode::ConnectDefault(BaseNode * const nextNode)
{
    CheckPinListStatus();
    if(!nextNode){
        HRTC_ASSERT_MSG_DEBUG(false,"next node is nullptr");
        return HRTC_CODE_ERROR_NULLPTR;
    }

    if(nextNode->m_inputPins.empty()){
        HRTC_ASSERT_MSG_DEBUG(false,"nextnode no available input pin");
        return HRTC_CODE_ERROR_NOT_SUPPORTED;
    }
    return Connect(nextNode->m_inputPins[0].get());
}


RtcResult hrtc::BaseNode::DisconnectAll()
{
    CheckPinListStatus();
    for (auto& out : m_outputPins) {
        out->DisconnectAllPin();
    }

    for (auto&in : m_inputPins) {
        in->DisconnectAllPin();
    }
    return HRTC_CODE_OK; 
}

RtcResult hrtc::BaseNode::Disconnect(BaseNode * const node)
{
    CheckPinListStatus();
    if(!node){
        HRTC_ASSERT_MSG_DEBUG(false,"node is nullptr");
        return HRTC_CODE_ERROR_NULLPTR;
    }

    for(auto& input:node->m_inputPins ){
        Disconnect(input.get());
    }

    for(auto& output:node->m_outputPins){
        Disconnect(output.get());
    }
    return HRTC_CODE_OK;
}

RtcResult hrtc::BaseNode::Disconnect(IPin * pin)
{
    CheckPinListStatus();
    if(!pin){
        HRTC_ASSERT_MSG_DEBUG(false,"pin is nullptr");
        return HRTC_CODE_ERROR_NULLPTR;
    }
    if(IPin::Direction::Input == pin->GetDirection()){
        auto input = dynamic_cast<InputPin*>(pin);
        if(!input){
            HRTC_ASSERT_MSG_DEBUG(false,"input is nullptr");
            return HRTC_CODE_ERROR_NULLPTR;
        }
        for (auto& outPin : m_outputPins) {
            auto out = std::dynamic_pointer_cast<OutputPin>(outPin);
            if (out->IsConnectedTo(input)) {
                return out->DisconnectPin(input);
            }
        }
    }
    else if(IPin::Direction::Output == pin->GetDirection()) {
        auto output = dynamic_cast<OutputPin*>(pin);
        if(!output){
            HRTC_ASSERT_MSG_DEBUG(false,"output is nullptr");
            return HRTC_CODE_ERROR_NULLPTR;
        }

        for (auto& inPin : m_inputPins) {
            auto in = std::dynamic_pointer_cast<InputPin>(inPin);
            if (in->IsConnectedTo(output)) {
                return in->DisconnectPin(output);
            }
        }
    }
    return HRTC_CODE_WARNING_NODE_NOT_CONNECT;
}

void hrtc::BaseNode::SendDataToOutput(const IMediaInfo & sample)
{
    for (auto& e : m_outputPins) {
        const auto out = std::dynamic_pointer_cast<OutputPin>(e);
        out->Send(sample);
    }
}

void hrtc::BaseNode::RequestDataFromInput(IMediaInfo & requestSample)
{
    if(m_inputPins.size() > 0){
        auto in = std::dynamic_pointer_cast<InputPin>(m_inputPins[0]);
        in->Request(requestSample);
    }
}

RtcResult hrtc::BaseNode::ConfigurePins()
{
    switch (m_type)
    {
    case NodeType::SOURCE:
        m_outputPins.push_back(std::make_shared<OutputPin>(this));
        break;
    case NodeType::SINK:
        m_inputPins.push_back(std::make_shared<InputPin>(this));
        break;
    case NodeType::FILTER:
        m_outputPins.push_back(std::make_shared<OutputPin>(this));
        m_inputPins.push_back(std::make_shared<InputPin>(this));
        break;
    default:
        break;
    }
    return HRTC_CODE_OK;
}

void hrtc::BaseNode::CheckPinListStatus()
{
    if(!m_configured){
        ConfigurePins();
        m_configured = true;
    }
}
