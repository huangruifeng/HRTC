#include "BoardDisk.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>

#include <QtMath>

#include "BoardIcons.h"
#include "BoardUtil.h"
#include "ColorPickerPanel.h"

namespace {

// ---------- 布局常量（逻辑像素） ----------
constexpr int kBallDiameter = 56;     // 悬浮球 / 中心球直径
constexpr int kInnerRadius = 66;      // 内环按钮圆心半径
constexpr int kInnerButton = 44;      // 内环按钮直径
constexpr int kOuterRadius = 126;     // 外环按钮圆心半径
constexpr int kOuterButton = 36;      // 外环按钮直径
constexpr int kExpandedSize = 296;    // 展开态窗口边长（含 2px 余量）
constexpr int kDiskRadius = kOuterRadius + kOuterButton / 2 + 4;  // 圆盘背景半径

const QColor kDiskBg(35, 39, 42, 204);       // 同工具栏圆角背景
const QColor kBallBg(35, 39, 42, 240);       // 中心球更深一档
const QColor kButtonBg(255, 255, 255, 26);   // 环上按钮底色（浅白浮层）
const QColor kHotRing(0xE0, 0xE0, 0xE0, 140);  // 焦点类目描边
const QColor kActiveColor(0xFF, 0x7D, 0x00);
const QColor kNormalColor(0x99, 0x99, 0x99);

// 色板（同 PenSettingPanel 11 色）
const char* const kPalette[] = {
    "#000000", "#FFFFFF", "#FF2C1B", "#FF8B00", "#331EB5", "#306ED9",
    "#306C00", "#66D552", "#FF1ED0", "#4FA0B7", "#8B7E6E",
};

// 橡皮大小档位（高度短边，擦除区宽 = 高 × 1.5）
constexpr int kEraserSteps = 5;
constexpr int kEraserSizes[kEraserSteps] = { 12, 24, 40, 60, 90 };

// 缩放绘制 48px 图标到目标矩形
void drawIcon(QPainter& p, const QPixmap& icon, const QRectF& rect) {
    if (icon.isNull())
        return;
    p.drawPixmap(rect.toRect(), icon);
}

}  // namespace

BoardDisk::BoardDisk(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("boardDisk"));
    setAttribute(Qt::WA_TranslucentBackground);  // 圆形外透明（需 frameless 配合，
    // 无窗口标志的子 widget 天然透明，无需 FramelessWindowHint）
    setFixedSize(kBallDiameter, kBallDiameter);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);

    ballRect_ = rect();
    refreshBallIcon();
}

// ---------- 状态同步 ----------

void BoardDisk::setCurrentTool(BoardView::Tool tool) {
    tool_ = tool;
    // 外环焦点跟随当前工具（撤销/重做/抓手不改变焦点）
    switch (tool) {
        case BoardView::Tool::Pen:
            focusKind_ = (penKind_ == PenSettingPanel::PenKind::Highlighter)
                             ? InnerButton::Type::Highlighter
                             : InnerButton::Type::Pen;
            break;
        case BoardView::Tool::Eraser:
            focusKind_ = InnerButton::Type::Eraser;
            break;
        case BoardView::Tool::Select:
        case BoardView::Tool::Lasso:
            focusKind_ = InnerButton::Type::Select;
            break;
        case BoardView::Tool::Mouse:
            focusKind_ = InnerButton::Type::Mouse;
            break;
        case BoardView::Tool::Shape:
        case BoardView::Tool::MindMap:
        case BoardView::Tool::Table:
        case BoardView::Tool::Text:
        case BoardView::Tool::Widget:
            focusKind_ = InnerButton::Type::More;
            break;
        case BoardView::Tool::Pan:
            break;  // 抓手仅底部工具栏入口：保持原焦点
    }
    refreshBallIcon();
    rebuildRing();
    update();
}

