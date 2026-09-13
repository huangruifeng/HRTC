#pragma once
#include "Whiteboard/command/command.h"
#include "Whiteboard/page/page.h"
#include <vector>

namespace whiteboard {

class FullSync : public Command {
public:
    std::string GetType() const override { return "FullSync"; }

    std::vector<Page> pages;
};

} // namespace whiteboard