#pragma once
#include <QColor>
#include <QGraphicsView>
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVector>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "QtBoardData.h"
#include "SelectionFrame.h"

class QGraphicsPathItem;
class QGraphicsProxyWidget;
class QGraphicsRectItem;
class QMouseEvent;
class QPainter;
class QResizeEvent;
class QTextEdit;
class QWheelEvent;
class QEvent;
class QPainterPath;
class QTimer;

// 黑板画布视图：捕获鼠标事件驱动白板数据层，并把 Page 数据渲染为图元。
// 线程模型：所有 scene/item 操作仅在 UI 线程；数据层回调经 QueuedConnection 投递回 UI 线程。
// 无限画布：场景为超大矩形（近似无限），初始按 1080p（1920x1080）等比适配显示，
// 支持抓手/中键平移查看屏幕外元素；缩放限制 50%~300%，步进 0.5。
class BoardView : public QGraphicsView {
    Q_OBJECT
public:
    enum class Tool { Pen, Select, Lasso, Eraser, Pan, Shape, MindMap, Table, Text, Widget, Mouse };

    explicit BoardView(QWidget* parent = nullptr);
    ~BoardView() override;

    void setTool(Tool tool);
    Tool currentTool() const { return tool_; }
    void setPenColor(uint32_t color);   // COLORREF 语义 (0x00BBGGRR)
    void setPenWidth(int width);
    void setShapeKind(int kind) { shapeKind_ = kind; }   // 图形工具形状（GraphicKind 整数值）
    int shapeKind() const { return shapeKind_; }         // 当前形状
    void setTableSize(int rows, int cols);               // 表格工具行列数（2~8）
    void setTextFontSize(int size);                      // 文字工具字号（面板选择，像素）
void setTextColor(uint32_t color);                   // 文字工具颜色（面板选择，COLORREF 语义）
    void setWidgetKind(int kind);                        // 小工具类型 0~4（面板选择；参数在卡片内设置）
    void setEraserSize(int size);                        // 橡皮擦高度（横向长方形，宽 = 高×kEraserAspect，档位见 BoardDisk）
    int eraserSize() const { return eraserSize_; }
    void clearBoard();

    // 黑板背景：图片拉伸铺满视口（固定于视口，不随内容平移/缩放，参考 BoardSlideControl）；
    // 空 pixmap 回退墨绿纯色
    void setBoardBackground(const QPixmap& pixmap);
    QPixmap boardBackground() const { return boardBg_; }

    // 黑板原色（无背景图时的回退色；设置面板"原色"项取色用）
    static QColor boardDefaultColor() { return QColor(21, 42, 32); }

    // 撤销 / 重做（数据层历史栈驱动，成功后自动全量刷新画布）
    void undo();
    void redo();

    // 页面管理（数据层多页面 + UI 全量重建）
    QStringList pageIds() const { return pageIds_; }
    QString currentPageId() const { return currentPageId_; }
    void createPage();
    bool deletePage(const QString& pageId);
    bool copyPage(const QString& pageId);  // 复制页面（内容一致、插入其后并选中新页）
    void selectPage(const QString& pageId);

    // 页面缩略图（页数面板用）：全部页数据 + 单页渲染为缩略图；
    // background 非空时以背景图铺底（与画布观感一致），否则墨绿纯色
    std::vector<whiteboard::Page> allPages();
    static QPixmap renderPageThumbnail(const whiteboard::Page& page, const QSize& size,
                                       const QPixmap& background = QPixmap());

    static constexpr int kMaxPages = 20;  // 页面数上限（参考 MaxWhiteboard _maxSlideCount）

    // 视图缩放
    void zoomIn();
    void zoomOut();
    void resetZoom();

    // 数据层引用（互动会话层注册命令产出回调/应用远程命令用）
    QtBoardData& data() { return data_; }

signals:
    void pagesChanged();
    void currentPageChanged(int index);
    void zoomChanged(qreal percent);  // 当前缩放比例（百分数，如 100.0）
    void toolChanged(Tool tool);      // 工具切换（外部工具栏同步选中态）

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // 数据层回调（在数据线程触发），投递回 UI 线程后执行
    void onElementsChanged(std::vector<std::string> removed,
                           std::vector<std::shared_ptr<whiteboard::Element>> added,
                           std::vector<whiteboard::EraserPlacement> placements);
    void onStrokeCommitted(uint64_t token, const std::string& id,
                           const std::string& parentId, int cellIndex);
    void onCleared();
    void onPageChanged();
    void onStrokePreview(std::string strokeId, uint32_t color, int width,
                         std::vector<whiteboard::Point> points);
    void onToolPreview(uint32_t tool, std::string sessionId,
                       std::vector<whiteboard::Point> points,
                       std::vector<std::string> elementIds);
    void onSynced();