void BoardDisk::setPenState(PenSettingPanel::PenKind kind, uint32_t color, int width) {
    penKind_ = kind;
    // 统一存纯色（低 24 位；荧光笔透明度由 MainWindow 在 alpha 位统一编码）
    const uint32_t rgb = color & 0x00FFFFFFu;
    if (kind == PenSettingPanel::PenKind::Highlighter) {
        hlColor_ = rgb;
        hlWidth_ = width;
    } else {
        normalColor_ = rgb;
        normalWidth_ = width;
    }
    // 当前是笔工具：笔类型切换同步外环焦点
    if (tool_ == BoardView::Tool::Pen) {
        focusKind_ = (kind == PenSettingPanel::PenKind::Highlighter)
                         ? InnerButton::Type::Highlighter
                         : InnerButton::Type::Pen;
    }
    refreshBallIcon();
    rebuildRing();
    update();
}

void BoardDisk::setEraserSize(int size) {
    eraserSize_ = size;
    rebuildRing();
    update();
}

void BoardDisk::setZoomPercent(qreal percent) {
    zoomPercent_ = percent;
    update();
}

void BoardDisk::setBounds(const QRect& rect) {
    bounds_ = rect;
    // 首次有效几何：贴画布右缘、垂直中部偏上（3/8 高度处；构造期矩形尺寸未就绪，
    // 高度足够才定位，否则会被后续 clamp 钓到错误位置）
    if (!positioned_ && rect.isValid() && rect.height() >= kExpandedSize) {
        positioned_ = true;
        move(rect.right() - kBallDiameter - 12,
             qRound(rect.top() + rect.height() * 0.375 - kBallDiameter / 2.0));
        return;
    }
    // 范围变化后钮回活动区（尺寸不变，中心不变）
    const QPointF center = QPointF(geometry().center());
    move(clampedPos(size(), center));
}

// 展开态切换：中心点保持不变（头文件内联 expanded()）
void BoardDisk::setExpanded(bool on) {
    if (expanded_ == on)
        return;
    applyGeometry(on);
}

QRect BoardDisk::diskGlobalRect() const {
    return QRect(mapToGlobal(QPoint(0, 0)), size());
}

int BoardDisk::widthForKind(PenSettingPanel::PenKind kind, int index) {
    static const int kNormal[] = { 3, 6, 12 };
    static const int kHighlighter[] = { 12, 24, 48 };
    const int* table = (kind == PenSettingPanel::PenKind::Highlighter) ? kHighlighter : kNormal;
    return table[qBound(0, index, 2)];
}

// ---------- 几何 ----------

// 中心点 → clamp 后左上角（活动区为空时退回父 widget 区域）
QPoint BoardDisk::clampedPos(const QSize& size, const QPointF& center) const {
    QRect area = bounds_;
    if (area.isNull()) {
        if (parentWidget())
            area = parentWidget()->rect();
        else
            return QPoint(qRound(center.x() - size.width() / 2.0),
                          qRound(center.y() - size.height() / 2.0));
    }
    const qreal halfW = size.width() / 2.0;
    const qreal halfH = size.height() / 2.0;
    // 圆盘展开后允许圆心贴边界（大圆越界露出裁掉），悬浮球完全钳入
    const int x = qRound(qBound(area.left() + halfW, center.x(), area.right() - halfW));
    const int y = qRound(qBound(area.top() + halfH, center.y(), area.bottom() - halfH));
    return QPoint(x - qRound(halfW), y - qRound(halfH));
}

