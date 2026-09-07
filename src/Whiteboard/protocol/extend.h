#pragma once
#include <msgpack.hpp>
#include <string>

#include "Whiteboard/protocol/geometry_codec.h"

namespace whiteboard {
namespace protocol {

// Template serialization helpers: Serialize returns binary string, Deserialize
// returns a re-constructed object. Concrete types rely on their msgpack adaptors
// (geometry adaptors are provided by geometry_codec.h; the protocol layer may add
// more adaptors without touching the data classes).
struct Extend {
    template <class T>
    static std::string Serialize(const T& value) {
        msgpack::sbuffer sbuf;
        msgpack::pack(sbuf, value);
        return std::string(sbuf.data(), sbuf.size());
    }

    template <class T>
    static T Deserialize(const std::string& data) {
        msgpack::object_handle oh = msgpack::unpack(data.data(), data.size());
        T value{};
        oh.get().convert(value);
        return value;
    }
};

} // namespace protocol
} // namespace whiteboard