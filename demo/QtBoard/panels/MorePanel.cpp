#include "MorePanel.h"

#include <QFont>
#include <QHideEvent>
#include <QIcon>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "BoardIcons.h"

namespace {

constexpr int kPanelW = 256;      // 与 SettingsPanel 同宽
constexpr int kTitleHeight = 34;  // 标题区："更多" 20px 文字
constexpr int kButtonH = 34;
constexpr int kButtonSpacing = 8;
constexpr int kIconSize = 20;

// 颜色常量（同 SettingsPanel）
const QColor kMenuBackground(0x23, 0x27, 0x2A, 0xCC);  // #CC23272A
const QColor kMenuBorder(0x33, 0x63, 0x6C, 0x73);      // #33636C73

// 条目按钮：左侧图标 + 左对齐文字（QSS 底色必须不透明——分层弹窗内透明区域会擦穿）
const char* const kItemQss =
    "QPushButton { background: #32383B; border: 1px solid #3F4448; border-radius: 4px;"
    "              color: #CCCCCC; font-size: 13px; text-align: left; padding-left: 12px; }"
    "QPushButton:hover { background: #3A4045; }"
    "QPushButton:pressed { background: #2A2F32; }";

}  // namespace

MorePanel::MorePanel(QWidget* parent) : QWidget(parent) {
    // Windows 下 WA_TranslucentBackground 需要 FramelessWindowHint 才生效，
    // 否则圆角外未绘制区域会被渲染成黑块
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(kPanelW, kTitleHeight + 5 * kButtonH + 4 * kButtonSpacing + 12);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, kTitleHeight, 12, 12);
    root->setSpacing(kButtonSpacing);

    const auto addItem = [this, root](QPushButton*& slot, const QString& text,
                                      BoardIcons::Glyph glyph) {
        slot = new QPushButton(text, this);
        slot->setFixedHeight(kButtonH);
        slot->setCursor(Qt::PointingHandCursor);
        slot->setFocusPolicy(Qt::NoFocus);
        slot->setStyleSheet(QString::fromLatin1(kItemQss));
        slot->setIcon(QIcon(BoardIcons::pixmap(glyph, false)));
        slot->setIconSize(QSize(kIconSize, kIconSize));
        root->addWidget(slot);
    };
    addItem(interactButton_, QStringLiteral("互动白板"), BoardIcons::Glyph::Interact);
    addItem(saveButton_, QStringLiteral("保存白板"), BoardIcons::Glyph::Save);
    addItem(openButton_, QStringLiteral("打开白板"), BoardIcons::Glyph::Open);
    addItem(settingsButton_, QStringLiteral("设置"), BoardIcons::Glyph::Settings);
    addItem(exitButton_, QStringLiteral("退出程序"), BoardIcons::Glyph::Exit);

    connect(interactButton_, &QPushButton::clicked, this, &MorePanel::interactRequested);
    connect(saveButton_, &QPushButton::clicked, this, &MorePanel::saveRequested);
    connect(openButton_, &QPushButton::clicked, this, &MorePanel::openRequested);
    connect(settingsButton_, &QPushButton::clicked, this, &MorePanel::settingsRequested);
    connect(exitButton_, &QPushButton::clicked, this, &MorePanel::exitRequested);
}

void MorePanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // 圆角背景 + 边框（同 SettingsPanel / SlideManagerPanel 风格）
    p.setPen(QPen(kMenuBorder, 1.0));
    p.setBrush(kMenuBackground);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);

    // 标题"更多"（20px #EEEEEE）
    QFont f = font();
    f.setPixelSize(20);
    p.setFont(f);
    p.setPen(QColor(0xEE, 0xEE, 0xEE));
    p.drawText(QRect(16, 4, width() - 32, 26), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("更多"));
}

void MorePanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    emit closed();
}