// 展开/收起几何切换：中心点保持不变；展开态重建内环/外环按钮表
void BoardDisk::applyGeometry(bool expand) {
    expanded_ = expand;
    const QSize size = expand ? QSize(kExpandedSize, kExpandedSize)
                              : QSize(kBallDiameter, kBallDiameter);
    const QPointF center = QRectF(QPointF(geometry().topLeft()),
                                  QPointF(geometry().bottomRight())).center();
    const QPoint topLeft = clampedPos(size, center);
    setFixedSize(size);
    move(topLeft);

    if (expand) {
        // 内环 8 钮：顶部起顺时针（画笔/荧光棒/擦除/选择/鼠标/撤销/重做/更多）
        const InnerButton::Type order[] = {
            InnerButton::Type::Pen, InnerButton::Type::Highlighter,
            InnerButton::Type::Eraser, InnerButton::Type::Select,
            InnerButton::Type::Mouse, InnerButton::Type::Undo,
            InnerButton::Type::Redo, InnerButton::Type::More,
        };
        const QPointF c = rect().center();
        innerButtons_.clear();
        for (int i = 0; i < 8; ++i) {
            const qreal angle = qDegreesToRadians(-90.0 + i * 45.0);
            InnerButton button;
            const qreal cx = c.x() + kInnerRadius * std::cos(angle);
            const qreal cy = c.y() + kInnerRadius * std::sin(angle);
            button.rect = QRect(qRound(cx - kInnerButton / 2.0),
                                qRound(cy - kInnerButton / 2.0),
                                kInnerButton, kInnerButton);
            button.type = order[i];
            innerButtons_.push_back(button);
        }
        ballRect_ = QRect(qRound(c.x() - kBallDiameter / 2.0),
                          qRound(c.y() - kBallDiameter / 2.0),
                          kBallDiameter, kBallDiameter);
        rebuildRing();
    } else {
        innerButtons_.clear();
        ringButtons_.clear();
        ballRect_ = rect();
    }
    update();
}

// ---------- 外环内容 ----------

void BoardDisk::rebuildRing() {
    ringButtons_.clear();
    if (!expanded_)
        return;

    const auto append = [this](RingButton::Type type, int id, const QPointF& center,
                               bool checked) {
        RingButton button;
        button.rect = QRect(qRound(center.x() - kOuterButton / 2.0),
                            qRound(center.y() - kOuterButton / 2.0),
                            kOuterButton, kOuterButton);
        button.type = type;
        button.id = id;
        button.checked = checked;
        ringButtons_.push_back(button);
    };

    // 按钮类型序列（含选中态计算）→ 均匀角度分布
    QVector<RingButton> items;
    const auto addItem = [&items](RingButton::Type type, int id, bool checked) {
        RingButton button;
        button.type = type;
        button.id = id;
        button.checked = checked;
        items.push_back(button);
    };

    const bool isHighlighter = focusKind_ == InnerButton::Type::Highlighter;
    const uint32_t penColor = isHighlighter ? hlColor_ : normalColor_;
    const int penWidth = isHighlighter ? hlWidth_ : normalWidth_;

    switch (focusKind_) {
        case InnerButton::Type::Pen:
        case InnerButton::Type::Highlighter:
            for (int i = 0; i < 3; ++i)
                addItem(RingButton::Type::PenWidth, i,
                        widthForKind(penKind_, i) == penWidth);
            for (int i = 0; i < kPaletteCount; ++i)
                addItem(RingButton::Type::PenColor, i,
                        qColorToColorRef(QColor(kPalette[i])) == penColor);
            addItem(RingButton::Type::Wheel, 0, false);
            break;
        case InnerButton::Type::Eraser:
            for (int i = 0; i < kEraserSteps; ++i)
                addItem(RingButton::Type::EraserSize, i, kEraserSizes[i] == eraserSize_);
            addItem(RingButton::Type::Clear, 0, false);
            break;
        case InnerButton::Type::Select:
            addItem(RingButton::Type::RubberBand, 0, tool_ == BoardView::Tool::Select);
            addItem(RingButton::Type::Lasso, 0, tool_ == BoardView::Tool::Lasso);
            break;
        case InnerButton::Type::Mouse:
            addItem(RingButton::Type::ZoomOut, 0, false);
            addItem(RingButton::Type::ZoomReset, 0, false);
            addItem(RingButton::Type::ZoomIn, 0, false);
            break;
        case InnerButton::Type::More:
            addItem(RingButton::Type::Text, 0, tool_ == BoardView::Tool::Text);
            addItem(RingButton::Type::Shape, 0, tool_ == BoardView::Tool::Shape);
            addItem(RingButton::Type::Table, 0, tool_ == BoardView::Tool::Table);
            addItem(RingButton::Type::Widget, 0, tool_ == BoardView::Tool::Widget);
            addItem(RingButton::Type::Menu, 0, false);
            break;
        case InnerButton::Type::Undo:
        case InnerButton::Type::Redo:
            break;  // 撤销/重做无外环
    }

    const QPointF c = rect().center();
    const int count = items.size();
    // 紧凑相邻排列：固定角间隔，整体从右侧（0°）开始——整圈均布自 0°
    // 顺时针展开；不足整圈时以右侧为中心对称（按钮少时挨在一起，
    // 如 2 钮相隔 40°而非 180°对分，多时退化为均布）
    const qreal full = 360.0 / count;
    const qreal step = qMin<qreal>(40.0, full);
    const qreal start = (count * step >= 360.0) ? 0.0 : -(count - 1) * step / 2.0;
    for (int i = 0; i < count; ++i) {
        const qreal angle = qDegreesToRadians(start + i * step);
        const QPointF center(c.x() + kOuterRadius * std::cos(angle),
                             c.y() + kOuterRadius * std::sin(angle));
        append(items[i].type, items[i].id, center, items[i].checked);
    }
}

