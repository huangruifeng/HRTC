#include "Whiteboard/command/command.h"
#include "Whiteboard/command/stroke_commands.h"
#include "Whiteboard/command/eraser_commands.h"
#include "Whiteboard/command/element_commands.h"
#include "Whiteboard/command/full_sync_command.h"
#include "Whiteboard/command/transform_commands.h"
#include "Whiteboard/command/page_commands.h"
#include "Whiteboard/command/undo_redo_commands.h"
#include "Whiteboard/command/preview_commands.h"
#include <map>

namespace whiteboard {

namespace {

std::map<std::string, CommandFactory>& Factories()
{
    static std::map<std::string, CommandFactory> factories = {
        { "StrokeBegin",   []() -> std::shared_ptr<Command> { return std::make_shared<StrokeBegin>(); } },
        { "StrokeMove",    []() -> std::shared_ptr<Command> { return std::make_shared<StrokeMove>(); } },
        { "StrokeEnd",     []() -> std::shared_ptr<Command> { return std::make_shared<StrokeEnd>(); } },
        { "EraserBegin",   []() -> std::shared_ptr<Command> { return std::make_shared<EraserBegin>(); } },
        { "EraserMove",    []() -> std::shared_ptr<Command> { return std::make_shared<EraserMove>(); } },
        { "EraserEnd",     []() -> std::shared_ptr<Command> { return std::make_shared<EraserEnd>(); } },
        { "ElementAdd",    []() -> std::shared_ptr<Command> { return std::make_shared<ElementAdd>(); } },
        { "ElementRemove", []() -> std::shared_ptr<Command> { return std::make_shared<ElementRemove>(); } },
        { "ElementUpdate", []() -> std::shared_ptr<Command> { return std::make_shared<ElementUpdate>(); } },
        { "StrokeUpdate",  []() -> std::shared_ptr<Command> { return std::make_shared<StrokeUpdate>(); } },
        { "PageCreate",    []() -> std::shared_ptr<Command> { return std::make_shared<PageCreate>(); } },
        { "PageSelect",    []() -> std::shared_ptr<Command> { return std::make_shared<PageSelect>(); } },
        { "PageDelete",    []() -> std::shared_ptr<Command> { return std::make_shared<PageDelete>(); } },
        { "PageClear",     []() -> std::shared_ptr<Command> { return std::make_shared<PageClear>(); } },
        { "Undo",          []() -> std::shared_ptr<Command> { return std::make_shared<Undo>(); } },
        { "Redo",          []() -> std::shared_ptr<Command> { return std::make_shared<Redo>(); } },
        { "FullSync",      []() -> std::shared_ptr<Command> { return std::make_shared<FullSync>(); } },
        { "LassoPreview",     []() -> std::shared_ptr<Command> { return std::make_shared<LassoPreview>(); } },
        { "SelectionPreview", []() -> std::shared_ptr<Command> { return std::make_shared<SelectionPreview>(); } },
    };
    return factories;
}

} // namespace

bool RegisterCommandFactory(const std::string& type, CommandFactory factory)
{
    return Factories().emplace(type, factory).second;
}

std::shared_ptr<Command> CreateCommand(const std::string& type)
{
    auto& factories = Factories();
    const auto it = factories.find(type);
    if (it == factories.end() || !it->second)
        return nullptr;
    return it->second();
}

} // namespace whiteboard