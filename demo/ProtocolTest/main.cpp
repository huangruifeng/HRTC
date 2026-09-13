// 白板协议命令 round-trip 测试：验证各命令序列化 -> 反序列化后字段完整一致。
// 覆盖本轮新增命令（StrokeUpdate / PageCreate / PageSelect / PageDelete /
// PageClear / Undo / Redo）与既有命令的 sanity 检查。
#include "Whiteboard/protocol/protocol.h"
#include "Transport/transport.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "Whiteboard/command/stroke_commands.h"
#include "Whiteboard/command/eraser_commands.h"
#include "Whiteboard/command/transform_commands.h"
#include "Whiteboard/command/page_commands.h"
#include "Whiteboard/command/undo_redo_commands.h"
#include "Whiteboard/command/full_sync_command.h"
#include "Whiteboard/command/preview_commands.h"

namespace {
int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

template <typename T>
std::shared_ptr<T> RoundTrip(const whiteboard::Command& cmd) {
    const std::string data = whiteboard::protocol::SerializeCommand(cmd);
    auto back = whiteboard::protocol::DeserializeCommand(data);
    CHECK(back != nullptr);
    auto typed = std::dynamic_pointer_cast<T>(back);
    CHECK(typed != nullptr);
    CHECK(back->pageId == cmd.pageId);
    return typed;
}
}  // namespace