// ---------- 悬浮球图标 ----------

void BoardDisk::refreshBallIcon() {
    using namespace BoardIcons;
    QPixmap icon;
    switch (tool_) {
        case BoardView::Tool::Pen:
            icon = (penKind_ == PenSettingPanel::PenKind::Highlighter)
                       ? pixmap(Glyph::Highlighter, true)
                       : QPixmap(QStringLiteral(":/icons/pen_pressed.png"));
            break;
        case BoardView::Tool::Eraser:
            icon = QPixmap(QStringLiteral(":/icons/eraser_pressed.png"));
            break;
        case BoardView::Tool::Select:
            icon = QPixmap(QStringLiteral(":/icons/select_pressed.png"));
            break;
        case BoardView::Tool::Lasso:
            icon = pixmap(Glyph::Lasso, true);
            break;
        case BoardView::Tool::Pan:
            icon = pixmap(Glyph::Hand, true);
            break;
        case BoardView::Tool::Shape:
            icon = pixmap(Glyph::Shape, true);
            break;
        case BoardView::Tool::MindMap:
            icon = pixmap(Glyph::MindMap, true);
            break;
        case BoardView::Tool::Table:
            icon = pixmap(Glyph::Table, true);
            break;
        case BoardView::Tool::Text:
            icon = pixmap(Glyph::Text, true);
            break;
        case BoardView::Tool::Widget:
            icon = pixmap(Glyph::Widget, true);
            break;
        case BoardView::Tool::Mouse:
            icon = pixmap(Glyph::Mouse, true);
            break;
    }
    ballIcon_ = icon;
}

// ---------- 绘制 ----------

