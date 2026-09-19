#include "TableSetupPanel.h"

#include <QFont>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QShowEvent>

namespace {

constexpr int kMinSize = 2;
constexpr int kMaxSize = 8;
constexpr int kPanelW = 204;
constexpr int kTitleHeight = 32;
constexpr int kCell = 15;
constexpr int kGap = 3;
constexpr int kStep = kCell + kGap;
constexpr int kGridSize = kMaxSize * kCell + (kMaxSize - 1) * kGap;  // 141
constexpr int kGridX = (kPanelW - kGridSize) / 2;                    // 31
constexpr int kGridY = kTitleHeight + 4;
constexpr int kTextHeight = 24;

const QColor kMenuBackground(0x23, 0x27, 0x2A, 0xCC);  // 同 MorePanel
const QColor kMenuBorder(0x33, 0x63, 0x6C, 0x73);
const QColor kCellBg(0x32, 0x38, 0x3B);
const QColor kCellBorder(0x4A, 0x50, 0x54);
const QColor kAccent(0xFF, 0x7D, 0x00);

}  // namespace

TableSetupPanel::TableSetupPanel(QWidget* parent) : QWidget(parent) {
    // Windows 下 WA_TranslucentBackground 需要 FramelessWindowHint 才生效，
    // 否则圆角外未绘制区域会被渲染成黑块
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(kPanelW, kGridY + kGridSize + kTextHeight);
    setMouseTracking(true);
}

void TableSetupPanel::setCurrentSize(int rows, int cols) {
    rows_ = qBound(kMinSize, rows, kMaxSize);
    cols_ = qBound(kMinSize, cols, kMaxSize);
    hoverRows_ = rows_;
    hoverCols_ = cols_;
    update();
}

QRect TableSetupPanel::gridRect() const {
    return QRect(kGridX, kGridY, kGridSize, kGridSize);
}

void TableSetupPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    hoverRows_ = rows_;  // 展开时预选 = 当前设置
    hoverCols_ = cols_;
}

void TableSetupPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // 圆角背景 + 边框（同 MorePanel 风格）
    p.setPen(QPen(kMenuBorder, 1.0));
    p.setBrush(kMenuBackground);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);

    // 标题"表格"
    QFont f = font();
    f.setPixelSize(20);
    p.setFont(f);
    p.setPen(QColor(0xEE, 0xEE, 0xEE));
    p.drawText(QRect(16, 4, width() - 32, 26), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("表格"));

    // 8x8 格子：悬停预选区域橙色高亮
    for (int r = 0; r < kMaxSize; ++r) {
        for (int c = 0; c < kMaxSize; ++c) {
            const QRect cell(kGridX + c * kStep, kGridY + r * kStep, kCell, kCell);
            const bool inSel = (r < hoverRows_ && c < hoverCols_);
            QColor fill = inSel ? kAccent : kCellBg;
            if (inSel)
                fill.setAlpha(70);
            p.setPen(QPen(inSel ? kAccent : kCellBorder, 1.0));
            p.setBrush(fill);
            p.drawRect(cell);
        }
    }

    // 底部尺寸文字（悬停预览）
    f.setPixelSize(13);
    p.setFont(f);
    p.setPen(QColor(0xCC, 0xCC, 0xCC));
    p.drawText(QRect(0, kGridY + kGridSize, width(), kTextHeight), Qt::AlignCenter,
               QStringLiteral("%1 行 × %2 列").arg(hoverRows_).arg(hoverCols_));
}

void TableSetupPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    emit closed();
}

void TableSetupPanel::mouseMoveEvent(QMouseEvent* event) {
    if (!gridRect().contains(event->pos()))
        return;
    const int col = qBound(kMinSize, (event->pos().x() - kGridX) / kStep + 1, kMaxSize);
    const int row = qBound(kMinSize, (event->pos().y() - kGridY) / kStep + 1, kMaxSize);
    if (row != hoverRows_ || col != hoverCols_) {
        hoverRows_ = row;
        hoverCols_ = col;
        update();
    }
}

void TableSetupPanel::mousePressEvent(QMouseEvent* event) {
    if (!gridRect().contains(event->pos())) {
        // 未命中网格：交给 Popup 基类逻辑（面板外点击 → 关闭并透传；
        // 否则 popup 会吃掉外部点击且不关闭）
        QWidget::mousePressEvent(event);
        return;
    }
    rows_ = hoverRows_;
    cols_ = hoverCols_;
    update();
    emit tableSizeChanged(rows_, cols_);
    hide();
}
