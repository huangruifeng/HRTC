#include "Whiteboard/protocol/element_codec.h"
#include "Whiteboard/protocol/geometry_codec.h"

namespace whiteboard {
namespace protocol {

namespace {

void PackStrokeFields(msgpack::packer<msgpack::sbuffer>& pk, const Stroke& s)
{
    pk.pack_array(4);
    pk.pack(s.id);
    pk.pack(s.width);
    pk.pack(s.color);
    pk.pack(s.rawPoints);
}

void UnpackStrokeFields(const msgpack::object& o, Stroke& s)
{
    if (o.type != msgpack::type::ARRAY || o.via.array.size != 4)
        throw msgpack::type_error();

    o.via.array.ptr[0].convert(s.id);
    o.via.array.ptr[1].convert(s.width);
    o.via.array.ptr[2].convert(s.color);
    o.via.array.ptr[3].convert(s.rawPoints);

    // Deserialized strokes carry only the user-drawn points; rebuild the render
    // point set and bounding box, keeping the default minDistance for future inserts.
    s.points = s.rawPoints;
    s.bounding = BoundaryRect();
    for (const auto& p : s.points)
        s.bounding.Update(p.x, p.y);
}

void PackPageFields(msgpack::packer<msgpack::sbuffer>& pk, const Page& p)
{
    pk.pack_array(4);
    pk.pack(p.id);
    pk.pack(p.pageId);
    pk.pack(p.transform);

    pk.pack_array(static_cast<uint32_t>(p.elements.size()));
    for (const auto& e : p.elements)
        PackElement(pk, *e);
}

void UnpackPageFields(const msgpack::object& o, Page& p)
{
    if (o.type != msgpack::type::ARRAY || o.via.array.size != 4)
        throw msgpack::type_error();

    o.via.array.ptr[0].convert(p.id);
    o.via.array.ptr[1].convert(p.pageId);
    o.via.array.ptr[2].convert(p.transform);

    p.elements.clear();
    const msgpack::object& arr = o.via.array.ptr[3];
    if (arr.type != msgpack::type::ARRAY)
        throw msgpack::type_error();

    for (uint32_t i = 0; i < arr.via.array.size; ++i)
    {
        auto e = UnpackElement(arr.via.array.ptr[i]);
        if (e)
            p.elements.push_back(e);
    }

    // 线协议不携带该标志；Page 默认构造关闭橡皮插值，反序列化后统一启用，
    // 否则远程同步得到的页面在 Page::Eraser(rc, sid) 下会静默失效。
    p.EnableEraserInsert(true);
}

} // namespace

std::string SerializeStroke(const Stroke& stroke)
{
    msgpack::sbuffer sbuf;
    msgpack::packer<msgpack::sbuffer> pk(&sbuf);
    PackStrokeFields(pk, stroke);
    return std::string(sbuf.data(), sbuf.size());
}

Stroke DeserializeStroke(const std::string& data)
{
    msgpack::object_handle oh = msgpack::unpack(data.data(), data.size());

    auto e = CreateElement("Stroke");
    auto* stroke = dynamic_cast<Stroke*>(e.get());
    if (!stroke)
        throw msgpack::type_error();

    UnpackStrokeFields(oh.get(), *stroke);
    return *stroke;
}

std::string SerializePage(const Page& page)
{
    msgpack::sbuffer sbuf;
    msgpack::packer<msgpack::sbuffer> pk(&sbuf);
    PackPageFields(pk, page);
    return std::string(sbuf.data(), sbuf.size());
}

Page DeserializePage(const std::string& data)
{
    msgpack::object_handle oh = msgpack::unpack(data.data(), data.size());

    auto e = CreateElement("Page");
    auto* page = dynamic_cast<Page*>(e.get());
    if (!page)
        throw msgpack::type_error();

    UnpackPageFields(oh.get(), *page);
    return *page;
}

std::string SerializeElement(const Element& e)
{
    msgpack::sbuffer sbuf;
    msgpack::packer<msgpack::sbuffer> pk(&sbuf);
    PackElement(pk, e);
    return std::string(sbuf.data(), sbuf.size());
}

std::shared_ptr<Element> DeserializeElement(const std::string& data)
{
    msgpack::object_handle oh = msgpack::unpack(data.data(), data.size());
    return UnpackElement(oh.get());
}

void PackElement(msgpack::packer<msgpack::sbuffer>& pk, const Element& e)
{
    pk.pack_array(2);
    pk.pack(e.GetType());

    if (auto* stroke = dynamic_cast<const Stroke*>(&e))
        PackStrokeFields(pk, *stroke);
    else if (auto* page = dynamic_cast<const Page*>(&e))
        PackPageFields(pk, *page);
    else
        pk.pack_nil();
}

std::shared_ptr<Element> UnpackElement(const msgpack::object& obj)
{
    if (obj.type != msgpack::type::ARRAY || obj.via.array.size != 2)
        throw msgpack::type_error();

    std::string type;
    obj.via.array.ptr[0].convert(type);

    auto e = CreateElement(type);
    if (!e)
        return nullptr;

    const msgpack::object& fields = obj.via.array.ptr[1];
    if (auto* stroke = dynamic_cast<Stroke*>(e.get()))
        UnpackStrokeFields(fields, *stroke);
    else if (auto* page = dynamic_cast<Page*>(e.get()))
        UnpackPageFields(fields, *page);

    return e;
}

} // namespace protocol
} // namespace whiteboard