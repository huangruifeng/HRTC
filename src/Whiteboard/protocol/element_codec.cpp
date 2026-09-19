#include "Whiteboard/protocol/element_codec.h"
#include "Whiteboard/protocol/geometry_codec.h"
#include "Whiteboard/element/graphic.h"
#include "Whiteboard/element/table.h"
#include "Whiteboard/element/mind_map.h"
#include "Whiteboard/element/text.h"
#include "Whiteboard/element/widget.h"

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

void PackGraphicFields(msgpack::packer<msgpack::sbuffer>& pk, const GraphicElement& g)
{
    pk.pack_array(5);
    pk.pack(g.id);
    pk.pack(g.kind);
    pk.pack(g.width);
    pk.pack(g.color);
    pk.pack_array(static_cast<uint32_t>(g.subpaths.size()));
    for (const auto& sp : g.subpaths)
    {
        pk.pack_array(2);
        pk.pack(sp.closed);
        pk.pack(sp.points);
    }
}

void UnpackGraphicFields(const msgpack::object& o, GraphicElement& g)
{
    if (o.type != msgpack::type::ARRAY || o.via.array.size != 5)
        throw msgpack::type_error();

    o.via.array.ptr[0].convert(g.id);
    o.via.array.ptr[1].convert(g.kind);
    o.via.array.ptr[2].convert(g.width);
    o.via.array.ptr[3].convert(g.color);

    const msgpack::object& arr = o.via.array.ptr[4];
    if (arr.type != msgpack::type::ARRAY)
        throw msgpack::type_error();

    g.subpaths.clear();
    g.subpaths.reserve(arr.via.array.size);
    for (uint32_t i = 0; i < arr.via.array.size; ++i)
    {
        const msgpack::object& so = arr.via.array.ptr[i];
        if (so.type != msgpack::type::ARRAY || so.via.array.size != 2)
            throw msgpack::type_error();
        Subpath sp;
        so.via.array.ptr[0].convert(sp.closed);
        so.via.array.ptr[1].convert(sp.points);
        g.subpaths.push_back(std::move(sp));
    }

    // 由点集重算包围盒（同 Stroke 反序列化约定）
    g.Rebuild();
}

void PackTableFields(msgpack::packer<msgpack::sbuffer>& pk, const TableElement& t)
{
    pk.pack_array(10);
    pk.pack(t.id);
    pk.pack(t.origin);
    pk.pack(t.rotation);
    pk.pack(t.rows);
    pk.pack(t.cols);
    pk.pack(t.width);
    pk.pack(t.color);
    pk.pack(t.minCellW);
    pk.pack(t.minCellH);

    // 单元格子元素：任意已注册类型递归 PackElement（含未来嵌套表格）
    pk.pack_array(static_cast<uint32_t>(t.cells.size()));
    for (const auto& cell : t.cells)
    {
        pk.pack_array(static_cast<uint32_t>(cell.size()));
        for (const auto& e : cell)
        {
            if (e)
                PackElement(pk, *e);
            else
                pk.pack_nil();
        }
    }
}

void UnpackTableFields(const msgpack::object& o, TableElement& t)
{
    if (o.type != msgpack::type::ARRAY || o.via.array.size != 10)
        throw msgpack::type_error();

    o.via.array.ptr[0].convert(t.id);
    o.via.array.ptr[1].convert(t.origin);
    o.via.array.ptr[2].convert(t.rotation);
    o.via.array.ptr[3].convert(t.rows);
    o.via.array.ptr[4].convert(t.cols);
    o.via.array.ptr[5].convert(t.width);
    o.via.array.ptr[6].convert(t.color);
    o.via.array.ptr[7].convert(t.minCellW);
    o.via.array.ptr[8].convert(t.minCellH);

    const msgpack::object& arr = o.via.array.ptr[9];
    if (arr.type != msgpack::type::ARRAY)
        throw msgpack::type_error();

    t.cells.clear();
    t.cells.resize(arr.via.array.size);
    for (uint32_t i = 0; i < arr.via.array.size; ++i)
    {
        const msgpack::object& cellObj = arr.via.array.ptr[i];
        if (cellObj.type != msgpack::type::ARRAY)
            continue;
        for (uint32_t j = 0; j < cellObj.via.array.size; ++j)
        {
            const msgpack::object& eo = cellObj.via.array.ptr[j];
            if (eo.type != msgpack::type::ARRAY)
                continue;
            auto e = UnpackElement(eo);
            if (e)
                t.cells[i].push_back(e);
        }
    }

    // 校正尺寸：cells.size() 恒等于 rows*cols（异常包防御）
    const size_t expected = static_cast<size_t>(t.rows > 0 ? t.rows : 0) *
                            static_cast<size_t>(t.cols > 0 ? t.cols : 0);
    t.cells.resize(expected);
}