void BoardDisk::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    if (!expanded_) {
        // 收起：悬浮球
        p.setPen(Qt::NoPen);
        p.setBrush(kDiskBg);
        p.drawEllipse(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
        drawIcon(p, ballIcon_, QRectF(rect()).adjusted(12, 12, -12, -12));
        return;
    }

    const QPointF c = rect().center();

    // 圆盘背景
    p.setPen(Qt::NoPen);
    p.setBrush(kDiskBg);
    p.drawEllipse(c, kDiskRadius, kDiskRadius);

    // 外环按钮
    for (const RingButton& button : ringButtons_) {
        const QRectF r(button.rect);
        const QPointF center = r.center();
        p.setPen(button.checked ? QPen(kActiveColor, 2.0) : Qt::NoPen);
        p.setBrush(kButtonBg);
        p.drawEllipse(center, r.width() / 2.0, r.height() / 2.0);

        const QRectF inner = r.adjusted(6, 6, -6, -6);  // 图标区 24x24
        switch (button.type) {
            case RingButton::Type::PenWidth: {
                // 粗细示意：档位越粗线越宽（4 / 8 / 14px）
                static const qreal widths[] = { 4.0, 8.0, 14.0 };
                p.setPen(QPen(button.checked ? kActiveColor : QColor(0xE0, 0xE0, 0xE0),
                              widths[button.id], Qt::SolidLine, Qt::RoundCap));
                p.drawLine(QPointF(r.left() + 7, center.y()), QPointF(r.right() - 7, center.y()));
                break;
            }
            case RingButton::Type::PenColor: {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(kPalette[button.id]));
                p.drawEllipse(center, r.width() / 2.0 - 5.0, r.height() / 2.0 - 5.0);
                break;
            }
            case RingButton::Type::Wheel:
                drawIcon(p, BoardIcons::pixmap(BoardIcons::Glyph::ColorWheel, false), inner);
                break;
            case RingButton::Type::EraserSize: {
                // 大小示意：横向长方形（宽高比 1.5 同擦除区域；高度按档位 7/10/13/16/19px）
                static const qreal sides[] = { 7.0, 10.0, 13.0, 16.0, 19.0 };
                p.setPen(QPen(button.checked ? kActiveColor : QColor(0xE0, 0xE0, 0xE0), 1.4));
                p.setBrush(QColor(255, 255, 255, 230));
                const qreal s = sides[button.id];
                p.drawRect(QRectF(center.x() - s * 0.75, center.y() - s / 2.0, s * 1.5, s));
                break;
            }
            case RingButton::Type::Clear:
                drawIcon(p, BoardIcons::pixmap(BoardIcons::Glyph::Clear, button.checked), inner);
                break;
            case RingButton::Type::RubberBand:
                drawIcon(p, QPixmap(QStringLiteral(":/icons/select_normal.png")), inner);
                break;
            case RingButton::Type::Lasso:
                drawIcon(p, BoardIcons::pixmap(BoardIcons::Glyph::Lasso, button.checked), inner);
                break;
            case RingButton::Type::ZoomOut:
                drawIcon(p, BoardIcons::pixmap(BoardIcons::Glyph::ZoomOut, false), inner);
                break;
            case RingButton::Type::ZoomReset: {
                drawIcon(p, BoardIcons::pixmap(BoardIcons::Glyph::ZoomReset, false), inner);
                QFont f = font();
                f.setPixelSize(11);
                f.setBold(true);
                p.setFont(f);
                p.setPen(QColor(0xE0, 0xE0, 0xE0));
                p.drawText(r.adjusted(-12, 0, 12, 12),
                           Qt::AlignHCenter | Qt::AlignBottom,
                           QStringLiteral("%1%").arg(qRound(zoomPercent_)));
                break;
            }
            case RingButton::Type::ZoomIn:
                drawIcon(p, BoardIcons::pixmap(BoardIcons::Glyph::ZoomIn, false), inner);
                break;
            case RingButton::Type::Shape:
                drawIcon(p, BoardIcons::pixmap(BoardIcons::Glyph::Shape, button.checked), inner);
                break;
            case RingButton::Type::Widget:
                drawIcon(p, BoardIcons::pixmap(BoardIcons::Glyph::Widget, button.checked), inner);
                break;
            case RingButton::Type::Text:
                drawIcon(p, BoardIcons::pixmap(BoardIcons::Glyph::Text, button.checked), inner);
                break;
            case RingButton::Type::Table:
                drawIcon(p, BoardIcons::pixmap(BoardIcons::Glyph::Table, button.checked), inner);
                break;
            case RingButton::Type::Menu:
                drawIcon(p, BoardIcons::pixmap(BoardIcons::Glyph::More, false), inner);
                break;
        }
    }

    // 内环按钮（高亮态按当前工具动态计算，不依赖构建时快照）
    const auto kindForTool = [this]() -> InnerButton::Type {
        switch (tool_) {
            case BoardView::Tool::Pen:
                return (penKind_ == PenSettingPanel::PenKind::Highlighter)
                           ? InnerButton::Type::Highlighter
                           : InnerButton::Type::Pen;
            case BoardView::Tool::Eraser:
                return InnerButton::Type::Eraser;
            case BoardView::Tool::Select:
            case BoardView::Tool::Lasso:
                return InnerButton::Type::Select;
            case BoardView::Tool::Mouse:
                return InnerButton::Type::Mouse;
            case BoardView::Tool::Shape:
            case BoardView::Tool::MindMap:
            case BoardView::Tool::Table:
            case BoardView::Tool::Text:
            case BoardView::Tool::Widget:
                return InnerButton::Type::More;
            default:  // 抓手等：无高亮
                return InnerButton::Type::Undo;
        }
    }();
    for (const InnerButton& button : innerButtons_) {
        const QRectF r(button.rect);
        const QPointF center = r.center();
        const bool active = (button.type == kindForTool);
        p.setPen(Qt::NoPen);
        p.setBrush(kButtonBg);
        p.drawEllipse(center, r.width() / 2.0, r.height() / 2.0);

        // 焦点类目：浅白描边（当前工具类目为橙描边，见下）
        if (static_cast<int>(focusKind_) == static_cast<int>(button.type) && !active) {
            p.setPen(QPen(kHotRing, 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(center, r.width() / 2.0 - 1.0, r.height() / 2.0 - 1.0);
            p.setBrush(kButtonBg);
        }

        const QRectF iconRect = r.adjusted(8, 8, -8, -8);  // 图标区 28x28
        QPixmap icon;
        using namespace BoardIcons;
        switch (button.type) {
            case InnerButton::Type::Pen:
                icon = QPixmap(active ? QStringLiteral(":/icons/pen_pressed.png")
                                      : QStringLiteral(":/icons/pen_normal.png"));
                break;
            case InnerButton::Type::Highlighter:
                icon = pixmap(Glyph::Highlighter, active);
                break;
            case InnerButton::Type::Eraser:
                icon = QPixmap(active ? QStringLiteral(":/icons/eraser_pressed.png")
                                      : QStringLiteral(":/icons/eraser_normal.png"));
                break;
            case InnerButton::Type::Select:
                icon = QPixmap(active ? QStringLiteral(":/icons/select_pressed.png")
                                      : QStringLiteral(":/icons/select_normal.png"));
                break;
            case InnerButton::Type::Mouse:
                icon = pixmap(Glyph::Mouse, active);
                break;
            case InnerButton::Type::Undo:
                icon = pixmap(Glyph::Undo, false);
                break;
            case InnerButton::Type::Redo:
                icon = pixmap(Glyph::Redo, false);
                break;
            case InnerButton::Type::More:
                icon = pixmap(Glyph::More, active);
                break;
        }
        drawIcon(p, icon, iconRect);

        if (active) {  // 选中：橙描边（画在图标之上，覆盖按钮边缘）
            p.setPen(QPen(kActiveColor, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(center, r.width() / 2.0 - 1.0, r.height() / 2.0 - 1.0);
        }
    }

    // 中心球
    p.setPen(Qt::NoPen);
    p.setBrush(kBallBg);
    p.drawEllipse(c, kBallDiameter / 2.0, kBallDiameter / 2.0);
    p.setPen(QPen(QColor(255, 255, 255, 40), 1.0));  // 中心球高亮描边
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, kBallDiameter / 2.0 - 0.5, kBallDiameter / 2.0 - 0.5);
    drawIcon(p, ballIcon_, QRectF(ballRect_).adjusted(12, 12, -12, -12));
}

// ---------- 鼠标交互 ----------

void BoardDisk::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    pressGlobalPos_ = event->globalPos();
    pressWidgetPos_ = geometry().topLeft();  // 父坐标（拖动目标基准）
    pressMoved_ = false;
    pressOnBall_ = ballRect_.contains(event->pos());
    grabMouse();  // 独占鼠标事件：拖快出界仍连续跟踪（release 必达）
    event->accept();
}

void BoardDisk::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton) || !pressOnBall_) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const QPoint delta = event->globalPos() - pressGlobalPos_;
    if (!pressMoved_ && delta.manhattanLength() > dragThreshold_)
        pressMoved_ = true;
    if (pressMoved_) {
        // 拖动目标 = 按下位置 + 全量位移（保持相对抓取点；clamp 活动区）
        const QPoint topLeft = pressWidgetPos_ + delta;
        const QPointF center(topLeft.x() + size().width() / 2.0,
                             topLeft.y() + size().height() / 2.0);
        move(clampedPos(size(), center));
    }
    event->accept();
}

