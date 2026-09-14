#include "PenSettingPanel.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QVBoxLayout>

#include "BoardIcons.h"
#include "BoardUtil.h"
#include "ColorPickerPanel.h"

namespace {

// 面板收起/展开宽度（参考 PenSettingContent：350 + 间距 6 + 色盘 222）
constexpr int kPanelWidth = 350;
constexpr int kExpandedWidth = 350 + 6 + 222;

// 色板（与参考 PenSettingContent 一致）
const char* const kPalette[] = {
    "#000000", "#FFFFFF", "#FF2C1B", "#FF8B00", "#331EB5", "#306ED9",
    "#306C00", "#66D552", "#FF1ED0", "#4FA0B7", "#8B7E6E",
};

// 选中高亮背景 #32038DFF
const QColor kSelectFill(3, 141, 255, 50);

}  // namespace

// ---------- 预览条 ----------

class PenSettingPanel::PreviewBar : public QWidget {
public:
    explicit PreviewBar(QWidget* parent) : QWidget(parent) {}

    void setState(const QColor& color, int penWidth, bool highlighter) {
        color_ = color;
        penWidth_ = penWidth;
        highlighter_ = highlighter;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // 背景 = 当前笔色（近白色时描边，避免与白色画布融合）；
        // 荧光笔：背景用 30% 透明色块模拟叠加效果
        const bool lightBackground = color_.lightnessF() > 0.82;
        p.setPen(lightBackground ? QPen(QColor(0xB2, 0x97, 0x97, 0x97), 1.0) : Qt::NoPen);
        QColor background = color_;
        if (highlighter_)
            background.setAlpha(77);  // 0x4D ≈ 30%，与 MainWindow 编码一致
        p.setBrush(background);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 18.0, 18.0);

        // 笔迹示例：深色背景白线 / 浅色背景深灰线，线宽即当前笔宽；
        // 荧光笔：同色半透明粗线（12/24/48 → 4/8/16），模拟荧光笔迹
        QPainterPath wave;
        wave.moveTo(24.0, 29.0);
        wave.cubicTo(90.0, 4.0, 160.0, 42.0, width() - 24.0, 15.0);
        QColor strokeColor = lightBackground ? QColor(0x55, 0x55, 0x55) : QColor(0xFF, 0xFF, 0xFF);
        qreal lineWidth = qBound(2.0, static_cast<double>(penWidth_), 16.0);
        if (highlighter_) {
            strokeColor = color_;
            strokeColor.setAlpha(0xCC);
            lineWidth = qBound(2.0, static_cast<double>(penWidth_) / 3.0, 16.0);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(strokeColor, lineWidth, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(wave);

        // 粗细文字：细 / 中 / 粗（荧光笔宽度换算到常规刻度再判断）
        const int base = highlighter_ ? penWidth_ / 4 : penWidth_;
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(QColor(0x99, 0x99, 0x99));
        p.drawText(QRect(35, 0, 80, height()), Qt::AlignVCenter | Qt::AlignLeft,
                   base <= 3 ? QStringLiteral("细")
                             : (base <= 6 ? QStringLiteral("中") : QStringLiteral("粗")));
    }

private:
    QColor color_ = Qt::white;
    int penWidth_ = 3;
    bool highlighter_ = false;
};

// ---------- 色板圆点按钮 ----------

class PenSettingPanel::ColorDotButton : public QAbstractButton {
public:
    ColorDotButton(const QColor& color, bool isWheel, QWidget* parent)
        : QAbstractButton(parent), color_(color), isWheel_(isWheel) {
        setFixedSize(54, 54);
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
        const QPointF center(27.0, 27.0);

        if (isChecked()) {
            p.setPen(Qt::NoPen);
            p.setBrush(kSelectFill);
            p.drawEllipse(center, 27.0, 27.0);
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

// ---------- 粗细档位按钮 ----------

class PenSettingPanel::ThicknessButton : public QAbstractButton {
public:
    ThicknessButton(int index, QWidget* parent)
        : QAbstractButton(parent), index_(index) {
        setFixedSize(48, 48);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }

    int index() const { return index_; }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        if (isChecked()) {
            p.setPen(Qt::NoPen);
            p.setBrush(kSelectFill);
            p.drawEllipse(QRectF(0.0, 0.0, 48.0, 48.0));
        }

        // S 形笔迹示例，视觉线宽按档位 index（0/1/2 → 2.5/4.5/7.5）
        QPainterPath sample;
        sample.moveTo(4.0, 11.0);
        sample.cubicTo(4.0, 5.0, 10.0, 5.0, 12.0, 10.0);
        sample.cubicTo(14.0, 15.0, 20.0, 15.0, 20.0, 9.0);
        static const qreal kLineWidths[3] = {2.5, 4.5, 7.5};
        const qreal lineWidth = kLineWidths[qBound(0, index_, 2)];
        p.translate(12.0, 13.0);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0x99, 0x99, 0x99), lineWidth,
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(sample);
    }

private:
    int index_ = 0;
};

// ---------- 笔类型切换按钮（常规笔/手指 | 荧光笔） ----------

class PenSettingPanel::TypeSwitchButton : public QAbstractButton {
public:
    TypeSwitchButton(PenKind kind, QWidget* parent)
        : QAbstractButton(parent), kind_(kind) {
        setFixedSize(140, 40);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }

    PenKind kind() const { return kind_; }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        if (isChecked()) {
            p.setPen(Qt::NoPen);
            p.setBrush(kSelectFill);
            p.drawRoundedRect(QRectF(0.0, 0.0, width(), height()), 8.0, 8.0);
        }

        // 图标 30x30：常规笔沿用 pen_type.png；荧光笔用自绘图标（灰/橙两态）
        const bool highlighter = (kind_ == PenKind::Highlighter);
        const QPixmap icon = highlighter
                                 ? BoardIcons::pixmap(BoardIcons::Glyph::Highlighter, isChecked())
                                 : QPixmap(QStringLiteral(":/icons/pen_type.png"))
                                       .scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        const QString text = highlighter ? QStringLiteral("荧光笔") : QStringLiteral("常规笔/手指");
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int textWidth = fm.horizontalAdvance(text);

        // 图标 + 文字整体水平居中，选中态橙色文字 / 未选中灰色
        constexpr int kIconSize = 30;
        constexpr int kGap = 8;
        const int startX = (width() - (kIconSize + kGap + textWidth)) / 2;
        p.drawPixmap(QRect(startX, (height() - kIconSize) / 2, kIconSize, kIconSize), icon);
        p.setPen(isChecked() ? QColor(0xFF, 0x7D, 0x00) : QColor(0x99, 0x99, 0x99));
        p.drawText(QRect(startX + kIconSize + kGap, 0, textWidth + 4, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, text);
    }

private:
    PenKind kind_ = PenKind::Normal;
};

PenSettingPanel::PenSettingPanel(QWidget* parent) : QWidget(parent) {
    // Windows 下 WA_TranslucentBackground 需要 FramelessWindowHint 才生效，
    // 否则圆角外未绘制区域会被渲染成黑块
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(kPanelWidth);

    // 根布局：左主列（350）+ 6px 间距 + 内联颜色选择盘（底部对齐，默认隐藏）
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* leftColumn = new QWidget(this);
    leftColumn->setFixedWidth(kPanelWidth);
    auto* rootLeft = new QVBoxLayout(leftColumn);
    rootLeft->setContentsMargins(0, 0, 0, 0);
    rootLeft->setSpacing(0);

    // 顶部：笔类型切换（常规笔/手指 | 荧光笔，互斥；切换后各自记忆颜色与档位）
    auto* header = new QHBoxLayout;
    header->setContentsMargins(30, 16, 30, 16);
    header->setSpacing(10);
    normalTypeButton_ = new TypeSwitchButton(PenKind::Normal, leftColumn);
    highlighterTypeButton_ = new TypeSwitchButton(PenKind::Highlighter, leftColumn);
    header->addWidget(normalTypeButton_);
    header->addWidget(highlighterTypeButton_);
    rootLeft->addLayout(header);
    connect(normalTypeButton_, &QAbstractButton::clicked, this,
            [this]() { onTypeClicked(PenKind::Normal); });
    connect(highlighterTypeButton_, &QAbstractButton::clicked, this,
            [this]() { onTypeClicked(PenKind::Highlighter); });

    // 预览条
    auto* previewRow = new QHBoxLayout;
    previewRow->setContentsMargins(30, 0, 30, 0);
    preview_ = new PreviewBar(leftColumn);
    preview_->setFixedHeight(44);
    previewRow->addWidget(preview_);
    rootLeft->addLayout(previewRow);

    // 内容：左列粗细 + 右列色板
    auto* content = new QHBoxLayout;
    content->setContentsMargins(30, 20, 30, 20);
    content->setSpacing(5);

    thicknessGroup_ = new QButtonGroup(this);
    thicknessGroup_->setExclusive(true);
    auto* thicknessColumn = new QVBoxLayout;
    thicknessColumn->setContentsMargins(0, 2, 0, 2);
    thicknessColumn->setSpacing(0);
    for (int index = 0; index < 3; ++index) {
        auto* button = new ThicknessButton(index, leftColumn);
        thicknessGroup_->addButton(button);
        thicknessColumn->addWidget(button, 0, Qt::AlignTop);
        connect(button, &QAbstractButton::clicked, this, [this, index]() {
            widthRef() = widthFor(kind_, index);
            updateChecks();
            emit penChanged(kind_, colorRef(), widthRef());
        });
    }
    thicknessColumn->addStretch();
    content->addLayout(thicknessColumn);

    colorGroup_ = new QButtonGroup(this);
    colorGroup_->setExclusive(true);
    auto* colorGrid = new QGridLayout;
    colorGrid->setContentsMargins(0, 0, 0, 0);
    colorGrid->setSpacing(0);
    for (int i = 0; i < 11; ++i) {
        const QColor color(kPalette[i]);
        auto* button = new ColorDotButton(color, false, leftColumn);
        colorGroup_->addButton(button);
        colorGrid->addWidget(button, i / 4, i % 4);
        connect(button, &QAbstractButton::clicked, this, [this, color]() {
            colorRef() = qColorToColorRef(color);
            collapsePicker();  // 参考：点击色板取消色盘选中并关闭
            updateChecks();
            emit penChanged(kind_, colorRef(), widthRef());
        });
    }
    wheelButton_ = new ColorDotButton(QColor(), true, leftColumn);
    colorGroup_->addButton(wheelButton_);
    colorGrid->addWidget(wheelButton_, 2, 3);
    connect(wheelButton_, &QAbstractButton::clicked, this, &PenSettingPanel::onWheelClicked);
    content->addLayout(colorGrid);
    content->addStretch();

    rootLeft->addLayout(content);
    root->addWidget(leftColumn);

    // 内联颜色选择盘（展开时可见，底部对齐）
    picker_ = new ColorPickerPanel(this);
    connect(picker_, &ColorPickerPanel::colorChosen, this, &PenSettingPanel::onPickerColorChosen);
    picker_->setVisible(false);
    root->addWidget(picker_, 0, Qt::AlignBottom);

    updateChecks();
}

void PenSettingPanel::setCurrent(PenKind kind, uint32_t color, int width) {
    kind_ = kind;
    colorRef() = color;
    widthRef() = width;
    if (pickerVisible_)
        picker_->setColor(colorToQColor(color));
    updateChecks();
}

// 当前类型的颜色 / 宽度引用（两类笔各自记忆）
uint32_t& PenSettingPanel::colorRef() {
    return kind_ == PenKind::Highlighter ? hlColor_ : color_;
}

int& PenSettingPanel::widthRef() {
    return kind_ == PenKind::Highlighter ? hlWidth_ : width_;
}

// 档位 index → 实际宽度：常规笔 3/6/12，荧光笔 12/24/48
int PenSettingPanel::widthFor(PenKind kind, int index) {
    return (kind == PenKind::Highlighter) ? (12 << index) : (3 << index);
}

void PenSettingPanel::updateChecks() {
    preview_->setState(colorToQColor(colorRef()), widthRef(), kind_ == PenKind::Highlighter);

    // 类型按钮选中态（互斥）
    normalTypeButton_->setChecked(kind_ == PenKind::Normal);
    highlighterTypeButton_->setChecked(kind_ == PenKind::Highlighter);

    // 色板：命中当前色则选中对应圆点，否则选中色轮（面板内只存纯 RGB）
    bool colorMatched = false;
    for (QAbstractButton* button : colorGroup_->buttons()) {
        auto* dot = static_cast<ColorDotButton*>(button);
        if (dot->isWheel())
            continue;
        const bool hit = qColorToColorRef(dot->dotColor()) == colorRef();
        dot->setChecked(hit);
        colorMatched = colorMatched || hit;
    }
    // 色轮按钮：展开中或当前色不在色板时点亮
    wheelButton_->setChecked(pickerVisible_ || !colorMatched);

    // 粗细档位：按当前类型宽度表匹配
    for (QAbstractButton* button : thicknessGroup_->buttons()) {
        auto* thickness = static_cast<ThicknessButton*>(button);
        thickness->setChecked(widthFor(kind_, thickness->index()) == widthRef());
    }
}

// 色轮格子：展开内联颜色选择盘（已展开时不再响应，同参考单选按钮语义）
void PenSettingPanel::onWheelClicked() {
    if (pickerVisible_)
        return;
    picker_->setColor(colorToQColor(colorRef()));  // 同步当前类型颜色到色轮/明度滑条
    pickerVisible_ = true;
    picker_->setVisible(true);
    setFixedWidth(kExpandedWidth);
    adjustSize();
    updateChecks();
    update();
}

// 取色完成：更新当前类型的颜色并保持展开（参考 SelectedBrushCompleted 不收起色盘）
void PenSettingPanel::onPickerColorChosen(const QColor& color) {
    colorRef() = qColorToColorRef(color);
    updateChecks();
    emit penChanged(kind_, colorRef(), widthRef());
}

// 切换笔类型：两类各自记忆颜色与档位；点击已选中类型仅恢复选中态
void PenSettingPanel::onTypeClicked(PenKind kind) {
    if (kind_ == kind) {
        updateChecks();  // checkable 按钮点击会自行切换选中态，此处按当前类型恢复
        return;
    }
    kind_ = kind;
    if (pickerVisible_)  // 展开中的色盘同步到新类型颜色
        picker_->setColor(colorToQColor(colorRef()));
    updateChecks();
    emit penChanged(kind_, colorRef(), widthRef());
}

// 收起颜色选择盘：恢复 350 宽（面板隐藏时同步收起，参考 IsVisibleChanged）
void PenSettingPanel::collapsePicker() {
    if (!pickerVisible_)
        return;
    pickerVisible_ = false;
    picker_->setVisible(false);
    setFixedWidth(kPanelWidth);
    adjustSize();
    update();
}

void PenSettingPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    collapsePicker();
    updateChecks();  // 重新打开时色轮按钮按当前色决定选中态
    emit closed();
}

void PenSettingPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x23, 0x27, 0x2A, 0xCC));  // #CC23272A
    // 仅绘制左侧主列圆角背景（右侧色盘自带圆角背景）
    p.drawRoundedRect(QRectF(0.0, 0.0, kPanelWidth, height()), 8.0, 8.0);
}