    // 以下均在 UI 线程执行
    void applyElementChanges(const std::vector<std::string>& removed,
                             const std::vector<std::shared_ptr<whiteboard::Element>>& added,
                             const std::vector<whiteboard::EraserPlacement>& placements);
    void applyStrokeCommitted(uint64_t token, const std::string& id,
                              const std::string& parentId, int cellIndex);
    void applyCleared();
    void applyPageChanged();
    void applyStrokePreview(const std::string& strokeId, uint32_t color, int width,
                            const std::vector<whiteboard::Point>& points);
    void applyToolPreview(uint32_t tool, const std::string& sessionId,
                          const std::vector<whiteboard::Point>& points,
                          const std::vector<std::string>& elementIds);
    void applySynced();
    QGraphicsPathItem* buildStrokeItem(const whiteboard::Stroke& stroke, const std::string& id);
    QGraphicsPathItem* buildGraphicItem(const whiteboard::GraphicElement& g, const std::string& id);
    QGraphicsPathItem* buildTableItem(const whiteboard::TableElement& t, const std::string& id);
    void rebuildTable(const QString& tableId);  // 按数据层快照整表重建（布局重排/格内内容变更后）
    QGraphicsPathItem* buildMindMapItem(const whiteboard::MindMapElement& mind,
                                        const std::string& id);
    QGraphicsPathItem* buildTextItem(const whiteboard::TextElement& t, const std::string& id);
    QGraphicsPathItem* buildWidgetItem(const whiteboard::WidgetElement& w, const std::string& id);
    QGraphicsPathItem* buildElementItem(const whiteboard::Element& e);  // 按类型分派
    QGraphicsPathItem* createPreviewItem(const QPointF& start);
    void ensureEraserItem();
    void updateEraserPreview(const QPointF& center);