void BoardDisk::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    releaseMouse();
    const QPoint pos = event->pos();
    if (pressOnBall_) {
        if (!pressMoved_) {  // 点击（非拖动）
            if (expanded_)
                setExpanded(false);  // 中心球点击：收起
            else
                setExpanded(true);   // 悬浮球点击：展开
        }
        pressOnBall_ = false;
        event->accept();
        return;
    }
    if (!expanded_ || pressMoved_) {  // 收起态非球区无操作 / 拖动结束
        event->accept();
        return;
    }

    // 展开态：外环 → 内环命中（release 与 press 同钮才触发，由 press 时刻无法
    // 记录索引（简单起见以 release 命中为准；按下未移动即可视为同钮）
    for (const RingButton& button : ringButtons_) {
        if (button.rect.contains(pos)) {
            ringClicked(button);
            event->accept();
            return;
        }
    }
    for (const InnerButton& button : innerButtons_) {
        if (button.rect.contains(pos)) {
            innerClicked(button);
            event->accept();
            return;
        }
    }
    event->accept();
}

// 外环点击：修改参数/发动作信号（有参数变化的重建外环选中态）
void BoardDisk::ringClicked(const RingButton& button) {
    uint32_t& colorRef = (penKind_ == PenSettingPanel::PenKind::Highlighter) ? hlColor_
                                                                             : normalColor_;
    int& widthRef = (penKind_ == PenSettingPanel::PenKind::Highlighter) ? hlWidth_
                                                                        : normalWidth_;
    switch (button.type) {
        case RingButton::Type::PenWidth:
            widthRef = widthForKind(penKind_, button.id);
            emit penParamChanged(penKind_, colorRef, widthRef);
            break;
        case RingButton::Type::PenColor:
            colorRef = qColorToColorRef(QColor(kPalette[button.id]));
            emit penParamChanged(penKind_, colorRef, widthRef);
            break;
        case RingButton::Type::Wheel:
            showWheelPopup(button.rect);
            return;  // 弹窗不重建外环
        case RingButton::Type::EraserSize:
            eraserSize_ = kEraserSizes[button.id];
            emit eraserSizeChanged(eraserSize_);
            break;
        case RingButton::Type::Clear:
            emit clearPanelRequested();
            return;
        case RingButton::Type::RubberBand:
            emit toolSelected(BoardView::Tool::Select);
            return;
        case RingButton::Type::Lasso:
            emit toolSelected(BoardView::Tool::Lasso);
            return;
        case RingButton::Type::ZoomOut:
            emit zoomOutRequested();
            return;
        case RingButton::Type::ZoomReset:
            emit zoomResetRequested();
            return;
        case RingButton::Type::ZoomIn:
            emit zoomInRequested();
            return;
        case RingButton::Type::Shape:
            emit shapePanelRequested();
            return;
        case RingButton::Type::Widget:
            emit widgetToolRequested();
            return;
        case RingButton::Type::Text:
            emit textToolRequested();
            return;
        case RingButton::Type::Table:
            emit tableToolRequested();
            return;
        case RingButton::Type::Menu:
            emit menuRequested();
            return;
    }
    rebuildRing();
    update();
}

