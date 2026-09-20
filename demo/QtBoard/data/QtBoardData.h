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
    // 橡皮擦增量：removed = 被擦除元素 id；added = 擦除切割产生的新笔画碎片；
    // placements 与 added 一一对齐（归属位置：parentId 空 = 页面级追加）
    using ElementsChangedCb = std::function<void(std::vector<std::string> removed,
                                                 std::vector<std::shared_ptr<whiteboard::Element>> added,
                                                 std::vector<whiteboard::EraserPlacement> placements)>;
    // 一笔落库：token 用于匹配 UI 侧预览笔画；
    // 起点归属表格单元格时 parentId 非空（否则 parentId 空、cellIndex = -1）
    using StrokeCommittedCb = std::function<void(uint64_t token, const std::string& id,
                                                 const std::string& parentId, int cellIndex)>;
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
    // 按 id 深拷贝元素快照（同步等待；表格整表重建用，找不到返回 nullptr）
    std::shared_ptr<whiteboard::Element> GetElementSnapshot(const std::string& id);

    // 元素变换烘焙：用新点集替换指定笔画（id/color/width 不变，重算包围盒）
    void UpdateStroke(const std::string& id, const std::vector<whiteboard::Point>& points);

    // ---------- 新增元素（图形/思维导图/表格）：本地操作 ----------
    // 当前页追加元素（PushSnapshot + ElementAdd 广播 + 增量回调）
    void AddElement(std::shared_ptr<whiteboard::Element> element);
    // 按 id 递归删除元素（页面级 / 表格单元格内任意元素）；删除表格级联删除
    // 全部子元素（removed 回调含级联 id）；每个顶层 id 广播一次 ElementRemove
    void RemoveElements(const std::vector<std::string>& ids);
    // 图形几何静默写回（不触发 onElementsChanged，UI 自行保持同步）+ ElementUpdate 广播
    void UpdateGraphicGeometry(const std::string& id, const std::vector<whiteboard::Subpath>& subpaths);
    // 整表静默写回（变换烘焙用）：origin/rotation + 初始格尺寸 + 全部子元素整组替换；
    // UI 已本地重建保证视觉零跳变（不触发 onElementsChanged）+ ElementUpdate 广播（整表快照）
    void UpdateTableElement(std::shared_ptr<whiteboard::TableElement> table);

    // ---------- 文字元素：本地操作 ----------
    // 几何静默写回（锚点 + 字号 + rotation 度，顺时针正角）+ ElementUpdate 广播
    // （变换烘焙用；UI 自行保持同步）
    void UpdateTextGeometry(const std::string& id, int x, int y, int fontSize, float rotation);
    // 文本内容编辑：PushSnapshot → 改 text + 字形包围盒 bounds（UI 用渲染端字体引擎
    // 重算）→ 深拷贝快照广播（ElementUpdate）+ 增量回调（removed={id}/added={快照}），
    // UI 与远端走同一重建路径；文本位于表格格内时改广播整表快照（布局可能变化）
    void UpdateTextContent(const std::string& id, const std::string& text,
                           const whiteboard::Rect& bounds);

    // ---------- 小工具元素：本地操作 ----------
    // 几何静默写回（卡片左上角 + 等比缩放 0.5~3.0）+ ElementUpdate 广播
    // （变换烘焙用；UI 自行保持同步）
    void UpdateWidgetGeometry(const std::string& id, int x, int y, float scale);
    // 计时器时长静默写回（卡片内滚轮设置态点开始时提交；不入撤销栈，防滚轮刷爆
    // 历史）+ ElementUpdate 广播
    void UpdateWidgetDuration(const std::string& id, int durationSec);
    // 骰子参数静默写回（面数 4/6/8/12/20 + 颗数 1~10；卡片设置态点确定时提交）
    // + ElementUpdate 广播
    void UpdateWidgetDiceParams(const std::string& id, int sides, int count);
    // 转盘/点名器候选项写回（文本内容，UTF-8 每行一项；卡片编辑弹窗提交时调用）：
    // PushSnapshot → 改 options → 深拷贝快照广播 + removed/added 增量回调
    // （UI 与远端走同一重建路径，可撤销）
    void UpdateWidgetOptions(const std::string& id, const std::string& options);
    // 去重模式开关静默写回（抽中自动移出；不入撤销栈）+ ElementUpdate 广播
    void UpdateWidgetDedup(const std::string& id, bool dedup);
    // 点名器一次抽取人数静默写回（1 ~ 5；不入撤销栈）+ ElementUpdate 广播
    void UpdateWidgetPickCount(const std::string& id, int pickCount);

    // ---------- 思维导图（树状结构操作）：本地操作 ----------
    // 均为：PushSnapshot → 改元素 → 深拷贝快照广播（ElementUpdate）+ 增量回调
    // （removed={id}/added={快照}），UI 与远端走同一重建路径。
    // 整体变换烘焙写回（root + scale + rotation 度，顺时针正角）
    void UpdateMindMapGeometry(const std::string& id, const whiteboard::Point& root,
                               float scaleX, float scaleY, float rotation);
    // 展开/收缩指定节点（collapsed 取反）
    void MindMapToggleNode(const std::string& id, const std::string& nodeId);
    // 在指定节点下追加一个子节点（自动布局重排；父节点确保展开）
    void MindMapAddChild(const std::string& id, const std::string& nodeId);
    // 删除指定节点及其子树（根节点拒绝）
    void MindMapRemoveNode(const std::string& id, const std::string& nodeId);

    // ---------- 批量快照（撤销粒度合并） ----------
    // BeginBatch：记录一次快照后抑制批内所有 Update* 各自快照；EndBatch 解除抑制。
    // 用于一次变换烘焙产生多条 Update*（表格 + 展开的格子笔迹×N）时合并为单个撤销步。
    // 调用顺序经数据线程 FIFO 投递保证：BeginBatch → Update* → EndBatch。
    void BeginBatch();
    void EndBatch();

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
                         const std::vector<std::shared_ptr<whiteboard::Element>>& addedElements,
                         const std::vector<whiteboard::EraserPlacement>& addedPlacements);
    // 远程套索/选择预览透传（不落库，仅触发 ToolPreviewCb）
    void RemoteLassoPreview(const std::string& sessionId,
                            const std::vector<whiteboard::Point>& points);
    void RemoteSelectionPreview(const std::string& sessionId,
                                const std::vector<whiteboard::Point>& points,
                                const std::vector<std::string>& selectedIds);
    void RemoteUpdateStroke(const std::string& id, const std::vector<whiteboard::Point>& points);
    // 远程元素增删改（不产生 OutgoingCmdCb；更新走 removed={id}+added={新元素} 增量模式）
    void RemoteAddElement(const std::shared_ptr<whiteboard::Element>& element);
    void RemoteRemoveElements(const std::vector<std::string>& ids);
    void RemoteUpdateElement(const std::shared_ptr<whiteboard::Element>& element);
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
    bool CopyPage(const std::string& pageId);        // 复制页（元素深拷贝+新 id，插入源页后并选中）

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
    // 橡皮擦增量（removed/added + 与 added 一一对齐的归属位置）
    struct EraserDelta {
        std::vector<std::string> removedIds;
        std::vector<std::shared_ptr<whiteboard::Element>> addedElements;
        std::vector<whiteboard::EraserPlacement> addedPlacements;
    };
    // 收集橡皮增量并清空累积（返回值供 EraserEnd 命令使用）
    EraserDelta FlushEraser(bool clearAccum);
    // 思维导图变更统一收尾（仅数据线程调用）：深拷贝快照 → ElementUpdate 广播
    // + removed={id}/added={快照} 增量回调（快照脱离活对象，防跨线程读改竞态）
    void NotifyMindMapChanged(const whiteboard::MindMapElement& mind);
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
    std::vector<whiteboard::EraserPlacement> eraserAddedPlacements_;  // 与 eraserAdded_ 对齐
    std::map<std::string, PageHistory> histories_;  // pageId -> 历史栈
    bool batchSuppress_ = false;  // 批内抑制 PushSnapshot（仅数据线程访问）

    static constexpr size_t kHistoryLimit = 20;
};