    // 选择与变换
    void selectItems(const QList<QGraphicsPathItem*>& items);
    void clearSelection();
    void syncSelectionFrame();
    void broadcastSelectionPreview();  // 广播选择框 4 顶点+选中 id（供远端预览）
    void beginSelectionDrag(SelectionFrame::Handle h, const QPointF& scenePos);
    void updateSelectionDrag(const QPointF& scenePos);
    void endSelectionDrag();
    void bakeTransformToData();       // 变换结束：新点集写回数据层并复位 UI 变换
    void updateHoverCursor(const QPointF& scenePos);
    QGraphicsPathItem* hitElementItem(const QPointF& scenePos) const;
    // 思维导图：钮命中（折叠/加/删节点）、节点聚焦、聚焦态 path 重建
    bool tryMindMapAction(const QPointF& scenePos);
    void updateMindFocusOnHit(QGraphicsPathItem* item, const QPointF& scenePos);
    void setMindFocus(const QString& mapId, const QString& nodeId);
    void refreshMindMapPath(const QString& mapId);
    // 小工具（秒表/计时器/计算器/算盘/骰子/大转盘/点名器）：运行状态为本地 UI 运行时
    //（不落数据）；卡片控件操作（按钮/键盘/珠/骰子设置/转盘旋转/点名滚动）
    struct WidgetRuntime {
        int kind = 0;           // 0=秒表 1=计时器 2=计算器 3=算盘 4=骰子 5=大转盘 6=点名器（同步自元素数据）
        int durationSec = 300;  // 计时器时长（秒；其他类型忽略）
        bool running = false;   // 走时中
        bool finished = false;  // 计时器归零（数字变橙）
        qint64 startMs = 0;     // 当前运行段起点（毫秒时间戳）
        qint64 accumMs = 0;     // 已累计毫秒（暂停时冻结）
        // 计时器设置态（卡片内时分秒滚轮）：未开始/重置后为 true（显示滚轮）；
        // 点开始提交时长（UpdateWidgetDuration）进入倒计时显示态
        bool setupMode = false;
        int wheelH = 0;         // 滚轮值：时 0~23（设置态）
        int wheelM = 5;         // 分 0~59
        int wheelS = 0;         // 秒 0~59
        // 骰子（kind=4）：参数同步自数据；结果/滚动态为本地运行时
        int diceSides = 6;      // 面数 4/6/8/12/20
        int diceCount = 2;      // 颗数 1~10
        int diceFaces[10] = {}; // 当前显示面（滚动中快速变化；结果态为最终值）
        int diceJitter[10] = {}; // 滚动视觉抖动角（度；结果态为 0）
        bool rolling = false;   // 滚动动画中（2 秒）
        qint64 rollStartMs = 0;
        qint64 lastRollShuffleMs = 0;  // 换面节流（约 60ms 一次）
        // 计算器（kind=2）：完整四则运算状态机（本地运行时）
        QString calcDisplay = QStringLiteral("0");
        double calcAcc = 0.0;   // 累加器
        int calcOp = 0;         // 待定运算符：0=无 1=+ 2=− 3=× 4=÷
        bool calcFresh = true;  // 下一个数字键开启新输入（结果/运算后为 true）
        bool calcError = false; // 除零错误（按键后恢复）
        // 算盘（kind=3）：9 档珠位（本地运行时）
        int abacusHigh[9] = {}; // 上珠 0/1（1 = 贴梁拨下/启用 5）
        int abacusLow[9] = {};  // 下珠拨起数 0~4（从梁数）
        // 大转盘（kind=5）：设置（选项/去重）同步自数据；旋转/结果/已抽为本地运行时
        QString wheelOptionsText;      // 原始选项文本（UTF-8；编辑弹窗载入/变更对比）
        QStringList wheelOptions;      // 选项显示名（已剥离权重后缀）
        QVector<double> wheelWeights;  // 每项权重（>= 1）
        QSet<QString> wheelDrawn;      // 已抽中项名集合（去重模式；扇区变暗/不再参与）
        bool wheelDedup = false;       // 去重模式开关（同步自数据）
        double wheelAngle = 0.0;       // 当前旋转角度（度）
        bool wheelSpinning = false;    // 旋转动画中（3 秒）
        qint64 wheelSpinStartMs = 0;
        double wheelSpinFrom = 0.0;    // 动画起始角度
        double wheelSpinTo = 0.0;      // 动画结束角度（已对齐目标扇区中心）
        int wheelResultIdx = -1;       // 抽中项索引（-1 = 无）
        QString wheelResult;           // 抽中项显示名（结果区展示）
        QString wheelHint;             // 无法启动时的灰字提示（成功启动/重置/编辑后清空）
        // 点名器（kind=6）：设置（名单/去重/人数）同步自数据；滚动/结果为本地运行时
        QString nameOptionsText;       // 原始名单文本（UTF-8；编辑弹窗载入/变更对比）
        QStringList nameOptions;       // 名单显示名（已剥离权重后缀）
        QVector<double> nameWeights;   // 每人权重（>= 1）
        QSet<QString> nameDrawn;       // 已点过的人（去重模式；不再被抽中）
        bool nameDedup = false;        // 去重模式开关（同步自数据）
        bool nameRolling = false;      // 滚动动画中（2 秒）
        qint64 nameRollStartMs = 0;
        qint64 lastNameShuffleMs = 0;  // 换名节流（约 50ms 一次）
        int nameRollCursor = 0;        // 滚动显示游标（显示 nameOptions[cursor % n]）
        QStringList nameResult;        // 本次结果（1~5 个名字）
        QString nameHint;              // 无法启动时的灰字提示（成功启动/重置/编辑后清空）
        int pickCount = 1;             // 一次抽取人数 1~5（同步自数据）
    };
    qint64 widgetElapsedMs(const WidgetRuntime& rt) const;
    void refreshWidgetItem(const QString& id);  // 重算显示文本刷新卡片（tick/操作后）
    void updateWidgetTick();                    // 33ms tick：刷新运行中实例（走时/滚骰/转盘/点名），全停自停
    void toggleWidgetRun(const QString& id);    // 开始/暂停（计时器设置态→提交时长；归零后点击从头开始）
    void resetWidget(const QString& id);        // 归零并停止（计时器回设置态；转盘/点名器=清结果+恢复全部）
    bool widgetControlPress(const QString& id, const QPointF& local);  // 卡片控件动作分派（按 kind）
    void calcPress(WidgetRuntime& rt, int key);  // 计算器按键状态机（键索引 0~18）
    void diceStartRoll(const QString& id);       // 骰子开始 2 秒滚动（确定/再掷共用）
    void wheelStartSpin(const QString& id);      // 转盘开始 3 秒旋转（加权选目标 → 缓出定格）
    void nameStartRoll(const QString& id);       // 点名器开始 2 秒滚动（定格抽 N 人）
    bool tryWidgetButtonAction(const QPointF& scenePos);  // 卡片控件命中：执行动作/重置等
    bool widgetWheelAdjust(const QPointF& scenePos, int delta);  // 设置态滚轮：调时分秒（消费/穿透）
    bool widgetToolHitExisting(const QPointF& scenePos);  // 小工具工具下点击已有卡片（控件操作/消费）
    // 选择展开：选中表格内任何元素时，把整表（含全部后代）都纳入选中集，保证跟随变换
    QString topTableOf(const QString& id) const;
    QList<QGraphicsPathItem*> expandToWholeTables(const QList<QGraphicsPathItem*>& items) const;
    void deleteSelection();           // 删除选中元素（表格级联删内部笔迹）

