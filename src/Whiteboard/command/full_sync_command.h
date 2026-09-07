#pragma once
#include "Whiteboard/command/command.h"
#include "Whiteboard/page/page.h"

namespace whiteboard {

class FullSync : public Command {
public:
    std::string GetType() const override { return "FullSync"; }

    Page page;
};

} // namespace whiteboard