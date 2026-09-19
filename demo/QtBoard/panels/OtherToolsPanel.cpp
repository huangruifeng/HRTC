#include "OtherToolsPanel.h"

#include <QEvent>
#include <QFont>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>

#include "BoardIcons.h"

namespace {

constexpr int kPanelW = 344;
constexpr int kPanelH = 84;

// 条目几何（5 个等宽块，水平居中）；每块：32px 图标 + 11px 文字
constexpr int kItemW = 56;
constexpr int kItemH = 64;
constexpr int kItemGap = 6;
constexpr int kItemY = 10;
constexpr int kIconSize = 32;

const QColor kMenuBackground(0x23, 0x27, 0x2A, 0xCC);  // 同 WidgetSetupPanel
const QColor kMenuBorder(0x33, 0x63, 0x6C, 0x73);
const QColor kAccent(0xFF, 0x7D, 0x00);

}  // namespace

OtherToolsPanel::OtherToolsPanel(QWidget* parent) : QWidget(parent) {
    // Windows 下 WA_TranslucentBackground 需要 FramelessWindowHint 才生效，
    // 否则圆角外未绘制区域会被渲染成黑块
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(kPanelW, kPanelH);
    setMouseTracking(true);
}

void OtherToolsPanel::setCurrentIndex(int index) {
    currentIndex_ = (index >= 0 && index < ItemCount) ? index : -1;
    update();
}

QRect OtherToolsPanel::itemRect(int index) const {
    const int totalW = kItemW * ItemCount + kItemGap * (ItemCount - 1);
    const int x0 = (kPanelW - totalW) / 2;
    return QRect(x0 + index * (kItemW + kItemGap), kItemY, kItemW, kItemH);
}

void OtherToolsPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // 圆角背景 + 边框（同 WidgetSetupPanel）
    p.setPen(QPen(kMenuBorder, 1.0));
    p.setBrush(kMenuBackground);
    p.drawRoundedRect(QRectF(0.0, 0.0, kPanelW, kPanelH).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);

    const QString names[ItemCount] = { QStringLiteral("图形"), QStringLiteral("导图"),
                                       QStringLiteral("表格"), QStringLiteral("文字"),
                                       QStringLiteral("小工具") };
    const BoardIcons::Glyph glyphs[ItemCount] = {
        BoardIcons::Glyph::Shape, BoardIcons::Glyph::MindMap, BoardIcons::Glyph::Table,
        BoardIcons::Glyph::Text, BoardIcons::Glyph::Widget
    };
    for (int i = 0; i < ItemCount; ++i) {
        const QRect r = itemRect(i);
        const bool selected = (i == currentIndex_);
        const bool hovered = (i == hoverIndex_);
        // 当前工具条目：橙色描边 + 浅橙填充；悬停条目：浅白圆角高亮
        if (selected) {
            QColor fill = kAccent;
            fill.setAlpha(70);
            p.setPen(QPen(kAccent, 1.0));
            p.setBrush(fill);
            p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);
        } else if (hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 28));
            p.drawRoundedRect(QRectF(r), 8.0, 8.0);
        }

        // 图标：悬停/当前条目转橙色
        const QPixmap icon = BoardIcons::pixmap(glyphs[i], selected || hovered);
        p.drawPixmap(QRect(r.x() + (kItemW - kIconSize) / 2, r.y() + 6, kIconSize, kIconSize), icon);

        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(selected ? QColor(0xFF, 0xB0, 0x60)
                          : (hovered ? QColor(0xE8, 0xE8, 0xE8) : QColor(0xB8, 0xB8, 0xB8)));
        p.drawText(QRect(r.x(), r.y() + 44, kItemW, 16), Qt::AlignCenter, names[i]);
    }
}

void OtherToolsPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    hoverIndex_ = -1;
    update();
    emit closed();
}

void OtherToolsPanel::mouseMoveEvent(QMouseEvent* event) {
    int hoverIndex = -1;
    for (int i = 0; i < ItemCount; ++i) {
        if (itemRect(i).contains(event->pos())) {
            hoverIndex = i;
            break;
        }
    }
    if (hoverIndex != hoverIndex_) {
        hoverIndex_ = hoverIndex;
        update();
    }
}

// 点击任一条目：立即生效并关闭（图形/表格/文字/小工具的参数面板由 MainWindow 接力弹出）
void OtherToolsPanel::mousePressEvent(QMouseEvent* event) {
    for (int i = 0; i < ItemCount; ++i) {
        if (!itemRect(i).contains(event->pos()))
            continue;
        currentIndex_ = i;
        update();
        switch (i) {
            case ItemShape:
                emit shapeRequested();
                break;
            case ItemMindMap:
                emit mindMapRequested();
                break;
            case ItemTable:
                emit tableRequested();
                break;
            case ItemText:
                emit textRequested();
                break;
            case ItemWidget:
                emit widgetRequested();
                break;
            default:
                break;
        }
        hide();
        return;
    }
    // 点击面板外：交给 Popup 基类逻辑关闭（否则 popup 会吃掉外部点击且不关闭，
    // 导致画布点击/工具切换全部失效）
    QWidget::mousePressEvent(event);
}

void OtherToolsPanel::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    if (hoverIndex_ != -1) {
        hoverIndex_ = -1;
        update();
    }
}