// 内环点击：切换类目/焦点（撤销重做即时发信号）
void BoardDisk::innerClicked(const InnerButton& button) {
    using Kind = PenSettingPanel::PenKind;
    switch (button.type) {
        case InnerButton::Type::Pen:
            focusKind_ = InnerButton::Type::Pen;
            if (penKind_ != Kind::Normal || tool_ != BoardView::Tool::Pen) {
                penKind_ = Kind::Normal;
                emit penParamChanged(Kind::Normal, normalColor_, normalWidth_);
            }
            break;
        case InnerButton::Type::Highlighter:
            focusKind_ = InnerButton::Type::Highlighter;
            if (penKind_ != Kind::Highlighter || tool_ != BoardView::Tool::Pen) {
                penKind_ = Kind::Highlighter;
                emit penParamChanged(Kind::Highlighter, hlColor_, hlWidth_);
            }
            break;
        case InnerButton::Type::Eraser:
            focusKind_ = InnerButton::Type::Eraser;
            if (tool_ != BoardView::Tool::Eraser)
                emit toolSelected(BoardView::Tool::Eraser);
            break;
        case InnerButton::Type::Select:
            focusKind_ = InnerButton::Type::Select;
            if (tool_ != BoardView::Tool::Select)
                emit toolSelected(BoardView::Tool::Select);
            break;
        case InnerButton::Type::Mouse:
            focusKind_ = InnerButton::Type::Mouse;
            if (tool_ != BoardView::Tool::Mouse)
                emit toolSelected(BoardView::Tool::Mouse);
            break;
        case InnerButton::Type::Undo:
            emit undoRequested();
            return;  // 不改焦点
        case InnerButton::Type::Redo:
            emit redoRequested();
            return;
        case InnerButton::Type::More:
            focusKind_ = InnerButton::Type::More;
            break;  // 只切外环，不改当前工具
    }
    refreshBallIcon();
    rebuildRing();
    update();
}

