#include "Whiteboard/element/element.h"
#include "Whiteboard/element/stroke.h"
#include "Whiteboard/page/page.h"
#include <map>

namespace whiteboard {

namespace {

std::map<std::string, ElementFactory>& Factories()
{
    static std::map<std::string, ElementFactory> factories = {
        { "Stroke", []() -> std::shared_ptr<Element> { return std::make_shared<Stroke>(); } },
        { "Page",   []() -> std::shared_ptr<Element> { return std::make_shared<Page>(); } },
    };
    return factories;
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

} // namespace whiteboard