    // 文字内联编辑（QGraphicsProxyWidget 承载 QTextEdit；Enter 提交 / Shift+Enter 换行 /
    // Esc 取消 / 失焦提交）：existing 为空 = 在 scenePos 处新建；非空 = 编辑该文字图元
    void beginTextEditing(const QPointF& scenePos, QGraphicsPathItem* existing);
    void commitTextEditing();   // 提交：新建走 AddElement；已有走 UpdateTextContent（空文本等同取消）
    void cancelTextEditing();   // 取消：恢复原图元可见、移除编辑框
    void clearTextEditor();     // 仅移除编辑框资源（不提交不恢复，状态清理公共段）
    void updateTextEditFrame(); // 同步编辑框虚线框尺寸（内容变化/创建时调用）
    // 转盘/点名器选项编辑弹窗（QGraphicsProxyWidget 承载多行编辑器；Enter 换行 /
    // Ctrl+Enter 提交 / Esc 取消 / 失焦提交）：浮于卡片上方，提交走 UpdateWidgetOptions
    void beginWidgetOptionsEditing(const QString& id);  // 打开弹窗（载入当前选项文本）
    void commitWidgetOptionsEditing();  // 提交：非空且变更才写回（空等同取消）
    void cancelWidgetOptionsEditing();  // 取消：仅移除弹窗（不写回）
    void clearOptionsEditor();          // 仅移除弹窗资源（提交/取消公共段）
    // 新增元素工具（Shape/Table）拖动预览：开始/更新/提交/取消
    void updateToolDrawPreview(const QRectF& rect);
    void commitToolDraw(const QRectF& rect);
    void cancelToolPreview();
    void beginRubberBand(const QPointF& scenePos);
    void updateRubberBand(const QPointF& scenePos);
    void finishRubberBand();          // 框选两阶段判定：外接矩形粗筛 + 笔迹精判
    QRectF strokeSceneRect(const QGraphicsPathItem* item) const;
    // 精判：笔迹（笔宽描边后）与矩形/套索是否相交或完全包含
    QPainterPath strokedScenePath(const QGraphicsPathItem* item) const;
    bool pathHitsRect(const QGraphicsPathItem* item, const QRectF& rect) const;
    bool pathHitsLasso(const QGraphicsPathItem* item, const QPainterPath& lasso) const;

    // 套索选择（虚线笔迹）
    void beginLasso(const QPointF& scenePos);
    void updateLasso(const QPointF& scenePos);
    void finishLasso();

    // 视图适配：fit + 恢复用户缩放（1080p 等比显示）
    void fitView();
    // 计算能看全所有内容的最小缩放（无内容 50%，绝对下限 5%，上限 1.0）
    qreal computeContentFitScale() const;
    // 缩到最小时：视图居中显示全部内容（变换 = fitInView(内容)，不再叠加 userScale）
    void fitContentView();
    void beginPan(const QPoint& viewPos);
    void updatePan(const QPoint& viewPos);
    void endPan();

    // 页面
    void refreshPageIds();
    void reloadPage();

    whiteboard::Point toBoardPoint(const QPointF& p) const;
    whiteboard::Rect eraserRectAt(const QPointF& center) const;
    QPointF clampToScene(const QPointF& p) const;
    static QPen strokePen(uint32_t color, int width);
    static QPen rubberPen();

    QtBoardData data_;
    QGraphicsScene scene_;