// ---------- 色轮弹窗（Qt::Popup 容器内嵌 ColorPickerPanel） ----------

void BoardDisk::showWheelPopup(const QRect& anchor) {
    if (!wheelPopup_) {
        wheelPopup_ = new QWidget(this, Qt::Popup | Qt::FramelessWindowHint);
        wheelPopup_->setAttribute(Qt::WA_TranslucentBackground);
        auto* layout = new QVBoxLayout(wheelPopup_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        wheelPicker_ = new ColorPickerPanel(wheelPopup_);
        layout->addWidget(wheelPicker_);
        connect(wheelPicker_, &ColorPickerPanel::colorChosen, this, [this](const QColor& color) {
            uint32_t& colorRef = (penKind_ == PenSettingPanel::PenKind::Highlighter)
                                     ? hlColor_
                                     : normalColor_;
            const int width = (penKind_ == PenSettingPanel::PenKind::Highlighter) ? hlWidth_
                                                                                  : normalWidth_;
            colorRef = qColorToColorRef(color);
            emit penParamChanged(penKind_, colorRef, width);
            rebuildRing();
            update();
        });
    }

    const uint32_t current = (penKind_ == PenSettingPanel::PenKind::Highlighter) ? hlColor_
                                                                                : normalColor_;
    wheelPicker_->setColor(colorToQColor(current & 0x00FFFFFFu));
    wheelPopup_->adjustSize();

    // 定位：锚点右侧展开（空间不足换左侧），防出屏
    const QPoint anchorGlobal = mapToGlobal(anchor.topLeft());
    QPoint pos = anchorGlobal + QPoint(anchor.width() + 8, 0);
    QScreen* screen = QGuiApplication::screenAt(anchorGlobal);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        if (pos.x() + wheelPopup_->width() > available.right() - 8)
            pos.setX(anchorGlobal.x() - wheelPopup_->width() - 8);
        pos.setY(qBound(available.top() + 8, pos.y(),
                        available.bottom() - wheelPopup_->height() - 8));
        pos.setX(qMax(available.left() + 8, pos.x()));
    }
    wheelPopup_->move(pos);
    wheelPopup_->show();
}
