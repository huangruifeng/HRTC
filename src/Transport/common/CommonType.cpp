#include "CommonType.h"

#include <msgpack.hpp>

namespace hrtc {

std::string SerializeStream(const DataStreamType& stream)
{
    msgpack::sbuffer sbuf;
    msgpack::packer<msgpack::sbuffer> pk(&sbuf);

    pk.pack_array(2);
    pk.pack(stream.type);
    pk.pack_array(static_cast<uint32_t>(stream.param.size()));
    for (const auto& p : stream.param)
        pk.pack(p);

    return std::string(sbuf.data(), sbuf.size());
}

bool DeserializeStream(const std::string& data, DataStreamType& stream)
{
    try {
        msgpack::object_handle oh = msgpack::unpack(data.data(), data.size());
        const msgpack::object& obj = oh.get();
        if (obj.type != msgpack::type::ARRAY || obj.via.array.size != 2)
            return false;

        obj.via.array.ptr[0].convert(stream.type);

        const msgpack::object& params = obj.via.array.ptr[1];
        if (params.type != msgpack::type::ARRAY)
            return false;

        stream.param.clear();
        for (uint32_t i = 0; i < params.via.array.size; ++i) {
            std::string p;
            params.via.array.ptr[i].convert(p);
            stream.param.push_back(std::move(p));
        }
        return true;
    }
    catch (...) {
        return false;
    }
}

}