void PackMindNode(msgpack::packer<msgpack::sbuffer>& pk, const MindNode& node)
{
    pk.pack_array(3);
    pk.pack(node.id);
    pk.pack(node.collapsed);
    pk.pack_array(static_cast<uint32_t>(node.children.size()));
    for (const auto& child : node.children)
    {
        if (child)
            PackMindNode(pk, *child);
        else
            pk.pack_nil();
    }
}

void UnpackMindNode(const msgpack::object& o, MindNode& node)
{
    if (o.type != msgpack::type::ARRAY || o.via.array.size != 3)
        throw msgpack::type_error();

    o.via.array.ptr[0].convert(node.id);
    o.via.array.ptr[1].convert(node.collapsed);

    const msgpack::object& arr = o.via.array.ptr[2];
    if (arr.type != msgpack::type::ARRAY)
        throw msgpack::type_error();

    node.children.clear();
    node.children.reserve(arr.via.array.size);
    for (uint32_t i = 0; i < arr.via.array.size; ++i)
    {
        const msgpack::object& co = arr.via.array.ptr[i];
        if (co.type != msgpack::type::ARRAY)
            continue;
        auto child = std::make_shared<MindNode>();
        UnpackMindNode(co, *child);
        node.children.push_back(std::move(child));
    }
}

void PackMindMapFields(msgpack::packer<msgpack::sbuffer>& pk, const MindMapElement& m)
{
    pk.pack_array(8);
    pk.pack(m.id);
    pk.pack(m.root);
    pk.pack(m.scaleX);
    pk.pack(m.scaleY);
    pk.pack(m.rotation);
    pk.pack(m.width);
    pk.pack(m.color);
    if (m.rootNode)
        PackMindNode(pk, *m.rootNode);
    else
        pk.pack_nil();
}

void UnpackMindMapFields(const msgpack::object& o, MindMapElement& m)
{
    if (o.type != msgpack::type::ARRAY || o.via.array.size != 8)
        throw msgpack::type_error();

    o.via.array.ptr[0].convert(m.id);
    o.via.array.ptr[1].convert(m.root);
    o.via.array.ptr[2].convert(m.scaleX);
    o.via.array.ptr[3].convert(m.scaleY);
    o.via.array.ptr[4].convert(m.rotation);
    o.via.array.ptr[5].convert(m.width);
    o.via.array.ptr[6].convert(m.color);

    // rootNode 恒非空（Reset 语义）；节点包异常时保持空树防御
    m.rootNode = std::make_shared<MindNode>();
    const msgpack::object& nodeObj = o.via.array.ptr[7];
    if (nodeObj.type == msgpack::type::ARRAY)
        UnpackMindNode(nodeObj, *m.rootNode);
}

void PackTextFields(msgpack::packer<msgpack::sbuffer>& pk, const TextElement& t)
{
    pk.pack_array(8);
    pk.pack(t.id);
    pk.pack(t.text);
    pk.pack(t.x);
    pk.pack(t.y);
    pk.pack(t.fontSize);
    pk.pack(t.rotation);
    pk.pack(t.color);
    pk.pack(t.bounds);
}

