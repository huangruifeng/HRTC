#include "ShapePickerPanel.h"

#include <QFont>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include "Whiteboard/whiteboard.h"

namespace {

constexpr int kCols = 3;
constexpr int kRows = 2;
constexpr int kPanelW = 204;
constexpr int kTitleHeight = 32;
constexpr int kCellSize = 56;
constexpr int kCellGap = 6;
constexpr int kMargin = 12;

const QColor kMenuBackground(0x23, 0x27, 0x2A, 0xCC);  // 同 MorePanel
const QColor kMenuBorder(0x33, 0x63, 0x6C, 0x73);
const QColor kIconNormal(0x99, 0x99, 0x99);
const QColor kIconActive(0xFF, 0x7D, 0x00);
const QColor kCellHover(0x3A, 0x40, 0x45);

QRect cellRect(int index) {
    const int col = index % kCols;
    const int row = index / kCols;
    return QRect(kMargin + col * (kCellSize + kCellGap),
                 kTitleHeight + row * (kCellSize + kCellGap), kCellSize, kCellSize);
}

// 数据层 BuildShape 轮廓 → QPainterPath（与图形落库几何完全一致）
QPainterPath buildShapePath(int kind, const QRectF& rc) {
    QPainterPath path;
    const whiteboard::Rect r(qRound(rc.x()), qRound(rc.y()),
                             qRound(rc.width()), qRound(rc.height()));
    for (const auto& sp : whiteboard::BuildShape(kind, r)) {
        if (sp.points.empty())
            continue;
        path.moveTo(sp.points[0].x, sp.points[0].y);
        for (size_t i = 1; i < sp.points.size(); ++i)
            path.lineTo(sp.points[i].x, sp.points[i].y);
        if (sp.closed)
            path.closeSubpath();
    }
    return path;
}

}  // namespace

ShapePickerPanel::ShapePickerPanel(QWidget* parent) : QWidget(parent) {
    // Windows 下 WA_TranslucentBackground 需要 FramelessWindowHint 才生效，
    // 否则圆角外未绘制区域会被渲染成黑块
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(kPanelW, kTitleHeight + kRows * kCellSize + (kRows - 1) * kCellGap + kMargin);
    setMouseTracking(true);
}

void ShapePickerPanel::setCurrentKind(int kind) {
    kind_ = kind;
    update();
}

int ShapePickerPanel::cellAt(const QPoint& pos) const {
    for (int i = 0; i < kCols * kRows; ++i) {
        if (cellRect(i).contains(pos))
            return i;
    }
    return -1;
}

void ShapePickerPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // 圆角背景 + 边框（同 MorePanel 风格）
    p.setPen(QPen(kMenuBorder, 1.0));
    p.setBrush(kMenuBackground);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);

    // 标题"图形"
    QFont f = font();
    f.setPixelSize(20);
    p.setFont(f);
    p.setPen(QColor(0xEE, 0xEE, 0xEE));
    p.drawText(QRect(16, 4, width() - 32, 26), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("图形"));

    // 六形状格子（索引 = GraphicKind：矩形/圆形/椭圆/三角形/五边形/五角星）
    for (int i = 0; i < kCols * kRows; ++i) {
        const QRect cell = cellRect(i);
        const bool active = (i == kind_);
        const bool hover = (i == hoverIndex_);
        if (active || hover) {
            QColor bg = hover ? kCellHover : kIconActive;
            bg.setAlpha(hover ? 255 : 40);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(cell, 6.0, 6.0);
        }

        // 展示矩形：圆形收成 1:1、椭圆压扁，保证形状可区分
        QRectF rc = QRectF(cell).adjusted(11, 11, -11, -11);
        if (i == 1)
            rc = rc.adjusted(2, 2, -2, -2);
        else if (i == 2)
            rc = rc.adjusted(0, 7, 0, -7);
        p.setPen(QPen(active ? kIconActive : kIconNormal, 2.2, Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(buildShapePath(i, rc));
    }
}

void ShapePickerPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    hoverIndex_ = -1;
    emit closed();
}

void ShapePickerPanel::mouseMoveEvent(QMouseEvent* event) {
    const int idx = cellAt(event->pos());
    if (idx != hoverIndex_) {
        hoverIndex_ = idx;
        update();
    }
}

void ShapePickerPanel::mousePressEvent(QMouseEvent* event) {
    const int idx = cellAt(event->pos());
    if (idx < 0) {
        // 未命中格子：交给 Popup 基类逻辑（面板外点击 → 关闭并透传；
        // 否则 popup 会吃掉外部点击且不关闭）
        QWidget::mousePressEvent(event);
        return;
    }
    kind_ = idx;
    update();
    emit shapeSelected(idx);
    hide();
}

void ShapePickerPanel::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    if (hoverIndex_ != -1) {
        hoverIndex_ = -1;
        update();
    }
}
