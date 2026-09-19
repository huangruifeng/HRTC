#include "TextSetupPanel.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QEvent>
#include <QFont>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QWheelEvent>

#include "BoardUtil.h"
#include "ColorPickerPanel.h"

namespace {

constexpr int kPanelW = 350;
constexpr int kExpandedWidth = 350 + 6 + 222;  // 展开内联取色盘（同笔面板）
constexpr int kPanelH = 232;                   // 与 ColorPickerPanel 等高，展开后自然对齐

// 左侧垂直滚轮几何
constexpr int kRollerX = 20;
constexpr int kRollerY = 44;
constexpr int kRollerW = 82;
constexpr int kSlotH = 34;
constexpr int kVisibleSlots = 5;
constexpr int kCenterSlot = 2;  // 中央槽位恒为当前选中档

// 右侧色格（4x3 = 11 色板 + 色轮）几何
constexpr int kGridX = 114;  // 20 + 82 + 12
constexpr int kGridY = 44;
constexpr int kDotSize = 54;

// 色板（与笔面板一致）
const char* const kPalette[] = {
    "#000000", "#FFFFFF", "#FF2C1B", "#FF8B00", "#331EB5", "#306ED9",
    "#306C00", "#66D552", "#FF1ED0", "#4FA0B7", "#8B7E6E",
};

const QColor kMenuBackground(0x23, 0x27, 0x2A, 0xCC);  // 同 MorePanel
const QColor kMenuBorder(0x33, 0x63, 0x6C, 0x73);
const QColor kAccent(0xFF, 0x7D, 0x00);
const QColor kSelectFill(3, 141, 255, 50);  // 色格选中高亮（同笔面板）

}  // namespace

// ---------- 色板圆点按钮 ----------

class TextSetupPanel::ColorDotButton : public QAbstractButton {
public:
    ColorDotButton(const QColor& color, bool isWheel, QWidget* parent)
        : QAbstractButton(parent), color_(color), isWheel_(isWheel) {
        setFixedSize(kDotSize, kDotSize);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }

    QColor dotColor() const { return color_; }
    bool isWheel() const { return isWheel_; }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QPointF center(kDotSize / 2.0, kDotSize / 2.0);

        if (isChecked()) {
            p.setPen(Qt::NoPen);
            p.setBrush(kSelectFill);
            p.drawEllipse(center, kDotSize / 2.0, kDotSize / 2.0);
        }

        const QRectF dot(center.x() - 20.0, center.y() - 20.0, 40.0, 40.0);
        if (isWheel_) {
            const QPixmap wheel(QStringLiteral(":/icons/color_wheel.png"));
            p.drawPixmap(dot, wheel, QRectF(wheel.rect()));
            return;
        }
        const bool lightDot = color_.lightnessF() > 0.9;
        p.setPen(lightDot ? QPen(QColor(0xB2, 0x97, 0x97, 0x97), 1.0) : Qt::NoPen);
        p.setBrush(color_);
        p.drawEllipse(dot);
    }

private:
    QColor color_;
    bool isWheel_ = false;
};

TextSetupPanel::TextSetupPanel(QWidget* parent) : QWidget(parent) {
    // Windows 下 WA_TranslucentBackground 需要 FramelessWindowHint 才生效，
    // 否则圆角外未绘制区域会被渲染成黑块
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(kPanelW, kPanelH);
    setMouseTracking(true);

    // 色格：手动几何直接挂在面板上（滚轮区由 paintEvent 绘制，不设中间容器
    // 以免遮挡悬停/点击事件）
    colorGroup_ = new QButtonGroup(this);
    colorGroup_->setExclusive(true);
    for (int i = 0; i < 11; ++i) {
        const QColor color(kPalette[i]);
        auto* button = new ColorDotButton(color, false, this);
        colorGroup_->addButton(button);
        button->setGeometry(kGridX + (i % 4) * kDotSize, kGridY + (i / 4) * kDotSize,
                            kDotSize, kDotSize);
        connect(button, &QAbstractButton::clicked, this, [this, color]() {
            color_ = qColorToColorRef(color);
            collapsePicker();  // 同笔面板：点击色板取消色盘并收起
            updateChecks();
            emit colorChanged(color_);
        });
    }
    wheelButton_ = new ColorDotButton(QColor(), true, this);
    colorGroup_->addButton(wheelButton_);
    wheelButton_->setGeometry(kGridX + 3 * kDotSize, kGridY + 2 * kDotSize, kDotSize,
                              kDotSize);
    connect(wheelButton_, &QAbstractButton::clicked, this, &TextSetupPanel::onWheelColorClicked);

    // 内联颜色选择盘（展开时可见，与面板等高对齐）
    picker_ = new ColorPickerPanel(this);
    connect(picker_, &ColorPickerPanel::colorChosen, this, &TextSetupPanel::onPickerColorChosen);
    picker_->setGeometry(kPanelW + 6, 0, 222, kPanelH);
    picker_->setVisible(false);

    updateChecks();
}