int main() {
    using namespace whiteboard;

    // StrokeUpdate
    {
        StrokeUpdate c;
        c.pageId = "page-1";
        c.strokeId = "stroke-9";
        c.points = { Point(1, 2), Point(3, 4), Point(-5, 6) };
        auto r = RoundTrip<StrokeUpdate>(c);
        if (r) {
            CHECK(r->strokeId == "stroke-9");
            CHECK(r->points.size() == 3);
            if (r->points.size() == 3) {
                CHECK(r->points[0].x == 1 && r->points[0].y == 2);
                CHECK(r->points[1].x == 3 && r->points[1].y == 4);
                CHECK(r->points[2].x == -5 && r->points[2].y == 6);
            }
        }
    }

    // PageCreate
    {
        PageCreate c;
        c.pageId = "page-1";
        c.newPageId = "page-new-42";
        auto r = RoundTrip<PageCreate>(c);
        if (r)
            CHECK(r->newPageId == "page-new-42");
    }

    // PageSelect / PageDelete / PageClear / Undo / Redo（仅 pageId）
    {
        PageSelect c;
        c.pageId = "page-3";
        RoundTrip<PageSelect>(c);
    }
    {
        PageDelete c;
        c.pageId = "page-7";
        RoundTrip<PageDelete>(c);
    }
    {
        PageClear c;
        c.pageId = "page-7";
        RoundTrip<PageClear>(c);
    }
    {
        Undo c;
        c.pageId = "page-7";
        RoundTrip<Undo>(c);
    }
    {
        Redo c;
        c.pageId = "page-7";
        RoundTrip<Redo>(c);
    }

    // 既有命令 sanity：StrokeBegin
    {
        StrokeBegin c;
        c.pageId = "page-1";
        c.strokeId = "s-1";
        c.width = 5;
        c.color = 0x00FF0000;
        c.points = { Point(10, 20), Point(30, 40) };
        auto r = RoundTrip<StrokeBegin>(c);
        if (r) {
            CHECK(r->strokeId == "s-1");
            CHECK(r->width == 5);
            CHECK(r->color == 0x00FF0000u);
            CHECK(r->points.size() == 2);
        }
    }

    // 既有命令 sanity：FullSync 含一个带笔画的页面
    {
        FullSync c;
        c.pageId = "";
        Page p;
        p.pageId = "page-5";
        Stroke s;
        s.id = "st-1";
        s.width = 3;
        s.color = 0x00FFFFFF;
        s.points = { Point(0, 0), Point(100, 100) };
        s.rawPoints = s.points;  // 序列化走 rawPoints，数据层两者始终同步
        p.Append(s);
        c.pages.push_back(p);
        auto r = RoundTrip<FullSync>(c);
        if (r) {
            CHECK(r->pages.size() == 1);
            if (!r->pages.empty()) {
                CHECK(r->pages[0].pageId == "page-5");
                CHECK(r->pages[0].elements.size() == 1);
                auto* st = dynamic_cast<const Stroke*>(r->pages[0].elements.front().get());
                CHECK(st != nullptr);
                if (st) {
                    CHECK(st->id == "st-1");
                    CHECK(st->points.size() == 2);
                }
            }
        }
    }

    // 橡皮命令（带 sessionId）：EraserBegin / EraserMove / EraserEnd
    {
        EraserBegin c;
        c.pageId = "page-2";
        c.sessionId = "erase-session-7";
        c.points = { Point(0, 0), Point(20, 0), Point(20, 30), Point(0, 30) };
        auto r = RoundTrip<EraserBegin>(c);
        if (r) {
            CHECK(r->sessionId == "erase-session-7");
            CHECK(r->points.size() == 4);
        }
    }
    {
        EraserMove c;
        c.pageId = "page-2";
        c.sessionId = "erase-session-7";
        c.points = { Point(5, 5), Point(25, 5), Point(25, 35), Point(5, 35) };
        auto r = RoundTrip<EraserMove>(c);
        if (r) {
            CHECK(r->sessionId == "erase-session-7");
            CHECK(r->points.size() == 4);
        }
    }
    {
        EraserEnd c;
        c.pageId = "page-2";
        c.sessionId = "erase-session-7";
        c.removedIds = { "s-a", "s-b" };
        c.addedElements.push_back(std::make_shared<Stroke>());
        auto r = RoundTrip<EraserEnd>(c);
        if (r) {
            CHECK(r->sessionId == "erase-session-7");
            CHECK(r->removedIds.size() == 2);
            CHECK(r->removedIds[0] == "s-a");
            CHECK(r->removedIds[1] == "s-b");
            CHECK(r->addedElements.size() == 1);
        }
    }

    // 预览命令：LassoPreview / SelectionPreview 编解码往返
    {
        LassoPreview c;
        c.pageId = "page-3";
        c.sessionId = "lasso-session-2";
        c.points = { Point(10, 10), Point(50, 60), Point(90, 20) };
        auto r = RoundTrip<LassoPreview>(c);
        if (r) {
            CHECK(r->sessionId == "lasso-session-2");
            CHECK(r->points.size() == 3);
            if (r->points.size() == 3) {
                CHECK(r->points[0].x == 10 && r->points[0].y == 10);
                CHECK(r->points[2].x == 90 && r->points[2].y == 20);
            }
        }
    }
    {
        SelectionPreview c;
        c.pageId = "page-3";
        c.sessionId = "sel-session-3";
        c.points = { Point(0, 0), Point(100, 0), Point(100, 50), Point(0, 50) };
        c.selectedIds = { "st-x", "st-y" };
        auto r = RoundTrip<SelectionPreview>(c);
        if (r) {
            CHECK(r->sessionId == "sel-session-3");
            CHECK(r->points.size() == 4);
            CHECK(r->selectedIds.size() == 2);
            CHECK(r->selectedIds[0] == "st-x");
            CHECK(r->selectedIds[1] == "st-y");
        }
    }
    {
        // 清除选择：空 points + 空 selectedIds
        SelectionPreview c;
        c.pageId = "page-3";
        c.sessionId = "sel-session-3";
        auto r = RoundTrip<SelectionPreview>(c);
        if (r) {
            CHECK(r->sessionId == "sel-session-3");
            CHECK(r->points.empty());
            CHECK(r->selectedIds.empty());
        }
    }

    // Transport 数据流：REQUEST_DATA 消息编解码
    {
        hrtc::DataStreamType req;
        req.type = static_cast<int>(hrtc::DataStreamType::Type::REQUEST_DATA);
        req.param = { "user-new", "room-9" };
        const std::string data = hrtc::SerializeStream(req);
        hrtc::DataStreamType back;
        CHECK(hrtc::DeserializeStream(data, back));
        if (back.type == req.type) {
            CHECK(back.param.size() == 2);
            CHECK(back.param[0] == "user-new");
            CHECK(back.param[1] == "room-9");
        }
    }

    if (g_failures == 0) {
        std::printf("ALL PROTOCOL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d PROTOCOL TEST(S) FAILED\n", g_failures);
    return 1;
}
