#include "SelectionFrame.h"

#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QPainter>
#include <QPen>
#include <QtMath>

namespace {
const QColor kFrameColor(74, 144, 217);
const QColor kDeleteColor(220, 53, 69);
}  // namespace

SelectionFrame::SelectionFrame(const QList<QGraphicsPathItem*>& targets)
    : targets_(targets) {
    handlePos_.fill(QPointF(), HandleCount);
    handleRect_.fill(QRectF(), HandleCount);
    handleItems_.fill(nullptr, HandleCount);

    // 8 个方块手柄子图元（白底蓝边），旋转手柄由 paint 绘制
    for (int i = TL; i <= B; ++i) {
        auto* h = new QGraphicsRectItem(this);
        h->setPen(QPen(kFrameColor, 1));
        h->setBrush(QColor(255, 255, 255));
        h->setRect(0, 0, kHandleSize, kHandleSize);
        h->setZValue(1);
        handleItems_[i] = h;
    }
    setZValue(50);
    sync();
}

QRectF SelectionFrame::itemRect(const QGraphicsPathItem* item) {
    // 数据层白板笔记已维护外接矩形（stroke.bounding，由 buildStrokeItem 写入 data(1)），
    // 直接复用；非正式笔记（预览/无 data）才退化为按 path 计算。
    const QVariant v = item->data(1);
    QRectF r = v.isValid() ? v.toRectF() : item->path().boundingRect();
    // 水平/垂直直线外接矩形一维为 0：QRectF::intersects 对零尺寸矩形恒返回 false，
    // 会导致框选粗筛、选择框框内判定与手柄布局退化。统一做 1px 保底
    // （与数据层 BoundaryRect::ToRect 的保护一致）。
    if (r.width() <= 0)
        r.setWidth(1);
    if (r.height() <= 0)
        r.setHeight(1);
    return r;
}

void SelectionFrame::layoutHandles() {
    const qreal hs = kHandleSize;

    // 所有目标外接矩形（各自变换映射到场景）的并集
    rect_ = QRectF();
    for (QGraphicsPathItem* t : targets_) {
        if (!t)
            continue;
        const QRectF r = t->sceneTransform().mapRect(itemRect(t));
        rect_ = rect_.isNull() ? r : rect_.united(r);
    }

    const QPointF tl = rect_.topLeft();
    const QPointF tr = rect_.topRight();
    const QPointF bl = rect_.bottomLeft();
    const QPointF br = rect_.bottomRight();
    const QPointF c = rect_.center();

    handlePos_[TL] = tl;
    handlePos_[TR] = tr;
    handlePos_[BL] = bl;
    handlePos_[BR] = br;
    handlePos_[L] = QPointF(rect_.left(), c.y());
    handlePos_[R] = QPointF(rect_.right(), c.y());
    handlePos_[T] = QPointF(c.x(), rect_.top());
    handlePos_[B] = QPointF(c.x(), rect_.bottom());

    // 旋转手柄：从顶边中点沿目标旋转后的外法线方向外移
    const QPointF topMid(c.x(), rect_.top());
    QPointF dir = topMid - c;
    const qreal len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
    const QPointF unit = len > 1e-6 ? dir / len : QPointF(0, -1);
    handlePos_[Rotate] = topMid + unit * kRotateOffset;

    // 删除按钮：右上角斜向外移（避开 TR 缩放手柄）
    handlePos_[Delete] = QPointF(rect_.right() + kDeleteOffset,
                                 rect_.top() - kDeleteOffset);

    for (int i = TL; i <= B; ++i) {
        handleRect_[i] = QRectF(handlePos_[i].x() - hs / 2,
                                handlePos_[i].y() - hs / 2, hs, hs);
        if (handleItems_[i])
            handleItems_[i]->setPos(handleRect_[i].topLeft());
    }
    const qreal r = kRotateRadius + 4;
    handleRect_[Rotate] = QRectF(handlePos_[Rotate].x() - r,
                                 handlePos_[Rotate].y() - r, r * 2, r * 2);
    const qreal dr = kDeleteRadius + 4;
    handleRect_[Delete] = QRectF(handlePos_[Delete].x() - dr,
                                 handlePos_[Delete].y() - dr, dr * 2, dr * 2);
}

void SelectionFrame::sync() {
    prepareGeometryChange();
    layoutHandles();
    update();
}

SelectionFrame::Handle SelectionFrame::handleAt(const QPointF& scenePos) const {
    for (int i = TL; i <= Delete; ++i) {
        if (handleRect_[i].contains(scenePos))
            return static_cast<Handle>(i);
    }
    return None;
}

QRectF SelectionFrame::boundingRect() const {
    return rect_.adjusted(-kHandleSize - 4, -kRotateOffset - 16,
                          kDeleteOffset + kDeleteRadius + 4, kHandleSize + 4);
}

void SelectionFrame::paint(QPainter* painter,
                           const QStyleOptionGraphicsItem* option,
                           QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing, true);

    // 虚线外接矩形
    QPen framePen(kFrameColor, 1.2, Qt::DashLine);
    painter->setPen(framePen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(rect_);

    // 旋转手柄连线 + 圆
    const QPointF topMid(rect_.center().x(), rect_.top());
    painter->setPen(QPen(kFrameColor, 1.2, Qt::SolidLine));
    painter->drawLine(topMid, handlePos_[Rotate]);
    painter->setBrush(kFrameColor);
    painter->drawEllipse(handlePos_[Rotate], kRotateRadius, kRotateRadius);

    // 删除按钮：红底圆 + 白色十字符号
    const QPointF dp = handlePos_[Delete];
    painter->setPen(QPen(kDeleteColor.darker(120), 1));
    painter->setBrush(kDeleteColor);
    painter->drawEllipse(dp, kDeleteRadius, kDeleteRadius);
    painter->setPen(QPen(Qt::white, 2));
    const qreal cr = kDeleteRadius * 0.45;
    painter->drawLine(QPointF(dp.x() - cr, dp.y()), QPointF(dp.x() + cr, dp.y()));
    painter->drawLine(QPointF(dp.x(), dp.y() - cr), QPointF(dp.x(), dp.y() + cr));
}
