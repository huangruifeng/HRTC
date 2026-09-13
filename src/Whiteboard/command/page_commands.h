#pragma once
#include "Whiteboard/command/command.h"
#include <string>

namespace whiteboard {

// 页面管理命令：pageId 复用基类字段。
// PageCreate 额外携带新页面 id（接收端按此 id 创建空白页并切换）。
class PageCreate : public Command {
public:
    std::string GetType() const override { return "PageCreate"; }

    std::string newPageId;
};

// 切换当前页到 cmd.pageId
class PageSelect : public Command {
public:
    std::string GetType() const override { return "PageSelect"; }
};

// 删除 cmd.pageId 指定的页面
class PageDelete : public Command {
public:
    std::string GetType() const override { return "PageDelete"; }
};

// 清空 cmd.pageId 指定的页面
class PageClear : public Command {
public:
    std::string GetType() const override { return "PageClear"; }
};

} // namespace whiteboard
