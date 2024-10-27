#pragma once
#include "Pin/OutputPin.h"
#include "Pin/InputPin.h"
#include <vector>
#include <memory>
#include <Headers/HrtcEngine.h>
namespace hrtc {
class BaseNode : public virtual INode
               , public OutputPinObserver
               , public InputPinObserver {
public:
    enum NodeType{
        SOURCE,
        FILTER,
        SINK
    };
    enum NodeFormatType{
        AUDIO,
        VIDEO,
        COMMON
    };
    virtual ~BaseNode() = default;
    NodeType GetType() const;
    virtual const char* GetName() { return "BaseNode";}
    BaseNode(NodeType type,NodeFormatType formatType);

    RtcResult Connect(IPin* nextPin);
    RtcResult Connect(IPin* sourcePin,IPin* nextPin);
    RtcResult ConnectDefault(BaseNode* const nextNode);

    RtcResult DisconnectAll();
    RtcResult Disconnect(BaseNode* const node) ;
    RtcResult Disconnect(IPin* pin) ;


    virtual void OnReqeust(IPin* pin, IMediaInfo& sample) override{
        if(SOURCE != m_type){
            RequestDataFromInput(sample);
        }
    }
    virtual void OnOutputConnect(IPin* pin, IPin* outputPin) override{};
    virtual void OnOutputDisconnect(IPin* pin, IPin* outputPin) override{};

    virtual void OnData(IPin* pin, const IMediaInfo& sample) override{
        if(SINK != m_type){
            SendDataToOutput(sample);
        }
    }
    virtual void OnInputConnect(IPin* pin, IPin* outputPin) override{};
    virtual void OnInputDisconnect(IPin* pin, IPin* outputPin) override{};
protected:
    void SendDataToOutput(const IMediaInfo& sample);
    void RequestDataFromInput(IMediaInfo& requestSample);
    virtual RtcResult ConfigurePins();
    void CheckPinListStatus();

    std::vector<std::shared_ptr<IPin>> m_inputPins;
    std::vector<std::shared_ptr<IPin>> m_outputPins;
    const NodeType m_type;
    const NodeFormatType m_formatType;
    bool m_configured;
};
}