void TextSetupPanel::setCurrentSize(int size) {
    current_ = size;
    update();
}

void TextSetupPanel::setCurrentColor(uint32_t color) {
    color_ = color & 0x00FFFFFFu;  // 文字颜色恒为纯 RGB（不透明）
    if (pickerVisible_)
        picker_->setColor(colorToQColor(color_));
    updateChecks();
}

const QVector<int>& TextSetupPanel::sizeOptions() {
    static const QVector<int> kSizes = {16, 24, 32, 48, 64, 96};
    return kSizes;
}

// 当前字号在档位表中的最近下标（字号非档位值时取最接近档）
int TextSetupPanel::nearestIndex() const {
    const QVector<int>& sizes = sizeOptions();
    int best = 0;
    for (int i = 1; i < sizes.size(); ++i) {
        if (qAbs(sizes.at(i) - current_) < qAbs(sizes.at(best) - current_))
            best = i;
    }
    return best;
}

QRect TextSetupPanel::slotRect(int slot) const {
    return QRect(kRollerX, kRollerY + slot * kSlotH, kRollerW, kSlotH);
}

QRect TextSetupPanel::rollerRect() const {
    return QRect(kRollerX, kRollerY, kRollerW, kVisibleSlots * kSlotH);
}

// 命中档位下标：槽位内容在档位表范围内时才算有效（滚轮两端留空槽不响应）
int TextSetupPanel::optionIndexAt(const QPoint& pos) const {
    for (int slot = 0; slot < kVisibleSlots; ++slot) {
        if (!slotRect(slot).contains(pos))
            continue;
        const int index = nearestIndex() + (slot - kCenterSlot);
        return (index >= 0 && index < sizeOptions().size()) ? index : -1;
    }
    return -1;
}

void TextSetupPanel::selectOption(int optionIndex, bool close) {
    if (optionIndex < 0 || optionIndex >= sizeOptions().size())
        return;
    current_ = sizeOptions().at(optionIndex);
    update();
    emit fontSizeChanged(current_);
    if (close)
        hide();
}

void TextSetupPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // 圆角背景 + 边框（仅左主列；右侧取色盘自带圆角背景）
    p.setPen(QPen(kMenuBorder, 1.0));
    p.setBrush(kMenuBackground);
    p.drawRoundedRect(QRectF(0.0, 0.0, kPanelW, kPanelH).adjusted(0.5, 0.5, -0.5, -0.5), 8.0,
                      8.0);

    // 标题"文字"
    QFont f = font();
    f.setPixelSize(20);
    p.setFont(f);
    p.setPen(QColor(0xEE, 0xEE, 0xEE));
    p.drawText(QRect(20, 6, kPanelW - 40, 28), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("文字"));

    paintRoller(p);
}

// 垂直滚轮：中央槽恒为当前档（橙色高亮），上下相邻档渐小渐淡；出档位表留空
void TextSetupPanel::paintRoller(QPainter& p) {
    const int base = nearestIndex();
    const int count = sizeOptions().size();
    for (int slot = 0; slot < kVisibleSlots; ++slot) {
        const int index = base + (slot - kCenterSlot);
        if (index < 0 || index >= count)
            continue;  // 滚轮起点/终点：留空（iOS 滚轮风格）
        const QRect r = slotRect(slot);
        const bool isCenter = (slot == kCenterSlot);
        const bool hovered = (slot == hoverSlot_);
        if (isCenter || hovered) {
            QColor fill = kAccent;
            fill.setAlpha(isCenter ? 70 : 36);
            p.setPen(QPen(kAccent, 1.0));
            p.setBrush(fill);
            p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 6.0, 6.0);
        }

        // 距中央越远字越小越暗（16 / 13 / 11 px）
        const int distance = qAbs(slot - kCenterSlot);
        QFont f = font();
        f.setPixelSize(distance == 0 ? 16 : (distance == 1 ? 13 : 11));
        p.setFont(f);
        QColor textColor = distance == 0 ? QColor(0xFF, 0xB0, 0x60)
                           : distance == 1 ? QColor(0xB8, 0xB8, 0xB8)
                                           : QColor(0x82, 0x82, 0x82);
        if (hovered && !isCenter)
            textColor = QColor(0xE8, 0xE8, 0xE8);
        p.setPen(textColor);
        p.drawText(r, Qt::AlignCenter, QStringLiteral("%1 号").arg(sizeOptions().at(index)));
    }
}

void TextSetupPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    collapsePicker();
    hoverSlot_ = -1;
    updateChecks();  // 重新打开时色轮按钮按当前色决定选中态
    emit closed();
}

void TextSetupPanel::mouseMoveEvent(QMouseEvent* event) {
    int hover = -1;
    for (int slot = 0; slot < kVisibleSlots; ++slot) {
        if (!slotRect(slot).contains(event->pos()))
            continue;
        const int index = nearestIndex() + (slot - kCenterSlot);
        if (index >= 0 && index < sizeOptions().size())
            hover = slot;
        break;
    }
    if (hover != hoverSlot_) {
        hoverSlot_ = hover;
        update();
    }
}

void TextSetupPanel::mousePressEvent(QMouseEvent* event) {
    const int optionIndex = optionIndexAt(event->pos());
    if (optionIndex < 0) {
        // 标题/空槽处按下：交给 Popup 基类逻辑（面板外点击 → 关闭并透传；
        // 否则 popup 会吃掉外部点击且不关闭）。色格按钮为子控件自行处理，不走此处。
        QWidget::mousePressEvent(event);
        return;
    }
    selectOption(optionIndex, true);  // 点击档位：选中并关闭（选完继续输入）
}

// 滚轮滚动：仅滚轮区域内生效；切换相邻档并即时生效（不关闭面板，便于连续浏览）
void TextSetupPanel::wheelEvent(QWheelEvent* event) {
    if (event->angleDelta().y() == 0 || !rollerRect().contains(event->pos())) {
        QWidget::wheelEvent(event);
        return;
    }
    const int currentIndex = nearestIndex();
    const int index = currentIndex + (event->angleDelta().y() > 0 ? -1 : 1);  // 上滚 = 上一档
    if (index >= 0 && index < sizeOptions().size() && index != currentIndex)
        selectOption(index, false);
    event->accept();  // 到顶/到底：不循环（吸收事件避免误传）
}

void TextSetupPanel::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    if (hoverSlot_ != -1) {
        hoverSlot_ = -1;
        update();
    }
}

void TextSetupPanel::updateChecks() {
    // 色板：命中当前色则选中对应圆点，否则选中色轮（面板内只存纯 RGB）
    bool matched = false;
    for (QAbstractButton* button : colorGroup_->buttons()) {
        auto* dot = static_cast<ColorDotButton*>(button);
        if (dot->isWheel())
            continue;
        const bool hit = qColorToColorRef(dot->dotColor()) == color_;
        dot->setChecked(hit);
        matched = matched || hit;
    }
    // 色轮按钮：展开中或当前色不在色板时点亮
    wheelButton_->setChecked(pickerVisible_ || !matched);
}

// 色轮格子：展开内联颜色选择盘（已展开时不再响应，同笔面板单选按钮语义）
void TextSetupPanel::onWheelColorClicked() {
    if (pickerVisible_)
        return;
    picker_->setColor(colorToQColor(color_));  // 同步当前颜色到色轮/明度滑条
    pickerVisible_ = true;
    picker_->setVisible(true);
    setFixedWidth(kExpandedWidth);
    updateChecks();
    update();
}

// 取色完成：更新颜色并保持展开（同笔面板）
void TextSetupPanel::onPickerColorChosen(const QColor& color) {
    color_ = qColorToColorRef(color);
    updateChecks();
    emit colorChanged(color_);
}

// 收起颜色选择盘：恢复 350 宽（面板隐藏时同步收起）
void TextSetupPanel::collapsePicker() {
    if (!pickerVisible_)
        return;
    pickerVisible_ = false;
    picker_->setVisible(false);
    setFixedWidth(kPanelW);
    update();
}
