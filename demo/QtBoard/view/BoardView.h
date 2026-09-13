#pragma once
#include <QColor>
#include <QGraphicsView>
#include <QHash>
#include <QList>
#include <QPixmap>
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
class QGraphicsRectItem;
class QMouseEvent;
class QPainter;
class QResizeEvent;
class QWheelEvent;
class QEvent;
class QPainterPath;

// 黑板画布视图：捕获鼠标事件驱动白板数据层，并把 Page 数据渲染为图元。
// 线程模型：所有 scene/item 操作仅在 UI 线程；数据层回调经 QueuedConnection 投递回 UI 线程。
// 无限画布：场景为超大矩形（近似无限），初始按 1080p（1920x1080）等比适配显示，
// 支持抓手/中键平移查看屏幕外元素；缩放限制 50%~300%，步进 0.5。
class BoardView : public QGraphicsView {
    Q_OBJECT
public:
    enum class Tool { Pen, Select, Lasso, Eraser, Pan };

    explicit BoardView(QWidget* parent = nullptr);
    ~BoardView() override;

    void setTool(Tool tool);
    Tool currentTool() const { return tool_; }
    void setPenColor(uint32_t color);   // COLORREF 语义 (0x00BBGGRR)
    void setPenWidth(int width);
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
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // 数据层回调（在数据线程触发），投递回 UI 线程后执行
    void onElementsChanged(std::vector<std::string> removed,
                           std::vector<std::shared_ptr<whiteboard::Element>> added);
    void onStrokeCommitted(uint64_t token, const std::string& id);
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
                             const std::vector<std::shared_ptr<whiteboard::Element>>& added);
    void applyStrokeCommitted(uint64_t token, const std::string& id);
    void applyCleared();
    void applyPageChanged();
    void applyStrokePreview(const std::string& strokeId, uint32_t color, int width,
                            const std::vector<whiteboard::Point>& points);
    void applyToolPreview(uint32_t tool, const std::string& sessionId,
                          const std::vector<whiteboard::Point>& points,
                          const std::vector<std::string>& elementIds);
    void applySynced();
    QGraphicsPathItem* buildStrokeItem(const whiteboard::Stroke& stroke, const std::string& id);
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
    QGraphicsPathItem* hitStrokeItem(const QPointF& scenePos) const;
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
    // 正式笔画：id -> item（橡皮擦增量删除时按 id 命中）
    QHash<QString, QGraphicsPathItem*> strokeItems_;
    // 远程笔画实时预览：strokeId -> item（落库后移除并转为正式图元）
    QHash<QString, QGraphicsPathItem*> remotePreview_;

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

    static constexpr int kEraserSize = 40;  // 橡皮擦边长（白色方块，与数据层擦除矩形一致）
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