void UnpackTextFields(const msgpack::object& o, TextElement& t)
{
    if (o.type != msgpack::type::ARRAY || o.via.array.size != 8)
        throw msgpack::type_error();

    o.via.array.ptr[0].convert(t.id);
    o.via.array.ptr[1].convert(t.text);
    o.via.array.ptr[2].convert(t.x);
    o.via.array.ptr[3].convert(t.y);
    o.via.array.ptr[4].convert(t.fontSize);
    o.via.array.ptr[5].convert(t.rotation);
    o.via.array.ptr[6].convert(t.color);
    o.via.array.ptr[7].convert(t.bounds);
}

void PackWidgetFields(msgpack::packer<msgpack::sbuffer>& pk, const WidgetElement& w)
{
    pk.pack_array(11);
    pk.pack(w.id);
    pk.pack(w.kind);
    pk.pack(w.x);
    pk.pack(w.y);
    pk.pack(w.durationSec);
    pk.pack(w.scale);
    pk.pack(w.diceSides);
    pk.pack(w.diceCount);
    pk.pack(w.options);
    pk.pack(w.dedup);
    pk.pack(w.pickCount);
}

void UnpackWidgetFields(const msgpack::object& o, WidgetElement& w)
{
    // 兼容历史 5 字段（无 scale）、6 字段（无骰子参数）、8 字段（无转盘/点名器
    // 设置）与当前 11 字段
    if (o.type != msgpack::type::ARRAY ||
        o.via.array.size < 5 || o.via.array.size > 11)
        throw msgpack::type_error();

    o.via.array.ptr[0].convert(w.id);
    o.via.array.ptr[1].convert(w.kind);
    o.via.array.ptr[2].convert(w.x);
    o.via.array.ptr[3].convert(w.y);
    o.via.array.ptr[4].convert(w.durationSec);
    w.scale = 1.0f;
    if (o.via.array.size >= 6)
        o.via.array.ptr[5].convert(w.scale);
    w.diceSides = 6;
    w.diceCount = 2;
    if (o.via.array.size >= 8) {
        o.via.array.ptr[6].convert(w.diceSides);
        o.via.array.ptr[7].convert(w.diceCount);
    }
    w.options.clear();
    w.dedup = false;
    w.pickCount = 1;
    if (o.via.array.size >= 9)
        o.via.array.ptr[8].convert(w.options);
    if (o.via.array.size >= 10)
        o.via.array.ptr[9].convert(w.dedup);
    if (o.via.array.size >= 11)
        o.via.array.ptr[10].convert(w.pickCount);
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
    else if (auto* graphic = dynamic_cast<const GraphicElement*>(&e))
        PackGraphicFields(pk, *graphic);
    else if (auto* table = dynamic_cast<const TableElement*>(&e))
        PackTableFields(pk, *table);
    else if (auto* mind = dynamic_cast<const MindMapElement*>(&e))
        PackMindMapFields(pk, *mind);
    else if (auto* text = dynamic_cast<const TextElement*>(&e))
        PackTextFields(pk, *text);
    else if (auto* widget = dynamic_cast<const WidgetElement*>(&e))
        PackWidgetFields(pk, *widget);
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
    else if (auto* graphic = dynamic_cast<GraphicElement*>(e.get()))
        UnpackGraphicFields(fields, *graphic);
    else if (auto* table = dynamic_cast<TableElement*>(e.get()))
        UnpackTableFields(fields, *table);
    else if (auto* mind = dynamic_cast<MindMapElement*>(e.get()))
        UnpackMindMapFields(fields, *mind);
    else if (auto* text = dynamic_cast<TextElement*>(e.get()))
        UnpackTextFields(fields, *text);
    else if (auto* widget = dynamic_cast<WidgetElement*>(e.get()))
        UnpackWidgetFields(fields, *widget);
    else if (auto* page = dynamic_cast<Page*>(e.get()))
        UnpackPageFields(fields, *page);

    return e;
}

} // namespace protocol
} // namespace whiteboard