    Tool tool_ = Tool::Pen;
    uint32_t penColor_ = 0x00FFFFFF;  // 默认白色
    int penWidth_ = 3;
    QPixmap boardBg_;                 // 黑板背景图（空 = 墨绿纯色）

    int sessionId_ = 0;
    uint64_t strokeToken_ = 0;

    // 绘制中的预览笔画（尚未落库，无 id）
    QGraphicsPathItem* previewItem_ = nullptr;
    // 已结束但仍未收到落库回调的笔画：token -> item
    QHash<uint64_t, QGraphicsPathItem*> pendingItems_;
    // 正式元素图元：id -> item（笔画/图形/表格网格统一存放；橡皮增量删除时按 id 命中）
    QHash<QString, QGraphicsPathItem*> elementItems_;
    // 表格归属映射：后代 id -> 直接父表格 id；表格 id -> 全部后代 id（含嵌套递归展开）
    QHash<QString, QString> cellOwner_;
    QHash<QString, QStringList> tableChildren_;
    // 表格布局缓存（值化结果：列宽/行高/总尺寸；渲染/命中/变换烘焙共用，
    // 与 buildTableItem 写入的网格 path 严格同源）
    QHash<QString, whiteboard::TableElement::Layout> tableLayouts_;
    // 思维导图布局缓存（值化结果，数据态坐标；钮绘制与命中判定用）
    QHash<QString, whiteboard::MindLayout> mindLayouts_;
    QString mindFocusMap_;   // 聚焦节点所属导图 id（空 = 无聚焦）
    QString mindFocusNode_;  // 聚焦节点 id
    // 小工具运行时（id -> 走时状态；本地 UI 状态不落数据）；tick 定时器仅在
    // 存在运行中实例时启动，全部停止后自停
    QHash<QString, WidgetRuntime> widgetRuntimes_;
    QTimer* widgetTick_ = nullptr;
    // 远程笔画实时预览：strokeId -> item（落库后移除并转为正式图元）
    QHash<QString, QGraphicsPathItem*> remotePreview_;

    // 新增元素工具状态（Shape/Table 拖动绘制；MindMap 点击即放置；Text 点击内联编辑）
    int shapeKind_ = 0;               // GraphicKind 整数值
    int tableRows_ = 3;
    int tableCols_ = 3;
    int textFontSize_ = 32;           // 文字工具字号（面板选择，像素）
    uint32_t textColor_ = 0x00FFFFFF; // 文字工具颜色（COLORREF 语义，默认白色）
    int widgetKind_ = 0;              // 小工具类型（0 秒表 / 1 计时器 / 2 计算器 / 3 算盘 / 4 骰子 / 5 大转盘 / 6 点名器）
    bool toolDrawing_ = false;
    QPointF toolDrawStart_;
    QGraphicsPathItem* toolDrawPreview_ = nullptr;

    // 文字内联编辑状态（同一时刻至多一个；QGraphicsProxyWidget 承载 QTextEdit）
    QTextEdit* textEditor_ = nullptr;                  // 编辑框 widget
    QGraphicsProxyWidget* textEditorProxy_ = nullptr;  // 编辑框场景代理（z=150）
    QString editingTextId_;                            // 编辑中的已有文字 id（空 = 新建中）
    QGraphicsPathItem* textEditHiddenItem_ = nullptr;  // 编辑期间被隐藏的原文字图元
    QPointF textEditAnchor_;                           // 新建锚点（场景/数据坐标 = 点击点）
    uint32_t textEditColor_ = 0x00FFFFFF;              // 新建文字颜色（当前笔色快照）
    bool textRouter_ = false;                          // 鼠标事件已路由给编辑框（拖选文字期间）
    QGraphicsPathItem* textEditFrame_ = nullptr;       // 编辑框橙色虚线框（场景图元，不依赖 widget 快照）

    // 转盘/点名器选项编辑弹窗状态（同一时刻至多一个；与文字编辑互斥使用）
    QTextEdit* optionsEditor_ = nullptr;                  // 弹窗编辑框 widget
    QGraphicsProxyWidget* optionsEditorProxy_ = nullptr;  // 弹窗场景代理（z=152）
    QString optionsEditId_;                               // 编辑中的卡片 id（空 = 未编辑）
    QGraphicsPathItem* optionsEditFrame_ = nullptr;       // 弹窗橙色虚线框（场景图元）
    QGraphicsPathItem* optionsEditHint_ = nullptr;        // 弹窗上方提示条（权重语法说明）
    bool optionsRouter_ = false;                          // 鼠标事件已路由给弹窗（拖选文本期间）

