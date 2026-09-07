#pragma once
#include <memory>
#include <string>

namespace whiteboard {

// Base type for whiteboard network commands. Commands are plain data (no
// serialization logic here); conversion to/from network binary is done by the
// protocol layer.
class Command {
public:
    virtual ~Command() = default;

    virtual std::string GetType() const = 0;

    std::string pageId;   // every command targets a specific page
};

using CommandFactory = std::shared_ptr<Command> (*)();

bool RegisterCommandFactory(const std::string& type, CommandFactory factory);
std::shared_ptr<Command> CreateCommand(const std::string& type);

} // namespace whiteboard