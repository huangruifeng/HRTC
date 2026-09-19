#include "Whiteboard/element/element.h"
#include "Whiteboard/element/stroke.h"
#include "Whiteboard/element/graphic.h"
#include "Whiteboard/element/table.h"
#include "Whiteboard/element/mind_map.h"
#include "Whiteboard/element/text.h"
#include "Whiteboard/element/widget.h"
#include "Whiteboard/page/page.h"
#include <map>

namespace whiteboard {

namespace {

std::map<std::string, ElementFactory>& Factories()
{
    static std::map<std::string, ElementFactory> factories = {
        { "Stroke",  []() -> std::shared_ptr<Element> { return std::make_shared<Stroke>(); } },
        { "Graphic", []() -> std::shared_ptr<Element> { return std::make_shared<GraphicElement>(); } },
        { "Table",   []() -> std::shared_ptr<Element> { return std::make_shared<TableElement>(); } },
        { "MindMap", []() -> std::shared_ptr<Element> { return std::make_shared<MindMapElement>(); } },
        { "Text",    []() -> std::shared_ptr<Element> { return std::make_shared<TextElement>(); } },
        { "Widget",  []() -> std::shared_ptr<Element> { return std::make_shared<WidgetElement>(); } },
        { "Page",    []() -> std::shared_ptr<Element> { return std::make_shared<Page>(); } },
    };
    return factories;
}

void CloneListInto(const std::list<std::shared_ptr<Element>>& src,
                   std::list<std::shared_ptr<Element>>& dst);

// 思维导图节点树递归深拷贝
std::shared_ptr<MindNode> CloneNode(const std::shared_ptr<MindNode>& node)
{
    if (!node)
        return nullptr;
    auto copy = std::make_shared<MindNode>();
    copy->id = node->id;
    copy->collapsed = node->collapsed;
    copy->children.reserve(node->children.size());
    for (const auto& child : node->children)
        copy->children.push_back(CloneNode(child));
    return copy;
}

} // namespace

bool RegisterElementFactory(const std::string& type, ElementFactory factory)
{
    return Factories().emplace(type, factory).second;
}

std::shared_ptr<Element> CreateElement(const std::string& type)
{
    auto& factories = Factories();
    const auto it = factories.find(type);
    if (it == factories.end() || !it->second)
        return nullptr;
    return it->second();
}

namespace {

void CloneListInto(const std::list<std::shared_ptr<Element>>& src,
                   std::list<std::shared_ptr<Element>>& dst)
{
    for (const auto& e : src)
    {
        auto copy = e ? CloneElement(*e) : nullptr;
        dst.push_back(copy ? copy : e);  // 未知类型保持共享引用（防御）
    }
}

} // namespace

std::shared_ptr<Element> CloneElement(const Element& e)
{
    // 按类型分派深拷贝：Stroke/Graphic 拷贝点集，Table/MindMap 递归深拷贝
    // 子元素/节点树，Page 走自身 Clone（其内部同样递归本函数）。
    if (const auto* stroke = dynamic_cast<const Stroke*>(&e))
        return std::make_shared<Stroke>(*stroke);
    if (const auto* graphic = dynamic_cast<const GraphicElement*>(&e))
        return std::make_shared<GraphicElement>(*graphic);
    if (const auto* table = dynamic_cast<const TableElement*>(&e))
    {
        auto copy = std::make_shared<TableElement>();
        copy->id = table->id;
        copy->bounds = table->bounds;
        copy->rotation = table->rotation;
        copy->rows = table->rows;
        copy->cols = table->cols;
        copy->width = table->width;
        copy->color = table->color;
        copy->cells.clear();
        copy->cells.resize(table->cells.size());
        for (size_t i = 0; i < table->cells.size(); ++i)
            CloneListInto(table->cells[i], copy->cells[i]);
        return copy;
    }
    if (const auto* mind = dynamic_cast<const MindMapElement*>(&e))
    {
        auto copy = std::make_shared<MindMapElement>();
        copy->id = mind->id;
        copy->root = mind->root;
        copy->scaleX = mind->scaleX;
        copy->scaleY = mind->scaleY;
        copy->rotation = mind->rotation;
        copy->width = mind->width;
        copy->color = mind->color;
        copy->rootNode = CloneNode(mind->rootNode);
        return copy;
    }
    if (const auto* text = dynamic_cast<const TextElement*>(&e))
        return std::make_shared<TextElement>(*text);
    if (const auto* widget = dynamic_cast<const WidgetElement*>(&e))
        return std::make_shared<WidgetElement>(*widget);
    if (const auto* page = dynamic_cast<const Page*>(&e))
        return page->Clone();
    return nullptr;
}

} // namespace whiteboard