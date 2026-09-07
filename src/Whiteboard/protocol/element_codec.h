#pragma once
#include <msgpack.hpp>
#include <memory>
#include <string>

#include "Whiteboard/element/element.h"
#include "Whiteboard/element/stroke.h"
#include "Whiteboard/page/page.h"

namespace whiteboard {
namespace protocol {

// Concrete element field arrays (no type tag):
//   Stroke -> [id, width, color, rawPoints]
//   Page   -> [id, pageId, transform, elements]
std::string SerializeStroke(const Stroke& stroke);
Stroke DeserializeStroke(const std::string& data);

std::string SerializePage(const Page& page);
Page DeserializePage(const std::string& data);

// Polymorphic element encoding: [type_str, fields].
std::string SerializeElement(const Element& e);
std::shared_ptr<Element> DeserializeElement(const std::string& data);

// Inline helpers used when embedding an element inside a command payload.
// PackElement writes [type_str, fields]; UnpackElement reads the same shape and
// returns nullptr for unknown types.
void PackElement(msgpack::packer<msgpack::sbuffer>& pk, const Element& e);
std::shared_ptr<Element> UnpackElement(const msgpack::object& obj);

} // namespace protocol
} // namespace whiteboard