#include "Whiteboard/command/command.h"
#include "Whiteboard/command/stroke_commands.h"
#include "Whiteboard/command/eraser_commands.h"
#include "Whiteboard/command/element_commands.h"
#include "Whiteboard/command/full_sync_command.h"
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
        { "FullSync",      []() -> std::shared_ptr<Command> { return std::make_shared<FullSync>(); } },
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