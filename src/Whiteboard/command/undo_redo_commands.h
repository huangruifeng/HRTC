#pragma once
#include "Whiteboard/command/command.h"

namespace whiteboard {

// 撤销/重做指令：对 cmd.pageId 指向的页面执行一次撤销/重做。
// 接收端维护与发送端一致的操作历史（保留最近 20 次）。
class Undo : public Command {
public:
    std::string GetType() const override { return "Undo"; }
};

class Redo : public Command {
public:
    std::string GetType() const override { return "Redo"; }
};

} // namespace whiteboard
