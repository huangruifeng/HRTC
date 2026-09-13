#pragma once
#include "Headers/HrtcEngine.h"
#include "Whiteboard/whiteboard.h"
#include "Whiteboard/command/command.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// 白板数据层封装（纯 C++，不依赖 Qt）：
// - 所有 Page/Stroke 数据操作都在数据线程串行执行；
// - 回调在数据线程触发，由上层自行投递回 UI 线程；
// - 橡皮擦每步实时回调增量结果（removedIds + addedElements），支持局部重绘；
// - 多页面管理：CreatePage / SelectPage / DeletePage，画笔与橡皮擦作用于当前页；
// - 撤销/重做：每页独立历史栈（保留最近 20 次），撤销/重做后回调全量刷新。
class QtBoardData {
public:
    // 橡皮擦增量：removed = 被擦除元素 id；added = 擦除切割产生的新笔画碎片
    using ElementsChangedCb = std::function<void(std::vector<std::string> removed,
                                                 std::vector<std::shared_ptr<whiteboard::Element>> added)>;
    // 一笔落库：token 用于匹配 UI 侧预览笔画
    using StrokeCommittedCb = std::function<void(uint64_t token, const std::string& id)>;
    using ClearedCb = std::function<void()>;
    // 页面整体被替换（撤销/重做/远程页面操作），UI 需全量重建
    using PageChangedCb = std::function<void()>;
    // 本地操作产生的命令（需广播到房间内其他用户）；在数据线程触发
    using OutgoingCmdCb = std::function<void(std::shared_ptr<whiteboard::Command>)>;
    // 远程笔画实时预览（Begin/Move 触发，含初始点集）
    using StrokePreviewCb = std::function<void(std::string strokeId, uint32_t color, int width,
                                              std::vector<whiteboard::Point> points)>;
    // 远程工具预览（不落库，仅 UI 展示）：tool 1=橡皮框(4点矩形) 2=套索路径 3=选择框(4顶点)
    // points 为空表示清除该会话预览；elementIds 供选择框高亮（可空）
    using ToolPreviewCb = std::function<void(uint32_t tool, std::string sessionId,
                                             std::vector<whiteboard::Point> points,
                                             std::vector<std::string> elementIds)>;
    // FullSync 应用后触发：UI 全量重建所有页面状态
    using SyncedCb = std::function<void()>;

    QtBoardData();
    ~QtBoardData();

    // 回调设置/移除均可跨线程调用（内部投递到数据线程串行执行）
    void SetCallbacks(ElementsChangedCb elementsChanged,
                      StrokeCommittedCb strokeCommitted,
                      ClearedCb cleared,
                      PageChangedCb pageChanged,
                      StrokePreviewCb strokePreview,
                      ToolPreviewCb toolPreview,
                      SyncedCb synced);
    // 命令产出回调单独注册（由互动会话层设置，不覆盖渲染回调）
    void SetOutgoingCmdCb(OutgoingCmdCb outgoingCmd);
    void RemoveCallbacks();  // 内部带线程屏障，返回后不再有任何回调

    // 画笔（作用于当前页）
    void PenBegin(const whiteboard::Point& p, int sessionId, uint64_t token);
    void PenMove(const whiteboard::Point& p, int sessionId);
    void PenEnd(const whiteboard::Point& p, int sessionId);
    // 橡皮擦：每一步都实时回调增量结果
    void EraserBegin(const whiteboard::Rect& rc, int sessionId);
    void EraserMove(const whiteboard::Rect& rc, int sessionId);
    void EraserEnd(const whiteboard::Rect& rc, int sessionId);
    void Clear();
    // 全量导出当前页（初始化/页面切换全量重建用）
    void GetPage(whiteboard::Page& page);

    // 元素变换烘焙：用新点集替换指定笔画（id/color/width 不变，重算包围盒）
    void UpdateStroke(const std::string& id, const std::vector<whiteboard::Point>& points);

