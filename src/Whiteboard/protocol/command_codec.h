#pragma once
#include <memory>
#include <string>

#include "Whiteboard/command/command.h"

namespace whiteboard {
namespace protocol {

// Command wire format: [type_str, pageId, payload_array]. The payload array
// layout is defined per command type (see command_codec.cpp).
std::string SerializeCommand(const Command& cmd);
std::shared_ptr<Command> DeserializeCommand(const std::string& data);

} // namespace protocol
} // namespace whiteboard