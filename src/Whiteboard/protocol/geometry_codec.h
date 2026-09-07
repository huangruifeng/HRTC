#pragma once
// msgpack adaptor specializations for the whiteboard geometry value types.
// They live in the protocol layer so the data classes stay free of msgpack.

#include <msgpack.hpp>

#include "Whiteboard/geometry/point.h"
#include "Whiteboard/geometry/rect.h"
#include "Whiteboard/geometry/boundary_rect.h"
#include "Whiteboard/geometry/transform.h"

namespace msgpack {
MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS) {
namespace adaptor {

template <typename T>
struct pack<whiteboard::PointTemplate<T>> {
    template <typename Stream>
    msgpack::packer<Stream>& operator()(msgpack::packer<Stream>& o, const whiteboard::PointTemplate<T>& v) const {
        o.pack_array(2);
        o.pack(v.x);
        o.pack(v.y);
        return o;
    }
};

template <typename T>
struct convert<whiteboard::PointTemplate<T>> {
    msgpack::object const& operator()(msgpack::object const& o, whiteboard::PointTemplate<T>& v) const {
        if (o.type != msgpack::type::ARRAY || o.via.array.size != 2)
            throw msgpack::type_error();
        o.via.array.ptr[0].convert(v.x);
        o.via.array.ptr[1].convert(v.y);
        return o;
    }
};

template <>
struct pack<whiteboard::Rect> {
    template <typename Stream>
    msgpack::packer<Stream>& operator()(msgpack::packer<Stream>& o, const whiteboard::Rect& v) const {
        o.pack_array(4);
        o.pack(v.x);
        o.pack(v.y);
        o.pack(v.width);
        o.pack(v.height);
        return o;
    }
};

template <>
struct convert<whiteboard::Rect> {
    msgpack::object const& operator()(msgpack::object const& o, whiteboard::Rect& v) const {
        if (o.type != msgpack::type::ARRAY || o.via.array.size != 4)
            throw msgpack::type_error();
        o.via.array.ptr[0].convert(v.x);
        o.via.array.ptr[1].convert(v.y);
        o.via.array.ptr[2].convert(v.width);
        o.via.array.ptr[3].convert(v.height);
        return o;
    }
};

template <>
struct pack<whiteboard::BoundaryRect> {
    template <typename Stream>
    msgpack::packer<Stream>& operator()(msgpack::packer<Stream>& o, const whiteboard::BoundaryRect& v) const {
        o.pack_array(4);
        o.pack(v.minx);
        o.pack(v.maxx);
        o.pack(v.miny);
        o.pack(v.maxy);
        return o;
    }
};

template <>
struct convert<whiteboard::BoundaryRect> {
    msgpack::object const& operator()(msgpack::object const& o, whiteboard::BoundaryRect& v) const {
        if (o.type != msgpack::type::ARRAY || o.via.array.size != 4)
            throw msgpack::type_error();
        o.via.array.ptr[0].convert(v.minx);
        o.via.array.ptr[1].convert(v.maxx);
        o.via.array.ptr[2].convert(v.miny);
        o.via.array.ptr[3].convert(v.maxy);
        return o;
    }
};

template <>
struct pack<whiteboard::Transform> {
    template <typename Stream>
    msgpack::packer<Stream>& operator()(msgpack::packer<Stream>& o, const whiteboard::Transform& v) const {
        o.pack_array(4);
        o.pack(v.scale);
        o.pack(v.translateX);
        o.pack(v.translateY);
        o.pack(v.rotation);
        return o;
    }
};

template <>
struct convert<whiteboard::Transform> {
    msgpack::object const& operator()(msgpack::object const& o, whiteboard::Transform& v) const {
        if (o.type != msgpack::type::ARRAY || o.via.array.size != 4)
            throw msgpack::type_error();
        o.via.array.ptr[0].convert(v.scale);
        o.via.array.ptr[1].convert(v.translateX);
        o.via.array.ptr[2].convert(v.translateY);
        o.via.array.ptr[3].convert(v.rotation);
        return o;
    }
};

} // namespace adaptor
} // MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
} // namespace msgpack