    // ---------- 远程命令应用（经数据线程投递，不产生 OutgoingCmdCb） ----------
    // 远程笔画：Begin/Move 触发 StrokePreviewCb 实时预览；End 落库 + onElementsChanged
    void RemoteStrokeBegin(const std::string& strokeId, int width, uint32_t color,
                           const std::vector<whiteboard::Point>& points);
    void RemoteStrokeMove(const std::string& strokeId, const std::vector<whiteboard::Point>& points);
    void RemoteStrokeEnd(const std::string& strokeId, const std::vector<whiteboard::Point>& points);
    // 远程橡皮：操作端为主——Begin/Move 仅触发 ToolPreviewCb 供 UI 实时预览擦除（数据层不动）；
    // End 携带操作端最终结果（removedIds + addedElements）一次性同步数据层并增量回调渲染。
    void RemoteEraserBegin(const std::string& sessionId, const std::vector<whiteboard::Point>& points);
    void RemoteEraserMove(const std::string& sessionId, const std::vector<whiteboard::Point>& points);
    void RemoteEraserEnd(const std::string& sessionId,
                         const std::vector<std::string>& removedIds,
                         const std::vector<std::shared_ptr<whiteboard::Element>>& addedElements);
    // 远程套索/选择预览透传（不落库，仅触发 ToolPreviewCb）
    void RemoteLassoPreview(const std::string& sessionId,
                            const std::vector<whiteboard::Point>& points);
    void RemoteSelectionPreview(const std::string& sessionId,
                                const std::vector<whiteboard::Point>& points,
                                const std::vector<std::string>& selectedIds);
    void RemoteUpdateStroke(const std::string& id, const std::vector<whiteboard::Point>& points);
    void RemoteClear();
    void RemoteCreatePage(const std::string& pageId);
    void RemoteSelectPage(const std::string& pageId);
    void RemoteDeletePage(const std::string& pageId);
    void RemoteUndo();
    void RemoteRedo();
    // 全量同步：替换全部页面并清空历史（触发 SyncedCb）
    void ApplyFullSync(const std::vector<whiteboard::Page>& pages);
    // 全量导出所有页面（FullSync 应答用，同步等待）
    void GetAllPages(std::vector<whiteboard::Page>& pages);

    // 撤销/重做（同步返回是否生效；成功后触发 PageChanged 回调）
    bool Undo();
    bool Redo();

    // 页面管理（同步返回结果）
    std::vector<std::string> GetPageIds();
    std::string GetCurrentPageId();
    std::string CreatePage();                        // 新建空白页并切换为当前页
    void SelectPage(const std::string& pageId);      // 切换当前页（丢弃未完成笔画/橡皮增量）
    bool DeletePage(const std::string& pageId);      // 至少保留一页，成功后自动调整当前页

    // ---------- 本地操作产生的工具预览命令（套索/选择，供远端 UI 实时展示） ----------
    void SendToolPreview(uint32_t tool, const std::string& sessionId,
                         const std::vector<whiteboard::Point>& points,
                         const std::vector<std::string>& elementIds);

    void SetColor(uint32_t color);   // COLORREF 语义 (0x00BBGGRR)
    void SetPenWidth(int width);

private:
    // 每页独立的历史栈：undo = 操作前快照；redo = 撤销前的状态
    struct PageHistory {
        std::deque<std::shared_ptr<whiteboard::Page>> undo;
        std::deque<std::shared_ptr<whiteboard::Page>> redo;
    };

    void PushSnapshot();  // 仅数据线程内调用：记录操作前状态到 undo 栈
    // 收集橡皮增量并清空累积（返回值：过滤后的 removed/added，供 EraserEnd 命令使用）
    std::pair<std::vector<std::string>, std::vector<std::shared_ptr<whiteboard::Element>>>
    FlushEraser(bool clearAccum);
    std::shared_ptr<whiteboard::Page>& CurrentPage();  // 仅数据线程内调用
    // 本地/远程笔画会话 key（本地 "l:<sessionId>"，远程 "r:<strokeId>"）
    static std::string LocalStrokeKey(int sessionId) { return "l:" + std::to_string(sessionId); }
    static std::string RemoteStrokeKey(const std::string& strokeId) { return "r:" + strokeId; }
    // 远程笔画全量点集替换（重算包围盒）
    void ReplaceStrokePoints(whiteboard::Stroke& stroke, const std::vector<whiteboard::Point>& points);
    // 通用：丢弃未完成笔画与橡皮增量（切页/撤销/重做/同步后调用）
    void ResetTransientState();

    std::shared_ptr<hrtc::IThread> thread_;
    std::vector<std::shared_ptr<whiteboard::Page>> pages_;
    size_t currentPage_ = 0;
    // 多会话并行笔画（本地按键 "l:<sessionId>"；远程按 "r:<strokeId>"；End 后移除）
    std::map<std::string, whiteboard::Stroke> strokes_;

    std::atomic<uint32_t> color_{ 0x00FFFFFF };  // 默认白色粉笔
    std::atomic<int> penWidth_{ 3 };

    // 以下状态仅在数据线程访问
    ElementsChangedCb onElementsChanged_;
    StrokeCommittedCb onStrokeCommitted_;
    ClearedCb onCleared_;
    PageChangedCb onPageChanged_;
    OutgoingCmdCb onOutgoingCmd_;
    StrokePreviewCb onStrokePreview_;
    ToolPreviewCb onToolPreview_;
    SyncedCb onSynced_;
    uint64_t pendingToken_ = 0;
    std::set<std::string> eraserRemoved_;
    std::vector<std::shared_ptr<whiteboard::Element>> eraserAdded_;
    std::map<std::string, PageHistory> histories_;  // pageId -> 历史栈

    static constexpr size_t kHistoryLimit = 20;
};
