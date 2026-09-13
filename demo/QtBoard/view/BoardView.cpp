#include "BoardView.h"

#include <QEvent>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <QtGlobal>
#include <QtMath>

#include <cmath>

#include "BoardUtil.h"

namespace {
// 黑板墨绿背景（单一来源：BoardView::boardDefaultColor）
const QColor kBoardBackground = BoardView::boardDefaultColor();
}  // namespace

BoardView::BoardView(QWidget* parent) : QGraphicsView(parent) {
    setScene(&scene_);
    // 无限画布：超大矩形场景（近似无限），平移/绘制不受边界限制
    scene_.setSceneRect(QRectF(-kSceneHalfExtent, -kSceneHalfExtent,
                               kSceneHalfExtent * 2, kSceneHalfExtent * 2));
    scene_.setBackgroundBrush(kBoardBackground);

    setRenderHint(QPainter::Antialiasing, true);
    setMouseTracking(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // 数据层回调：在数据线程触发，投递回 UI 线程（this 销毁后 Qt 自动丢弃）
    data_.SetCallbacks(
        [this](std::vector<std::string> removed,
               std::vector<std::shared_ptr<whiteboard::Element>> added) {
            onElementsChanged(std::move(removed), std::move(added));
        },
        [this](uint64_t token, const std::string& id) {
            onStrokeCommitted(token, id);
        },
        [this]() {
            onCleared();
        },
        [this]() {
            onPageChanged();
        },
        [this](std::string strokeId, uint32_t color, int width,
               std::vector<whiteboard::Point> points) {
            onStrokePreview(std::move(strokeId), color, width, std::move(points));
        },
        [this](uint32_t tool, std::string sessionId,
               std::vector<whiteboard::Point> points,
               std::vector<std::string> elementIds) {
            onToolPreview(tool, std::move(sessionId), std::move(points),
                          std::move(elementIds));
        },
        [this]() {
            onSynced();
        });

    refreshPageIds();
    fitView();
}

BoardView::~BoardView() {
    // 先移除回调并等待数据线程在途任务完成，避免回调触碰已析构对象
    data_.RemoveCallbacks();
}

// 黑板背景图：拉伸铺满视口（参考 BoardSlideControl 的 ImageBrush 背景）；空 = 墨绿纯色
void BoardView::setBoardBackground(const QPixmap& pixmap) {
    boardBg_ = pixmap;
    viewport()->update();
}

// 背景固定于视口：重置变换后按视口矩形拉伸绘制，不随内容平移/缩放
void BoardView::drawBackground(QPainter* painter, const QRectF& rect) {
    if (boardBg_.isNull()) {
        painter->fillRect(rect, kBoardBackground);
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->resetTransform();
    painter->drawPixmap(viewport()->rect(), boardBg_);
    painter->restore();
}

void BoardView::setTool(Tool tool) {
    tool_ = tool;
    if (tool_ == Tool::Eraser) {
        ensureEraserItem();
        setCursor(Qt::BlankCursor);  // 橡皮预览框即光标
    } else {
        if (eraserPreview_)
            eraserPreview_->setVisible(false);
        switch (tool_) {
            case Tool::Select:
            case Tool::Lasso:
                setCursor(Qt::ArrowCursor);
                break;
            case Tool::Pan:
                setCursor(Qt::OpenHandCursor);
                break;
            default:
                setCursor(Qt::CrossCursor);
                break;
        }
    }
    // 抓手工具交给 QGraphicsView 的 ScrollHandDrag 处理左键
    if (tool_ == Tool::Pan)
        setDragMode(QGraphicsView::ScrollHandDrag);
    else
        setDragMode(QGraphicsView::NoDrag);
    if (tool_ != Tool::Select && tool_ != Tool::Lasso)
        clearSelection();
    emit toolChanged(tool_);
}

void BoardView::undo() {
    data_.Undo();  // 成功时 onPageChanged 回调驱动全量刷新
}

void BoardView::redo() {
    data_.Redo();
}

void BoardView::setPenColor(uint32_t color) {
    penColor_ = color;
    data_.SetColor(color);
}

void BoardView::setPenWidth(int width) {
    penWidth_ = width;
    data_.SetPenWidth(width);
}

void BoardView::clearBoard() {
    data_.Clear();
}

// ---------- 页面管理 ----------

void BoardView::refreshPageIds() {
    const std::vector<std::string> ids = data_.GetPageIds();
    pageIds_.clear();
    for (const std::string& s : ids)
        pageIds_.append(QString::fromStdString(s));
    currentPageId_ = QString::fromStdString(data_.GetCurrentPageId());
}

void BoardView::createPage() {
    if (pageIds_.size() >= kMaxPages)
        return;  // 页数上限：最多 20 页（参考 MaxWhiteboard）
    data_.CreatePage();
    refreshPageIds();
    reloadPage();
    emit pagesChanged();
    emit currentPageChanged(pageIds_.indexOf(currentPageId_));
}

bool BoardView::deletePage(const QString& pageId) {
    if (!data_.DeletePage(pageId.toStdString()))
        return false;
    refreshPageIds();
    reloadPage();
    emit pagesChanged();
    emit currentPageChanged(pageIds_.indexOf(currentPageId_));
    return true;
}

void BoardView::selectPage(const QString& pageId) {
    if (pageId == currentPageId_)
        return;
    data_.SelectPage(pageId.toStdString());
    currentPageId_ = pageId;
    reloadPage();
    emit currentPageChanged(pageIds_.indexOf(currentPageId_));
}

// 全量重建当前页（切换/新建/删除页面、撤销/重做后调用）
void BoardView::reloadPage() {
    clearSelection();
    scene_.clear();  // 删除所有图元
    strokeItems_.clear();
    pendingItems_.clear();
    remotePreview_.clear();
    previewItem_ = nullptr;
    eraserPreview_ = nullptr;  // 懒重建
    rubberBand_ = nullptr;     // 已被 scene_.clear() 删除，防悬空
    lassoPreview_ = nullptr;   // 已被 scene_.clear() 删除，防悬空
    remoteEraserPreview_ = nullptr;    // 已被 scene_.clear() 删除，防悬空
    remoteEraserMask_ = nullptr;       // 已被 scene_.clear() 删除，防悬空
    remoteLassoPreview_ = nullptr;     // 已被 scene_.clear() 删除，防悬空
    remoteSelectionPreview_ = nullptr; // 已被 scene_.clear() 删除，防悬空
    remoteEraserSessionId_.clear();
    remoteEraserLastRect_ = QRectF();

    whiteboard::Page page;
    data_.GetPage(page);
    for (auto& e : page.elements) {
        auto* stroke = dynamic_cast<const whiteboard::Stroke*>(e.get());
        if (stroke)
            buildStrokeItem(*stroke, stroke->id);
    }
    if (tool_ == Tool::Eraser)
        ensureEraserItem();
}

// 全量导出所有页面（页数面板缩略图用；数据线程深拷贝，同步返回）
std::vector<whiteboard::Page> BoardView::allPages() {
    std::vector<whiteboard::Page> pages;
    data_.GetAllPages(pages);
    return pages;
}

// 单页渲染为缩略图（复刻参考 DisplaySlideManagerView 的 Viewbox 算法）：
// 1. 视口 = 元素并集包围盒；空页或被画板区(1920x1080 外扩 5)包含时取画板区；
// 2. 否则按中轴补齐到画板尺寸，再四周外扩 1/64；
// 3. 以 Stretch=Fill 语义（横纵独立缩放）填充目标尺寸，背景为黑板色。
QPixmap BoardView::renderPageThumbnail(const whiteboard::Page& page, const QSize& size,
                                       const QPixmap& background) {
    QPixmap pixmap(size);
    if (background.isNull()) {
        pixmap.fill(kBoardBackground);
    } else {
        // 背景图铺底：拉伸铺满缩略图（与画布观感一致）
        pixmap.fill(Qt::transparent);
        QPainter bgPainter(&pixmap);
        bgPainter.setRenderHint(QPainter::SmoothPixmapTransform);
        bgPainter.drawPixmap(QRect(QPoint(0, 0), size), background);
    }

    // 画板基准区：1080p 画板外扩 5
    const QRectF slideBounds(-5.0, -5.0, kViewWidth + 10.0, kViewHeight + 10.0);

    // 元素并集包围盒（无元素为空）
    QRectF least;
    bool hasContent = false;
    for (const auto& e : page.elements) {
        auto* stroke = dynamic_cast<const whiteboard::Stroke*>(e.get());
        if (!stroke)
            continue;
        const whiteboard::Rect br = stroke->bounding.ToRect();
        const QRectF r(br.x, br.y, br.width, br.height);
        least = hasContent ? least.united(r) : r;
        hasContent = true;
    }

    if (!hasContent || slideBounds.contains(least)) {
        least = slideBounds;
    } else {
        // 中轴补齐到画板尺寸（宽度/高度不足时向两侧各扩差值一半）
        if (least.width() < kViewWidth) {
            const qreal dw = (kViewWidth - least.width()) / 2.0;
            least.adjust(-dw, 0, dw, 0);
        }
        if (least.height() < kViewHeight) {
            const qreal dh = (kViewHeight - least.height()) / 2.0;
            least.adjust(0, -dh, 0, dh);
        }
        // 四周外扩 1/64（参考 Inflate(Width/64, Height/64)，两侧合计 1/32）
        least.adjust(-least.width() / 64.0, -least.height() / 64.0,
                     least.width() / 64.0, least.height() / 64.0);
    }

    if (least.width() <= 0 || least.height() <= 0)
        return pixmap;

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(-least.left(), -least.top());
    p.scale(size.width() / least.width(), size.height() / least.height());
    for (const auto& e : page.elements) {
        auto* stroke = dynamic_cast<const whiteboard::Stroke*>(e.get());
        if (!stroke || stroke->points.empty())
            continue;
        QPainterPath path;
        for (size_t i = 0; i < stroke->points.size(); ++i) {
            const whiteboard::Point& pt = stroke->points[i];
            if (i == 0)
                path.moveTo(pt.x, pt.y);
            else
                path.lineTo(pt.x, pt.y);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(strokePen(stroke->color, stroke->width));
        p.drawPath(path);
    }
    return pixmap;
}

// ---------- 视图缩放（1080p 等比适配 + 用户缩放因子，最小级别适配全部内容） ----------

// 1080p 基准区域等比适配当前窗口，再叠加用户缩放因子（上限 300%）。
void BoardView::fitView() {
    userScale_ = qBound(kMinFitScale, userScale_, kMaxScale);
    fitContentMode_ = false;
    resetTransform();
    fitInView(QRectF(0, 0, kViewWidth, kViewHeight), Qt::KeepAspectRatio);
    scale(userScale_, userScale_);
    emit zoomChanged(qRound(userScale_ * 100.0));
}

// 计算"看全所有内容"所需的最小缩放（基于 1080p 基准视图换算）：
// 无内容返回 50%；否则按内容包围盒尺寸求值，clamp 到 [5%, 100%]。
qreal BoardView::computeContentFitScale() const {
    QRectF content;
    bool hasContent = false;
    for (auto it = strokeItems_.constBegin(); it != strokeItems_.constEnd(); ++it) {
        if (!*it)
            continue;
        content = hasContent ? content.united((*it)->sceneBoundingRect())
                             : (*it)->sceneBoundingRect();
        hasContent = true;
    }
    for (auto it = remotePreview_.constBegin(); it != remotePreview_.constEnd(); ++it) {
        if (!*it)
            continue;
        content = hasContent ? content.united((*it)->sceneBoundingRect())
                             : (*it)->sceneBoundingRect();
        hasContent = true;
    }
    if (!hasContent)
        return kMinScale;

    const QSize vp = viewport()->size();
    if (vp.width() <= 0 || vp.height() <= 0)
        return kMinScale;

    // 1080p 基准视图的缩放（KeepAspectRatio）
    const qreal baseScale = qMin(vp.width() / kViewWidth, vp.height() / kViewHeight);
    if (baseScale <= 0 || content.width() <= 0 || content.height() <= 0)
        return kMinScale;

    const qreal fit = qMin(vp.width() / (content.width() * baseScale),
                           vp.height() / (content.height() * baseScale));
    return qBound(kMinFitScale, fit, 1.0);
}

// 缩到最小：视图居中显示全部内容（变换 = fitInView(内容)）。
void BoardView::fitContentView() {
    fitContentMode_ = true;
    fitContentRect_ = QRectF();
    for (auto it = strokeItems_.constBegin(); it != strokeItems_.constEnd(); ++it) {
        if (!*it)
            continue;
        fitContentRect_ = fitContentRect_.isNull() ? (*it)->sceneBoundingRect()
                                                   : fitContentRect_.united((*it)->sceneBoundingRect());
    }
    for (auto it = remotePreview_.constBegin(); it != remotePreview_.constEnd(); ++it) {
        if (!*it)
            continue;
        fitContentRect_ = fitContentRect_.isNull() ? (*it)->sceneBoundingRect()
                                                   : fitContentRect_.united((*it)->sceneBoundingRect());
    }
    const bool empty = fitContentRect_.isNull();
    if (empty)
        fitContentRect_ = QRectF(0, 0, kViewWidth, kViewHeight);

    resetTransform();
    fitInView(fitContentRect_, Qt::KeepAspectRatio);
    if (empty)
        scale(userScale_, userScale_);  // 无内容：视觉与名义比例一致（避免 60%→100% 跳变）
    emit zoomChanged(qRound(userScale_ * 100.0));
}

// 滚轮缩放：每次 ±10%（步进 0.1），保持鼠标位置为锚点；
// 缩到最小级别时视图自动居中显示全部白板。
void BoardView::wheelEvent(QWheelEvent* event) {
    const bool up = event->angleDelta().y() > 0;
    qreal target = userScale_ + (up ? kScaleStep : -kScaleStep);
    target = qBound(kMinFitScale, target, kMaxScale);

    if (up) {
        if (qFuzzyCompare(target, userScale_)) {
            event->accept();
            return;
        }
        const qreal factor = target / userScale_;
        userScale_ = target;
        if (fitContentMode_) {
            // 从最小级别放大：在当前 fit 变换上按鼠标锚点继续放大
            setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            scale(factor, factor);
            setTransformationAnchor(QGraphicsView::AnchorViewCenter);
            fitContentMode_ = false;
        } else {
            setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            scale(factor, factor);
            setTransformationAnchor(QGraphicsView::AnchorViewCenter);
        }
        emit zoomChanged(qRound(userScale_ * 100.0));
        event->accept();
        return;
    }

    // 缩小：先检查是否会到达最小级别。
    // 内容超出视口时最小级别 = fitScale（看全全部内容）；
    // 内容较少（fitScale 已达 100% 上限，内容本就全部可见）时最小级别 = 50%，继续按步进缩小。
    const qreal fitScale = computeContentFitScale();
    const qreal minScale = (fitScale < 1.0) ? fitScale : kMinScale;
    if (target <= minScale + 1e-6) {
        if (fitContentMode_ && qFuzzyCompare(userScale_, minScale)) {
            event->accept();
            return;  // 已是最小级别
        }
        if (fitScale < 1.0) {
            userScale_ = fitScale;
            fitContentView();  // 内容超出视口：缩到最小并居中显示全部白板
        } else {
            const qreal clamped = qMax(kMinScale, target);
            const qreal factor = clamped / userScale_;
            userScale_ = clamped;
            setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            scale(factor, factor);
            setTransformationAnchor(QGraphicsView::AnchorViewCenter);
            fitContentMode_ = false;
            emit zoomChanged(qRound(userScale_ * 100.0));
        }
    } else {
        const qreal factor = target / userScale_;
        userScale_ = target;
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        scale(factor, factor);
        setTransformationAnchor(QGraphicsView::AnchorViewCenter);
        fitContentMode_ = false;
        emit zoomChanged(qRound(userScale_ * 100.0));
    }
    event->accept();
}

void BoardView::zoomIn() {
    if (fitContentMode_)
        userScale_ = computeContentFitScale();  // 从最小级别恢复名义值
    userScale_ = qBound(kMinFitScale, userScale_ + kScaleStep, kMaxScale);
    fitView();
}

void BoardView::zoomOut() {
    if (fitContentMode_)
        userScale_ = computeContentFitScale();
    const qreal fitScale = computeContentFitScale();
    const qreal minScale = (fitScale < 1.0) ? fitScale : kMinScale;
    if (userScale_ - kScaleStep <= minScale + 1e-6) {
        if (fitScale < 1.0) {
            userScale_ = fitScale;
            fitContentView();  // 内容超出视口：缩到最小显示全部白板
        } else {
            userScale_ = kMinScale;  // 内容较少（本就全部可见）：最小 50%
            fitView();
        }
        return;
    }
    userScale_ = userScale_ - kScaleStep;
    fitView();
}

void BoardView::resetZoom() {
    userScale_ = 1.0;
    fitView();
}

// ---------- 绘制基础设施 ----------

void BoardView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    // 等比适配新窗口尺寸：最小级别（看全所有内容）状态保持，否则保持用户缩放因子
    if (fitContentMode_)
        fitContentView();
    else
        fitView();
}

// ---------- 中键平移（任意工具下可用；抓手工具左键走 ScrollHandDrag） ----------

void BoardView::beginPan(const QPoint& viewPos) {
    panning_ = true;
    lastPanPos_ = viewPos;
    setCursor(Qt::ClosedHandCursor);
}

void BoardView::updatePan(const QPoint& viewPos) {
    if (!panning_)
        return;
    const QPoint d = viewPos - lastPanPos_;
    lastPanPos_ = viewPos;
    // 平移视图内容：滚动条始终隐藏，但 value 依然有效
    QScrollBar* h = horizontalScrollBar();
    QScrollBar* v = verticalScrollBar();
    h->setValue(h->value() - d.x());
    v->setValue(v->value() - d.y());
}

void BoardView::endPan() {
    if (!panning_)
        return;
    panning_ = false;
    setCursor(tool_ == Tool::Pan ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

QPen BoardView::strokePen(uint32_t color, int width) {
    return QPen(colorToQColor(color), qMax(1, width),
                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

QGraphicsPathItem* BoardView::createPreviewItem(const QPointF& start) {
    QPainterPath path;
    path.moveTo(start);
    auto* item = new QGraphicsPathItem();
    item->setPath(path);
    item->setPen(strokePen(penColor_, penWidth_));
    item->setBrush(Qt::NoBrush);
    scene_.addItem(item);
    return item;
}

QGraphicsPathItem* BoardView::buildStrokeItem(const whiteboard::Stroke& stroke,
                                              const std::string& id) {
    QPainterPath path;
    for (size_t i = 0; i < stroke.points.size(); ++i) {
        const whiteboard::Point& p = stroke.points[i];
        if (i == 0)
            path.moveTo(p.x, p.y);
        else
            path.lineTo(p.x, p.y);
    }
    auto* item = new QGraphicsPathItem();
    item->setPath(path);
    item->setPen(strokePen(stroke.color, stroke.width));
    item->setBrush(Qt::NoBrush);
    item->setData(0, QString::fromStdString(id));
    // 外接矩形直接复用数据层已算好的 stroke.bounding，UI 不再从 path 重算
    const whiteboard::Rect br = stroke.bounding.ToRect();
    item->setData(1, QRectF(br.x, br.y, br.width, br.height));
    scene_.addItem(item);
    strokeItems_.insert(QString::fromStdString(id), item);
    return item;
}

void BoardView::ensureEraserItem() {
    if (eraserPreview_)
        return;
    eraserPreview_ = new QGraphicsRectItem();
    eraserPreview_->setPen(Qt::NoPen);              // 白色方块：无边框
    eraserPreview_->setBrush(QColor(255, 255, 255, 240));
    eraserPreview_->setZValue(100);
    eraserPreview_->setVisible(false);
    scene_.addItem(eraserPreview_);
}

void BoardView::updateEraserPreview(const QPointF& center) {
    ensureEraserItem();
    eraserPreview_->setRect(QRectF(center.x() - kEraserSize / 2.0,
                                   center.y() - kEraserSize / 2.0,
                                   kEraserSize, kEraserSize));
    eraserPreview_->setVisible(true);
}

whiteboard::Point BoardView::toBoardPoint(const QPointF& p) const {
    return whiteboard::Point(qRound(p.x()), qRound(p.y()));
}

whiteboard::Rect BoardView::eraserRectAt(const QPointF& center) const {
    const int x = qRound(center.x() - kEraserSize / 2.0);
    const int y = qRound(center.y() - kEraserSize / 2.0);
    return whiteboard::Rect(x, y, kEraserSize, kEraserSize);
}

QPointF BoardView::clampToScene(const QPointF& p) const {
    const QRectF r = scene_.sceneRect();
    QPointF c = p;
    c.setX(qBound(r.left(), c.x(), r.right()));
    c.setY(qBound(r.top(), c.y(), r.bottom()));
    return c;
}

// ---------- 选择与变换 ----------

void BoardView::clearSelection() {
    if (selectionFrame_) {
        scene_.removeItem(selectionFrame_);
        delete selectionFrame_;
        selectionFrame_ = nullptr;
        // 广播空选择框：远端清除选择预览
        data_.SendToolPreview(3, std::to_string(sessionId_), {}, {});
    }
    selectedItems_.clear();
    dragOp_ = DragOp::None;
}

void BoardView::selectItems(const QList<QGraphicsPathItem*>& items) {
    // 相同集合且已有选择框则直接返回（拖动已选集合时保持选择框）
    if (selectionFrame_ && items == selectedItems_)
        return;
    clearSelection();
    selectedItems_ = items;
    selectedItems_.removeAll(nullptr);
    if (selectedItems_.isEmpty())
        return;
    selectionFrame_ = new SelectionFrame(selectedItems_);
    scene_.addItem(selectionFrame_);
    broadcastSelectionPreview();
}

void BoardView::syncSelectionFrame() {
    if (selectionFrame_)
        selectionFrame_->sync();
}

// 广播当前选择框（4 顶点 + 选中笔画 id）供远端实时预览；无选择时广播空 points。
void BoardView::broadcastSelectionPreview() {
    std::vector<whiteboard::Point> pts;
    std::vector<std::string> ids;
    if (selectionFrame_) {
        const QRectF r = selectionFrame_->rect();
        pts = {
            whiteboard::Point(qRound(r.left()), qRound(r.top())),
            whiteboard::Point(qRound(r.right()), qRound(r.top())),
            whiteboard::Point(qRound(r.right()), qRound(r.bottom())),
            whiteboard::Point(qRound(r.left()), qRound(r.bottom())),
        };
        ids.reserve(static_cast<size_t>(selectedItems_.size()));
        for (QGraphicsPathItem* it : selectedItems_) {
            const QVariant v = it ? it->data(0) : QVariant();
            if (v.isValid())
                ids.push_back(v.toString().toStdString());
        }
    }
    data_.SendToolPreview(3, std::to_string(sessionId_), pts, ids);
}

QGraphicsPathItem* BoardView::hitStrokeItem(const QPointF& scenePos) const {
    // 先按包围盒粗筛，再用笔宽描边后的形状精确判断：
    // QGraphicsPathItem::shape() 默认是裸 path（零宽度），精确点击才命中，
    // 斜线等任意角度笔画几乎点不中，故用 stroker 加宽做容差。
    const qreal r = kHitTolerance;
    const QList<QGraphicsItem*> items = scene_.items(
        QRectF(scenePos.x() - r, scenePos.y() - r, r * 2, r * 2),
        Qt::IntersectsItemBoundingRect);
    for (QGraphicsItem* it : items) {
        auto* pi = dynamic_cast<QGraphicsPathItem*>(it);
        if (!pi || !pi->data(0).isValid())
            continue;
        QPainterPathStroker stroker;
        stroker.setWidth(qMax(10.0, pi->pen().widthF() + 8));
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        if (stroker.createStroke(pi->path()).contains(pi->mapFromScene(scenePos)))
            return pi;
    }
    return nullptr;
}

void BoardView::beginSelectionDrag(SelectionFrame::Handle h,
                                   const QPointF& scenePos) {
    if (selectedItems_.isEmpty())
        return;
    dragStartScene_ = scenePos;
    dragStartTransforms_.clear();
    for (QGraphicsPathItem* it : selectedItems_)
        dragStartTransforms_.append(it->transform());
    dragOp_ = DragOp::None;

    const QRectF lr = SelectionFrame::itemRect(selectedItems_.first());
    QPointF anchorLocal;
    switch (h) {
        case SelectionFrame::TL:
            dragOp_ = DragOp::ScaleTL;
            anchorLocal = lr.bottomRight();
            break;
        case SelectionFrame::TR:
            dragOp_ = DragOp::ScaleTR;
            anchorLocal = lr.bottomLeft();
            break;
        case SelectionFrame::BL:
            dragOp_ = DragOp::ScaleBL;
            anchorLocal = lr.topRight();
            break;
        case SelectionFrame::BR:
            dragOp_ = DragOp::ScaleBR;
            anchorLocal = lr.topLeft();
            break;
        case SelectionFrame::L:
            dragOp_ = DragOp::ScaleL;
            anchorLocal = QPointF(lr.right(), lr.center().y());
            break;
        case SelectionFrame::R:
            dragOp_ = DragOp::ScaleR;
            anchorLocal = QPointF(lr.left(), lr.center().y());
            break;
        case SelectionFrame::T:
            dragOp_ = DragOp::ScaleT;
            anchorLocal = QPointF(lr.center().x(), lr.bottom());
            break;
        case SelectionFrame::B:
            dragOp_ = DragOp::ScaleB;
            anchorLocal = QPointF(lr.center().x(), lr.top());
            break;
        case SelectionFrame::Rotate:
            dragOp_ = DragOp::Rotate;
            rotateCenterScene_ = selectionFrame_->center();
            rotateStartAngle_ = std::atan2(scenePos.y() - rotateCenterScene_.y(),
                                           scenePos.x() - rotateCenterScene_.x());
            break;
        case SelectionFrame::None:
        case SelectionFrame::HandleCount:
            dragOp_ = DragOp::Move;  // 框内/图元上按下 = 移动整个选中集合
            break;
    }
    if (dragOp_ == DragOp::None || dragOp_ == DragOp::Move || dragOp_ == DragOp::Rotate)
        return;
    if (selectedItems_.size() == 1) {
        // 单选：锚点 = 局部角点经当前变换映射到场景（旋转后为真实角点位置，非 AABB 角）
        anchorScene_ = dragStartTransforms_.first().map(anchorLocal);
    } else {
        // 多选：锚点 = 集合并集 AABB 的对应角/边中点（场景坐标）
        const QRectF& ur = selectionFrame_->rect();
        switch (dragOp_) {
            case DragOp::ScaleTL: anchorScene_ = ur.bottomRight(); break;
            case DragOp::ScaleTR: anchorScene_ = ur.bottomLeft(); break;
            case DragOp::ScaleBL: anchorScene_ = ur.topRight(); break;
            case DragOp::ScaleBR: anchorScene_ = ur.topLeft(); break;
            case DragOp::ScaleL: anchorScene_ = QPointF(ur.right(), ur.center().y()); break;
            case DragOp::ScaleR: anchorScene_ = QPointF(ur.left(), ur.center().y()); break;
            case DragOp::ScaleT: anchorScene_ = QPointF(ur.center().x(), ur.bottom()); break;
            case DragOp::ScaleB: anchorScene_ = QPointF(ur.center().x(), ur.top()); break;
            default: break;
        }
        dragStartUnionRect_ = ur;
    }
}

void BoardView::updateSelectionDrag(const QPointF& scenePos) {
    if (dragOp_ == DragOp::None || selectedItems_.isEmpty())
        return;

    const QPointF delta = scenePos - dragStartScene_;
    QTransform m;

    if (dragOp_ == DragOp::Move) {
        m = QTransform::fromTranslate(delta.x(), delta.y());
    } else if (dragOp_ == DragOp::Rotate) {
        const qreal cur = std::atan2(scenePos.y() - rotateCenterScene_.y(),
                                     scenePos.x() - rotateCenterScene_.x());
        const qreal angle = qRadiansToDegrees(cur - rotateStartAngle_);
        m = QTransform()
                .translate(rotateCenterScene_.x(), rotateCenterScene_.y())
                .rotate(angle)
                .translate(-rotateCenterScene_.x(), -rotateCenterScene_.y());
    } else {
        // 缩放：单选走局部精确投影，多选基于并集 AABB 场景坐标
        qreal sx = 1.0;
        qreal sy = 1.0;
        if (selectedItems_.size() == 1) {
            const QTransform& t = dragStartTransforms_.first();
            const QPointF localDelta = t.inverted().map(delta);
            const QRectF lr = SelectionFrame::itemRect(selectedItems_.first());
            const qreal w = lr.width();
            const qreal h = lr.height();
            switch (dragOp_) {
                case DragOp::ScaleTL: sx = (w - localDelta.x()) / w; sy = (h - localDelta.y()) / h; break;
                case DragOp::ScaleTR: sx = (w + localDelta.x()) / w; sy = (h - localDelta.y()) / h; break;
                case DragOp::ScaleBL: sx = (w - localDelta.x()) / w; sy = (h + localDelta.y()) / h; break;
                case DragOp::ScaleBR: sx = (w + localDelta.x()) / w; sy = (h + localDelta.y()) / h; break;
                case DragOp::ScaleL: sx = (w - localDelta.x()) / w; break;
                case DragOp::ScaleR: sx = (w + localDelta.x()) / w; break;
                case DragOp::ScaleT: sy = (h - localDelta.y()) / h; break;
                case DragOp::ScaleB: sy = (h + localDelta.y()) / h; break;
                default: return;
            }
            if (w <= 0)
                sx = 1.0;
            if (h <= 0)
                sy = 1.0;
        } else {
            const qreal w = dragStartUnionRect_.width();
            const qreal h = dragStartUnionRect_.height();
            switch (dragOp_) {
                case DragOp::ScaleTL: sx = (w - delta.x()) / w; sy = (h - delta.y()) / h; break;
                case DragOp::ScaleTR: sx = (w + delta.x()) / w; sy = (h - delta.y()) / h; break;
                case DragOp::ScaleBL: sx = (w - delta.x()) / w; sy = (h + delta.y()) / h; break;
                case DragOp::ScaleBR: sx = (w + delta.x()) / w; sy = (h + delta.y()) / h; break;
                case DragOp::ScaleL: sx = (w - delta.x()) / w; break;
                case DragOp::ScaleR: sx = (w + delta.x()) / w; break;
                case DragOp::ScaleT: sy = (h - delta.y()) / h; break;
                case DragOp::ScaleB: sy = (h + delta.y()) / h; break;
                default: return;
            }
            if (w <= 0)
                sx = 1.0;
            if (h <= 0)
                sy = 1.0;
        }
        sx = qMax(0.05, sx);
        sy = qMax(0.05, sy);
        // 锚点固定（beginSelectionDrag 时算好的场景坐标），仅缩放
        m = QTransform()
                .translate(anchorScene_.x(), anchorScene_.y())
                .scale(sx, sy)
                .translate(-anchorScene_.x(), -anchorScene_.y());
    }

    for (int i = 0; i < selectedItems_.size(); ++i)
        selectedItems_[i]->setTransform(m * dragStartTransforms_[i]);
    syncSelectionFrame();
    broadcastSelectionPreview();
}

void BoardView::endSelectionDrag() {
    if (dragOp_ == DragOp::None)
        return;
    bakeTransformToData();
    dragOp_ = DragOp::None;
    broadcastSelectionPreview();  // 烘焙后选择框同步到远端
}

// 变换烘焙：每个选中笔画的映射后新点集写回数据层（id 不变），UI 图元复位变换。
// 保证数据坐标 == 显示坐标，橡皮擦/切页/远端同步无需额外换算。
void BoardView::bakeTransformToData() {
    for (QGraphicsPathItem* item : selectedItems_) {
        if (!item)
            continue;
        const QVariant v = item->data(0);
        if (!v.isValid())
            continue;
        const QTransform tf = item->transform();
        if (tf.isIdentity())
            continue;

        const QPainterPath mapped = tf.map(item->path());
        std::vector<whiteboard::Point> pts;
        pts.reserve(static_cast<size_t>(mapped.elementCount()));
        for (int i = 0; i < mapped.elementCount(); ++i) {
            const QPainterPath::Element& el = mapped.elementAt(i);
            if (el.type != QPainterPath::MoveToElement &&
                el.type != QPainterPath::LineToElement)
                continue;  // 源 path 无曲线，防御
            pts.push_back(whiteboard::Point(qRound(el.x), qRound(el.y)));
        }
        if (pts.size() < 2)
            continue;

        data_.UpdateStroke(v.toString().toStdString(), pts);
        item->setPath(mapped);
        item->setTransform(QTransform());
        // 外接矩形同步（烘焙后数据层会重算 stroke.bounding，UI 侧同步一致）；
        // 水平/垂直直线一维为 0 时做 1px 保底，保证框选粗筛可命中（QRectF 零尺寸判定恒失败）
        QRectF mappedRect = mapped.boundingRect();
        if (mappedRect.width() <= 0)
            mappedRect.setWidth(1);
        if (mappedRect.height() <= 0)
            mappedRect.setHeight(1);
        item->setData(1, mappedRect);
    }
    syncSelectionFrame();
}

QRectF BoardView::strokeSceneRect(const QGraphicsPathItem* item) const {
    return item->sceneTransform().mapRect(SelectionFrame::itemRect(item));
}

// 笔迹的真实形状（场景坐标）：对 path 做笔宽描边，
// 命中判定基于描边后的闭合形状而非零宽度裸 path。
QPainterPath BoardView::strokedScenePath(const QGraphicsPathItem* item) const {
    QPainterPathStroker stroker;
    stroker.setWidth(qMax(6.0, item->pen().widthF() + 4));
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(item->sceneTransform().map(item->path()));
}

// 精判：矩形与笔迹相交或笔迹被矩形完全包含
bool BoardView::pathHitsRect(const QGraphicsPathItem* item, const QRectF& rect) const {
    return strokedScenePath(item).intersects(rect);
}

// 精判：套索闭合路径与笔迹相交，或笔迹完全被套索圈住
bool BoardView::pathHitsLasso(const QGraphicsPathItem* item,
                              const QPainterPath& lasso) const {
    const QPainterPath shape = strokedScenePath(item);
    if (lasso.intersects(shape))
        return true;
    // 笔迹完全在套索内部（无边界相交）时用点包含兜底
    const QPainterPath p = item->sceneTransform().map(item->path());
    if (p.elementCount() > 0) {
        const QPainterPath::Element& el = p.elementAt(0);
        if (lasso.contains(QPointF(el.x, el.y)))
            return true;
    }
    return false;
}

QPen BoardView::rubberPen() {
    return QPen(QColor(120, 190, 255), 1, Qt::DashLine);
}

void BoardView::beginRubberBand(const QPointF& scenePos) {
    clearSelection();
    rubberStartScene_ = scenePos;
    dragOp_ = DragOp::RubberBand;
    // 防御：橡皮筋可能已被 scene_.clear() 删除（清空/切页/撤销），指针悬空需重建
    if (!rubberBand_ || !rubberBand_->scene()) {
        rubberBand_ = new QGraphicsRectItem();
        rubberBand_->setPen(rubberPen());
        rubberBand_->setBrush(QColor(120, 190, 255, 32));
        rubberBand_->setZValue(50);
        scene_.addItem(rubberBand_);
    }
    rubberBand_->setRect(QRectF(scenePos, scenePos));
    rubberBand_->setVisible(true);
}

void BoardView::updateRubberBand(const QPointF& scenePos) {
    if (!rubberBand_)
        return;
    rubberBand_->setRect(QRectF(rubberStartScene_, scenePos).normalized());
}

// 框选结束：两阶段判定——
// 1) 橡皮筋矩形与外接矩形相交（粗筛）；2) 矩形与笔迹真实相交/包含（精判）。
void BoardView::finishRubberBand() {
    if (rubberBand_)
        rubberBand_->setVisible(false);
    dragOp_ = DragOp::None;

    if (rubberBand_) {
        QRectF r = rubberBand_->rect().normalized();
        // 纯水平/垂直拖动生成的零尺寸框对 QRectF::intersects 恒不命中，做 1px 保底
        if (r.width() <= 0)
            r.setWidth(1);
        if (r.height() <= 0)
            r.setHeight(1);
        QList<QGraphicsPathItem*> hits;
        for (QGraphicsPathItem* item : strokeItems_) {
            if (!r.intersects(strokeSceneRect(item)))
                continue;  // 粗筛：外接矩形不相交
            if (!pathHitsRect(item, r))
                continue;  // 精判：笔迹与矩形无真实交集
            hits.append(item);
        }
        if (!hits.isEmpty())
            selectItems(hits);
    }
}

// ---------- 套索选择（浅蓝虚线笔迹 + 两阶段相交判定） ----------

void BoardView::beginLasso(const QPointF& scenePos) {
    clearSelection();
    lassoPoints_.clear();
    lassoPoints_.append(scenePos);
    lassoLastSample_ = scenePos;
    dragOp_ = DragOp::Lasso;
    // 广播套索起点（远端实时显示套索路径）
    sessionId_++;
    data_.SendToolPreview(2, std::to_string(sessionId_),
                          { whiteboard::Point(qRound(scenePos.x()), qRound(scenePos.y())) }, {});
    // 防御：预览图元可能已被 scene_.clear() 删除（清空/切页/撤销），指针悬空需重建
    if (!lassoPreview_ || !lassoPreview_->scene()) {
        lassoPreview_ = new QGraphicsPathItem();
        lassoPreview_->setPen(rubberPen());
        lassoPreview_->setBrush(Qt::NoBrush);
        lassoPreview_->setZValue(50);
        scene_.addItem(lassoPreview_);
    }
    QPainterPath path;
    path.moveTo(scenePos);
    lassoPreview_->setPath(path);
    lassoPreview_->setVisible(true);
}

void BoardView::updateLasso(const QPointF& scenePos) {
    // 间隔采样，避免点数过多
    const QPointF d = scenePos - lassoLastSample_;
    if (qAbs(d.x()) < kLassoSampleDist && qAbs(d.y()) < kLassoSampleDist)
        return;
    lassoLastSample_ = scenePos;
    lassoPoints_.append(scenePos);
    QPainterPath path = lassoPreview_->path();
    path.lineTo(scenePos);
    lassoPreview_->setPath(path);
    // 广播当前完整采样点集
    std::vector<whiteboard::Point> pts;
    pts.reserve(static_cast<size_t>(lassoPoints_.size()));
    for (const QPointF& p : lassoPoints_)
        pts.push_back(whiteboard::Point(qRound(p.x()), qRound(p.y())));
    data_.SendToolPreview(2, std::to_string(sessionId_), pts, {});
}

// 套索结束：两阶段判定——
// 1) 套索外接矩形与笔迹外接矩形相交（粗筛）；2) 套索闭合路径与笔迹真实相交/圈住（精判）。
void BoardView::finishLasso() {
    if (lassoPreview_)
        lassoPreview_->setVisible(false);
    dragOp_ = DragOp::None;

    if (lassoPoints_.size() >= 3) {
        QPainterPath lasso;
        lasso.moveTo(lassoPoints_.first());
        for (int i = 1; i < lassoPoints_.size(); ++i)
            lasso.lineTo(lassoPoints_.at(i));
        lasso.closeSubpath();

        const QRectF lassoRect = lasso.boundingRect();
        QList<QGraphicsPathItem*> hits;
        for (QGraphicsPathItem* item : strokeItems_) {
            if (!lassoRect.intersects(strokeSceneRect(item)))
                continue;  // 粗筛：外接矩形不相交
            if (!pathHitsLasso(item, lasso))
                continue;  // 精判：套索与笔迹无真实交集
            hits.append(item);
        }
        if (!hits.isEmpty())
            selectItems(hits);
    }
    lassoPoints_.clear();
    // 广播空点集：远端清除套索预览
    data_.SendToolPreview(2, std::to_string(sessionId_), {}, {});
}

void BoardView::updateHoverCursor(const QPointF& scenePos) {
    if (tool_ != Tool::Select && tool_ != Tool::Lasso) {
        setCursor(tool_ == Tool::Eraser ? Qt::BlankCursor : Qt::ArrowCursor);
        return;
    }
    if (selectionFrame_) {
        switch (selectionFrame_->handleAt(scenePos)) {
            case SelectionFrame::TL:
            case SelectionFrame::BR:
                setCursor(Qt::SizeFDiagCursor);
                return;
            case SelectionFrame::TR:
            case SelectionFrame::BL:
                setCursor(Qt::SizeBDiagCursor);
                return;
            case SelectionFrame::L:
            case SelectionFrame::R:
                setCursor(Qt::SizeHorCursor);
                return;
            case SelectionFrame::T:
            case SelectionFrame::B:
                setCursor(Qt::SizeVerCursor);
                return;
            case SelectionFrame::Rotate:
                setCursor(Qt::CrossCursor);
                return;
            default:
                break;
        }
        if (selectedItems_.contains(hitStrokeItem(scenePos))) {
            setCursor(Qt::SizeAllCursor);
            return;
        }
    }
    if (hitStrokeItem(scenePos)) {
        setCursor(Qt::PointingHandCursor);
        return;
    }
    setCursor(Qt::ArrowCursor);
}

// ---------- 鼠标事件 ----------

void BoardView::mousePressEvent(QMouseEvent* event) {
    // 中键：任意工具下都用于平移视图
    if (event->button() == Qt::MiddleButton) {
        beginPan(event->pos());
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    // 抓手工具：左键交给 ScrollHandDrag
    if (tool_ == Tool::Pan) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    const QPointF pos = mapToScene(event->pos());
    if (!scene_.sceneRect().contains(pos))
        return;

    if (tool_ == Tool::Select || tool_ == Tool::Lasso) {
        // 手柄优先；其次笔记（已选集合整体移动 / 未选则单选）；
        // 若按在当前选中集合的外接矩形内部空白处，也视为移动整个集合；
        // 空白处：选择=矩形框选，套索=虚线笔迹圈选。
        if (selectionFrame_) {
            const SelectionFrame::Handle h = selectionFrame_->handleAt(pos);
            if (h != SelectionFrame::None) {
                beginSelectionDrag(h, pos);
                event->accept();
                return;
            }
        }
        QGraphicsPathItem* item = hitStrokeItem(pos);
        if (item) {
            if (!selectedItems_.contains(item))
                selectItems({ item });
            beginSelectionDrag(SelectionFrame::None, pos);
        } else if (selectionFrame_ && selectionFrame_->rect().contains(pos)) {
            beginSelectionDrag(SelectionFrame::None, pos);  // 框内空白 = 移动集合
        } else if (tool_ == Tool::Select) {
            beginRubberBand(pos);
        } else {
            beginLasso(pos);
        }
        event->accept();
        return;
    }

    sessionId_++;
    if (tool_ == Tool::Eraser) {
        updateEraserPreview(pos);
        data_.EraserBegin(eraserRectAt(pos), sessionId_);
    } else {
        previewItem_ = createPreviewItem(pos);
        strokeToken_++;
        data_.PenBegin(toBoardPoint(pos), sessionId_, strokeToken_);
    }
    event->accept();
}

void BoardView::mouseMoveEvent(QMouseEvent* event) {
    // 中键平移优先
    if (panning_) {
        updatePan(event->pos());
        event->accept();
        return;
    }
    if (tool_ == Tool::Pan) {
        QGraphicsView::mouseMoveEvent(event);
        return;
    }
    const QPointF pos = mapToScene(event->pos());

    if (!(event->buttons() & Qt::LeftButton)) {
        // 未按下：橡皮框跟随 / 选择态悬停光标
        if (tool_ == Tool::Eraser && scene_.sceneRect().contains(pos))
            updateEraserPreview(pos);
        else if (tool_ == Tool::Select || tool_ == Tool::Lasso)
            updateHoverCursor(pos);
        QGraphicsView::mouseMoveEvent(event);
        return;
    }

    if (tool_ == Tool::Select || tool_ == Tool::Lasso) {
        if (dragOp_ == DragOp::RubberBand)
            updateRubberBand(pos);
        else if (dragOp_ == DragOp::Lasso)
            updateLasso(pos);
        else if (dragOp_ != DragOp::None)
            updateSelectionDrag(pos);
        event->accept();
        return;
    }

    if (tool_ == Tool::Eraser) {
        if (scene_.sceneRect().contains(pos)) {
            updateEraserPreview(pos);
            data_.EraserMove(eraserRectAt(pos), sessionId_);
        }
    } else if (previewItem_) {
        if (scene_.sceneRect().contains(pos)) {
            QPainterPath path = previewItem_->path();
            path.lineTo(pos);
            previewItem_->setPath(path);
            data_.PenMove(toBoardPoint(pos), sessionId_);
        }
    }
}

void BoardView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        endPan();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }
    if (tool_ == Tool::Pan) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }
    const QPointF pos = clampToScene(mapToScene(event->pos()));

    if (tool_ == Tool::Select || tool_ == Tool::Lasso) {
        if (dragOp_ == DragOp::RubberBand)
            finishRubberBand();
        else if (dragOp_ == DragOp::Lasso)
            finishLasso();
        else
            endSelectionDrag();
        updateHoverCursor(pos);
        event->accept();
        return;
    }

    if (tool_ == Tool::Eraser) {
        updateEraserPreview(pos);
        data_.EraserEnd(eraserRectAt(pos), sessionId_);
    } else if (previewItem_) {
        // 收尾点（出界时钳制到画布内），保证数据与预览一致
        QPainterPath path = previewItem_->path();
        path.lineTo(pos);
        previewItem_->setPath(path);
        data_.PenEnd(toBoardPoint(pos), sessionId_);

        pendingItems_.insert(strokeToken_, previewItem_);
        previewItem_ = nullptr;
    }
    event->accept();
}

void BoardView::leaveEvent(QEvent* event) {
    QGraphicsView::leaveEvent(event);
    if (eraserPreview_)
        eraserPreview_->setVisible(false);
}

// ---------- 数据层回调（投递回 UI 线程） ----------

void BoardView::onElementsChanged(
    std::vector<std::string> removed,
    std::vector<std::shared_ptr<whiteboard::Element>> added) {
    QMetaObject::invokeMethod(this,
        [this, removed = std::move(removed), added = std::move(added)]() {
            applyElementChanges(removed, added);
        },
        Qt::QueuedConnection);
}

void BoardView::onStrokeCommitted(uint64_t token, const std::string& id) {
    QMetaObject::invokeMethod(this, [this, token, id]() {
        applyStrokeCommitted(token, id);
    }, Qt::QueuedConnection);
}

void BoardView::onCleared() {
    QMetaObject::invokeMethod(this, [this]() {
        applyCleared();
    }, Qt::QueuedConnection);
}

void BoardView::onPageChanged() {
    QMetaObject::invokeMethod(this, [this]() {
        applyPageChanged();
    }, Qt::QueuedConnection);
}

void BoardView::onStrokePreview(std::string strokeId, uint32_t color, int width,
                                std::vector<whiteboard::Point> points) {
    QMetaObject::invokeMethod(this,
        [this, strokeId = std::move(strokeId), color, width, points = std::move(points)]() {
            applyStrokePreview(strokeId, color, width, points);
        },
        Qt::QueuedConnection);
}

void BoardView::onToolPreview(uint32_t tool, std::string sessionId,
                              std::vector<whiteboard::Point> points,
                              std::vector<std::string> elementIds) {
    QMetaObject::invokeMethod(this,
        [this, tool, sessionId = std::move(sessionId),
         points = std::move(points), elementIds = std::move(elementIds)]() {
            applyToolPreview(tool, sessionId, points, elementIds);
        },
        Qt::QueuedConnection);
}

void BoardView::onSynced() {
    QMetaObject::invokeMethod(this, [this]() {
        applySynced();
    }, Qt::QueuedConnection);
}

// 撤销/重做/远程页面操作后页面整体被替换：全量重建 + 刷新页面列表
void BoardView::applyPageChanged() {
    refreshPageIds();
    reloadPage();
    emit pagesChanged();
    emit currentPageChanged(pageIds_.indexOf(currentPageId_));
}

// FullSync 应用后：全量重建所有页面状态
void BoardView::applySynced() {
    applyPageChanged();
}

// 远程笔画实时预览：按 strokeId 维护预览图元（Begin 创建，Move 更新）
void BoardView::applyStrokePreview(const std::string& strokeId, uint32_t color, int width,
                                   const std::vector<whiteboard::Point>& points) {
    const QString key = QString::fromStdString(strokeId);
    QGraphicsPathItem* item = remotePreview_.value(key, nullptr);
    if (!item) {
        item = new QGraphicsPathItem();
        item->setPen(strokePen(color, width));
        item->setBrush(Qt::NoBrush);
        item->setZValue(90);  // 低于橡皮预览框（100）
        scene_.addItem(item);
        remotePreview_.insert(key, item);
    }
    QPainterPath path;
    for (size_t i = 0; i < points.size(); ++i) {
        const whiteboard::Point& p = points[i];
        if (i == 0)
            path.moveTo(p.x, p.y);
        else
            path.lineTo(p.x, p.y);
    }
    item->setPath(path);
}

// 远端工具预览（UI 线程执行）：
// tool=1 橡皮擦：points 非空 → 累积背景色遮罩（视觉上只覆盖橡皮擦扫过的碰撞区域，
//               整条图元保持可见，数据不动）；points 空 → 移除遮罩（End 时数据层按 id 精确删/加）。
// tool=2 套索：points 非空 → 浅蓝虚线路径预览；空 → 清除。
// tool=3 选择框：points 非空 → 虚线矩形预览（4 顶点外接）；空 → 清除。
void BoardView::applyToolPreview(uint32_t tool, const std::string& sessionId,
                                 const std::vector<whiteboard::Point>& points,
                                 const std::vector<std::string>& elementIds) {
    Q_UNUSED(elementIds);
    if (tool == 1) {
        // ---------- 远端橡皮擦遮罩（填充） + 当前矩形虚线框 ----------
        if (points.empty()) {
            // End：移除遮罩与虚线框（数据层随后按 id 精确删/加，图元恢复正确形态）
            if (remoteEraserPreview_) {
                scene_.removeItem(remoteEraserPreview_);
                delete remoteEraserPreview_;
                remoteEraserPreview_ = nullptr;
            }
            if (remoteEraserMask_) {
                scene_.removeItem(remoteEraserMask_);
                delete remoteEraserMask_;
                remoteEraserMask_ = nullptr;
            }
            remoteEraserSessionId_.clear();
            remoteEraserLastRect_ = QRectF();
            return;
        }
        // 4 点橡皮矩形 → 外接矩形
        QRectF r;
        for (size_t i = 0; i < points.size(); ++i) {
            const QPointF p(points[i].x, points[i].y);
            r = i == 0 ? QRectF(p, QSizeF(1, 1)) : r.united(QRectF(p, QSizeF(1, 1)));
        }
        if (!remoteEraserMask_ || !remoteEraserMask_->scene()) {
            remoteEraserMask_ = new QGraphicsPathItem();
            remoteEraserMask_->setPen(Qt::NoPen);  // 无边框：描累积轮廓会形成"移动轨迹"残留
            // 不透明背景色填充：视觉上等于"擦除露出画布背景"，只覆盖碰撞区域
            remoteEraserMask_->setBrush(kBoardBackground);
            // 高于笔画(0)/远端预览笔画(90)/套索与选择预览(50)，低于本地橡皮指示框(100)
            remoteEraserMask_->setZValue(98);
            scene_.addItem(remoteEraserMask_);
        }
        if (!remoteEraserPreview_ || !remoteEraserPreview_->scene()) {
            remoteEraserPreview_ = new QGraphicsRectItem();
            remoteEraserPreview_->setPen(Qt::NoPen);  // 样式同本地橡皮预览：白色方块
            remoteEraserPreview_->setBrush(QColor(255, 255, 255, 240));
            remoteEraserPreview_->setZValue(99);
            scene_.addItem(remoteEraserPreview_);
        }
        // 会话切换：重置累积遮罩，防止上次 End 丢失导致遮罩残留
        if (remoteEraserSessionId_ != QString::fromStdString(sessionId)) {
            remoteEraserSessionId_ = QString::fromStdString(sessionId);
            remoteEraserMask_->setPath(QPainterPath());
            remoteEraserLastRect_ = QRectF();
        }
        // 累积遮罩：把当前擦除矩形并入；仅与「上一帧矩形」取并集防缝——
        // 不能用累积路径 boundingRect（随轨迹变大），否则来回擦时遮罩会膨胀成一个大矩形
        QPainterPath mask = remoteEraserMask_->path();
        if (!remoteEraserLastRect_.isNull() && remoteEraserLastRect_.intersects(r))
            mask.addRect(remoteEraserLastRect_.united(r));
        else
            mask.addRect(r);
        remoteEraserLastRect_ = r;
        mask.setFillRule(Qt::WindingFill);  // 重叠区域仍填充（OddEven 会产生空洞）
        remoteEraserMask_->setPath(mask);
        // 虚线框只跟随当前矩形
        remoteEraserPreview_->setRect(r);
    } else if (tool == 2) {
        // ---------- 远端套索预览 ----------
        if (points.empty()) {
            if (remoteLassoPreview_) {
                scene_.removeItem(remoteLassoPreview_);
                delete remoteLassoPreview_;
                remoteLassoPreview_ = nullptr;
            }
            return;
        }
        if (!remoteLassoPreview_ || !remoteLassoPreview_->scene()) {
            remoteLassoPreview_ = new QGraphicsPathItem();
            remoteLassoPreview_->setPen(rubberPen());
            remoteLassoPreview_->setBrush(Qt::NoBrush);
            remoteLassoPreview_->setZValue(50);
            scene_.addItem(remoteLassoPreview_);
        }
        QPainterPath path;
        for (size_t i = 0; i < points.size(); ++i) {
            if (i == 0)
                path.moveTo(points[i].x, points[i].y);
            else
                path.lineTo(points[i].x, points[i].y);
        }
        remoteLassoPreview_->setPath(path);
        remoteLassoPreview_->setVisible(true);
    } else if (tool == 3) {
        // ---------- 远端选择框预览 ----------
        if (points.empty()) {
            if (remoteSelectionPreview_) {
                scene_.removeItem(remoteSelectionPreview_);
                delete remoteSelectionPreview_;
                remoteSelectionPreview_ = nullptr;
            }
            return;
        }
        if (!remoteSelectionPreview_ || !remoteSelectionPreview_->scene()) {
            remoteSelectionPreview_ = new QGraphicsRectItem();
            remoteSelectionPreview_->setPen(rubberPen());
            remoteSelectionPreview_->setBrush(QColor(120, 190, 255, 32));
            remoteSelectionPreview_->setZValue(50);
            scene_.addItem(remoteSelectionPreview_);
        }
        QRectF r;
        for (size_t i = 0; i < points.size(); ++i) {
            const QPointF p(points[i].x, points[i].y);
            r = i == 0 ? QRectF(p, QSizeF(1, 1)) : r.united(QRectF(p, QSizeF(1, 1)));
        }
        remoteSelectionPreview_->setRect(r);
        remoteSelectionPreview_->setVisible(true);
    }
}

void BoardView::applyElementChanges(
    const std::vector<std::string>& removed,
    const std::vector<std::shared_ptr<whiteboard::Element>>& added) {
    // 先删后加，保证 id 不冲突
    bool selectionChanged = false;
    for (const std::string& id : removed) {
        QGraphicsPathItem* item = strokeItems_.take(QString::fromStdString(id));
        if (item) {
            if (selectedItems_.removeAll(item) > 0)
                selectionChanged = true;
            scene_.removeItem(item);
            delete item;
        }
    }
    if (selectionChanged) {
        if (selectedItems_.isEmpty())
            clearSelection();
        else
            selectItems(selectedItems_);  // 重建选择框（集合收缩）
    }
    for (const auto& e : added) {
        auto* stroke = dynamic_cast<const whiteboard::Stroke*>(e.get());
        if (!stroke)
            continue;
        const QString key = QString::fromStdString(stroke->id);
        // 远程预览笔画落库：先移除预览图元，再按正式图元重建
        if (QGraphicsPathItem* preview = remotePreview_.take(key)) {
            scene_.removeItem(preview);
            delete preview;
        }
        if (strokeItems_.contains(key))
            continue;
        buildStrokeItem(*stroke, stroke->id);
    }
}

void BoardView::applyStrokeCommitted(uint64_t token, const std::string& id) {
    QGraphicsPathItem* item = pendingItems_.take(token);
    if (!item)
        return;
    // Clear/切页可能已把 item 删除（item 不再属于 scene），此时忽略
    if (!item->scene())
        return;
    item->setData(0, QString::fromStdString(id));
    // 与 buildStrokeItem 对齐：转正时补写外接矩形 data(1)。否则选择框与框选粗筛
    // 退化为 path().boundingRect()，水平/垂直直线一维为 0 时 QRectF 相交判定
    // 恒失败（直线框选不上）；此处与数据层 BoundaryRect::ToRect 一致做 1px 保底。
    QRectF br = item->path().boundingRect();
    if (br.width() <= 0)
        br.setWidth(1);
    if (br.height() <= 0)
        br.setHeight(1);
    item->setData(1, br);
    strokeItems_.insert(QString::fromStdString(id), item);
}

void BoardView::applyCleared() {
    clearSelection();
    scene_.clear();  // 删除所有图元（含预览笔画、橡皮框与框选橡皮筋）
    strokeItems_.clear();
    pendingItems_.clear();
    remotePreview_.clear();
    previewItem_ = nullptr;
    eraserPreview_ = nullptr;  // 懒重建
    rubberBand_ = nullptr;     // 已被 scene_.clear() 删除，防悬空
    lassoPreview_ = nullptr;   // 已被 scene_.clear() 删除，防悬空
    remoteEraserPreview_ = nullptr;    // 已被 scene_.clear() 删除，防悬空
    remoteEraserMask_ = nullptr;       // 已被 scene_.clear() 删除，防悬空
    remoteLassoPreview_ = nullptr;     // 已被 scene_.clear() 删除，防悬空
    remoteSelectionPreview_ = nullptr; // 已被 scene_.clear() 删除，防悬空
    remoteEraserSessionId_.clear();
    remoteEraserLastRect_ = QRectF();
    if (tool_ == Tool::Eraser)
        ensureEraserItem();
}