    // 本地橡皮预览框（跟随鼠标的虚线矩形）
    QGraphicsRectItem* eraserPreview_ = nullptr;
    // 远端橡皮擦遮罩：无边框背景色填充橡皮扫过区域的累积路径，视觉上只"擦除"碰撞部分
    // （整条图元保持可见），End 时移除遮罩并由数据层按 id 精确删/加
    QGraphicsPathItem* remoteEraserMask_ = nullptr;
    // 远端橡皮擦当前矩形虚线框（只描当前矩形，不描累积轮廓，避免"移动轨迹"残留）
    QGraphicsRectItem* remoteEraserPreview_ = nullptr;
    // 当前远端橡皮会话 id：切换时重置遮罩，防止上次 End 丢失导致遮罩残留
    QString remoteEraserSessionId_;
    // 远端橡皮上一帧矩形：仅与相邻帧取并集防缝，避免用累积路径 boundingRect 导致遮罩膨胀
    QRectF remoteEraserLastRect_;
    // 远端套索预览（浅蓝虚线路径）
    QGraphicsPathItem* remoteLassoPreview_ = nullptr;
    // 远端选择框预览（虚线矩形，按顶点定位/旋转）
    QGraphicsRectItem* remoteSelectionPreview_ = nullptr;

    // 选择与变换状态
    SelectionFrame* selectionFrame_ = nullptr;
    QList<QGraphicsPathItem*> selectedItems_;
    enum class DragOp {
        None, Move,
        ScaleTL, ScaleTR, ScaleBL, ScaleBR,
        ScaleL, ScaleR, ScaleT, ScaleB,
        Rotate, RubberBand, Lasso
    };
    DragOp dragOp_ = DragOp::None;
    QPointF dragStartScene_;
    QList<QTransform> dragStartTransforms_;  // 与 selectedItems_ 一一对应
    QPointF anchorScene_;        // 缩放锚点（被拖手柄的对侧）
    QRectF dragStartUnionRect_;  // 多选缩放起始并集矩形（场景坐标）
    QPointF rotateCenterScene_;  // 旋转中心
    qreal rotateStartAngle_ = 0;

    // 框选橡皮筋（浅蓝虚线）
    QGraphicsRectItem* rubberBand_ = nullptr;
    QPointF rubberStartScene_;

    // 套索选择（浅蓝虚线笔迹）
    QGraphicsPathItem* lassoPreview_ = nullptr;
    QVector<QPointF> lassoPoints_;
    QPointF lassoLastSample_;

    // 中键平移
    bool panning_ = false;
    QPoint lastPanPos_;

    // 用户缩放因子（resize 后保持），1.0 = 1080p 适配窗口
    qreal userScale_ = 1.0;
    // 缩到最小（fit 全部内容）状态：视图变换 = fitInView(内容)，userScale_ 仅记录名义比例
    bool fitContentMode_ = false;
    QRectF fitContentRect_;  // fitContent 时的内容包围盒（resize 时重新 fit）

    // 页面 id 镜像（UI 线程）
    QStringList pageIds_;
    QString currentPageId_;

    int eraserSize_ = kEraserSize;   // 橡皮擦高度（横向长方形短边，与数据层擦除矩形一致；可调）
    static constexpr int kEraserSize = 40;  // 橡皮擦默认高度
    static constexpr qreal kEraserAspect = 1.5;  // 橡皮擦宽高比（横向长方形）
    static constexpr qreal kHitTolerance = 12;  // 选择工具命中容差（像素）
    static constexpr qreal kViewWidth = 1920;   // 初始视图基准 1080p
    static constexpr qreal kViewHeight = 1080;
    static constexpr qreal kSceneHalfExtent = 100000;  // 近似无限场景半径
    static constexpr qreal kMinScale = 0.5;    // 无内容时的最小缩放 50%
    static constexpr qreal kMinFitScale = 0.05;  // 内容极多时的绝对下限 5%
    static constexpr qreal kMaxScale = 3.0;    // 最大缩放 300%
    static constexpr qreal kScaleStep = 0.1;   // 缩放步进 10%
    static constexpr qreal kLassoSampleDist = 3;  // 套索采样最小间距（场景坐标）
};
