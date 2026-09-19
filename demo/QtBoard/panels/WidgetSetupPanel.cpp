#include "WidgetSetupPanel.h"

#include <QEvent>
#include <QFont>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>

namespace {

constexpr int kPanelW = 480;
constexpr int kPanelH = 84;

// Tab 几何（7 个等宽圆角块，水平居中）；下行为提示文案
constexpr int kTabY = 12;
constexpr int kTabW = 60;
constexpr int kTabH = 36;
constexpr int kTabGap = 6;
constexpr int kTabCount = 7;
constexpr int kHintY = 56;

const QColor kMenuBackground(0x23, 0x27, 0x2A, 0xCC);  // 同 TextSetupPanel
const QColor kMenuBorder(0x33, 0x63, 0x6C, 0x73);
const QColor kAccent(0xFF, 0x7D, 0x00);

}  // namespace

WidgetSetupPanel::WidgetSetupPanel(QWidget* parent) : QWidget(parent) {
    // Windows 下 WA_TranslucentBackground 需要 FramelessWindowHint 才生效，
    // 否则圆角外未绘制区域会被渲染成黑块
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(kPanelW, kPanelH);
    setMouseTracking(true);
}

void WidgetSetupPanel::setCurrent(int kind) {
    kind_ = qBound(0, kind, 6);
    update();
}

QRect WidgetSetupPanel::tabRect(int tab) const {
    const int totalW = kTabW * kTabCount + kTabGap * (kTabCount - 1);
    const int x0 = (kPanelW - totalW) / 2;
    return QRect(x0 + tab * (kTabW + kTabGap), kTabY, kTabW, kTabH);
}

void WidgetSetupPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // 圆角背景 + 边框（同文字面板）
    p.setPen(QPen(kMenuBorder, 1.0));
    p.setBrush(kMenuBackground);
    p.drawRoundedRect(QRectF(0.0, 0.0, kPanelW, kPanelH).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);

    paintTabs(p);
    paintHint(p);
}

// Tab：选中橙色描边 + 浅橙填充 + 橙字；未选灰字；悬停提亮
void WidgetSetupPanel::paintTabs(QPainter& p) {
    const QString names[7] = { QStringLiteral("秒表"), QStringLiteral("计时器"),
                               QStringLiteral("计算器"), QStringLiteral("算盘"),
                               QStringLiteral("骰子"), QStringLiteral("大转盘"),
                               QStringLiteral("点名器") };
    for (int tab = 0; tab < kTabCount; ++tab) {
        const QRect r = tabRect(tab);
        const bool selected = (tab == kind_);
        const bool hovered = (tab == hoverTab_);
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
        QFont f = font();
        f.setPixelSize(16);
        p.setFont(f);
        p.setPen(selected ? QColor(0xFF, 0xB0, 0x60)
                          : (hovered ? QColor(0xE8, 0xE8, 0xE8) : QColor(0xB8, 0xB8, 0xB8)));
        p.drawText(r, Qt::AlignCenter, names[tab]);
    }
}

// 底部提示：具体参数在卡片内设置
void WidgetSetupPanel::paintHint(QPainter& p) {
    QFont f = font();
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(QColor(0x8A, 0x8A, 0x8E));
    p.drawText(QRect(0, kHintY, kPanelW, 20), Qt::AlignCenter,
               QStringLiteral("更多设置在卡片内操作"));
}

void WidgetSetupPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    hoverTab_ = -1;
    update();
    emit closed();
}

void WidgetSetupPanel::mouseMoveEvent(QMouseEvent* event) {
    int hoverTab = -1;
    for (int tab = 0; tab < kTabCount; ++tab) {
        if (tabRect(tab).contains(event->pos())) {
            hoverTab = tab;
            break;
        }
    }
    if (hoverTab != hoverTab_) {
        hoverTab_ = hoverTab;
        update();
    }
}

// 点击任一类型：立即生效并关闭（具体参数在卡片内设置）
void WidgetSetupPanel::mousePressEvent(QMouseEvent* event) {
    for (int tab = 0; tab < kTabCount; ++tab) {
        if (!tabRect(tab).contains(event->pos()))
            continue;
        kind_ = tab;
        update();
        emit widgetChosen(tab);
        hide();
        return;
    }
    // 点击面板外：交给 Popup 基类逻辑关闭（否则 popup 会吃掉外部点击且不关闭，
    // 导致画布点击/工具切换全部失效）
    QWidget::mousePressEvent(event);
}

void WidgetSetupPanel::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    if (hoverTab_ != -1) {
        hoverTab_ = -1;
        update();
    }
}
