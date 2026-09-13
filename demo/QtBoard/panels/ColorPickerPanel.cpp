#include "ColorPickerPanel.h"

#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

// 面板总尺寸 222x232（参考 PlatteDiskSp Border：220x220 内容 + 1px 边框，底对齐）
constexpr int kPanelW = 222;
constexpr int kPanelH = 232;
constexpr int kContentX = 1;
constexpr int kContentY = 11;  // 内容区底对齐：(232 - 2) - 220 = 10 → 再 +1 边框
constexpr int kContentSize = 220;

// 色轮：逻辑 180x180，显示 ×0.7 → 126x126（参考 LayoutTransform ScaleX/Y=0.7）
constexpr int kLogicalWheel = 180;
constexpr double kLogicalRadius = 90.0;
constexpr int kDisplayWheel = 126;
constexpr double kScale = 0.7;

// 内容区内几何（参考 ColorPickerControl.xaml：色轮 Margin 0 30 0 10；
// 底部行 Margin 30 0 30 25：圆点 30x30，滑条 120x20 距圆点 10）
constexpr int kWheelTop = 30;
constexpr int kDotLeft = 30;
constexpr int kDotTop = 166;
constexpr int kDotSize = 30;
constexpr int kSliderLeft = 70;
constexpr int kSliderTop = 171;
constexpr int kSliderW = 120;
constexpr int kSliderH = 20;

// 颜色常量（参考 Main.xaml）
const QColor kMenuBackground(0x23, 0x27, 0x2A, 0xCC);  // #CC23272A
const QColor kMenuBorder(0x33, 0x63, 0x6C, 0x73);      // #33636C73
const QColor kDotBorder(0xB2, 0x79, 0x79, 0x79);       // #B2797979

QRect wheelRect() {
    return QRect(kContentX + (kContentSize - kDisplayWheel) / 2, kContentY + kWheelTop,
                 kDisplayWheel, kDisplayWheel);
}

QRect dotRect() {
    return QRect(kContentX + kDotLeft, kContentY + kDotTop, kDotSize, kDotSize);
}

QRect sliderRect() {
    return QRect(kContentX + kSliderLeft, kContentY + kSliderTop, kSliderW, kSliderH);
}

bool inCircle(int x, int y) {
    const double dx = x - kLogicalRadius;
    const double dy = y - kLogicalRadius;
    return std::sqrt(dx * dx + dy * dy) <= kLogicalRadius;
}

// 逻辑坐标 (0..180) → H/S/L（0..360 标度，参考 ColorWheelPanel.GetHslColor）
void hslAt(int x, int y, double& h, double& s, double& l) {
    const double dx = x - kLogicalRadius;
    const double dy = y - kLogicalRadius;
    const double dist = std::sqrt(dx * dx + dy * dy);
    double angle = std::atan2(dx, -dy) * 180.0 / M_PI;  // 0° 正上方、顺时针
    if (dx < 0.0)
        angle += 360.0;
    h = angle;
    s = dist / kLogicalRadius * 360.0;
    l = 360.0 * (1.0 - dist / kLogicalRadius * 0.5);  // 中心 1.0、边缘 0.5
}

// HslColor.GetColorComponent（参考 HslColor 实现）
double colorComponent(double t1, double t2, double t3) {
    if (t3 < 0.0)
        t3 += 1.0;
    else if (t3 > 1.0)
        t3 -= 1.0;
    if (t3 < 1.0 / 6.0)
        return t1 + (t2 - t1) * 6.0 * t3;
    if (t3 < 0.5)
        return t2;
    if (t3 < 2.0 / 3.0)
        return t1 + (t2 - t1) * (2.0 / 3.0 - t3) * 6.0;
    return t1;
}

// HSL(0..360) → RGB（复刻参考 HslColor 隐式转换）
void hslToRgb(double h, double s, double l, int& r, int& g, int& b) {
    const double hue = h / 360.0;
    const double sat = s / 360.0;
    const double lum = l / 360.0;
    double rD = 0.0;
    double gD = 0.0;
    double bD = 0.0;
    if (lum > 0.0) {
        if (sat <= 0.0) {
            rD = gD = bD = lum;
        } else {
            const double temp2 = (lum < 0.5) ? lum * (1.0 + sat) : lum + sat - lum * sat;
            const double temp1 = 2.0 * lum - temp2;
            rD = colorComponent(temp1, temp2, hue + 1.0 / 3.0);
            gD = colorComponent(temp1, temp2, hue);
            bD = colorComponent(temp1, temp2, hue - 1.0 / 3.0);
        }
    }
    r = qBound(0, static_cast<int>(255.0 * rD), 255);
    g = qBound(0, static_cast<int>(255.0 * gD), 255);
    b = qBound(0, static_cast<int>(255.0 * bD), 255);
}

// RGB → H/S/L（0..360 标度，参考 System.Drawing 的 GetHue/GetSaturation/GetBrightness）
void rgbToHsl(const QColor& c, double& h, double& s, double& l) {
    const double r = c.redF();
    const double g = c.greenF();
    const double b = c.blueF();
    const double mx = std::max({ r, g, b });
    const double mn = std::min({ r, g, b });
    l = (mx + mn) / 2.0;
    h = 0.0;
    s = 0.0;
    if (mx != mn) {
        const double d = mx - mn;
        s = (l > 0.5) ? d / (2.0 - mx - mn) : d / (mx + mn);
        if (mx == r)
            h = (g - b) / d + (g < b ? 6.0 : 0.0);
        else if (mx == g)
            h = (b - r) / d + 2.0;
        else
            h = (r - g) / d + 4.0;
        h *= 60.0;
    }
    s *= 360.0;
    l *= 360.0;
}

}  // namespace

ColorPickerPanel::ColorPickerPanel(QWidget* parent) : QWidget(parent) {
    setFixedSize(kPanelW, kPanelH);
    rebuildWheelImage();
    currentColor_ = colorAtTarget();
}

void ColorPickerPanel::setColor(const QColor& color) {
    double tH = 0.0;
    double tS = 0.0;
    double tL = 0.0;
    rgbToHsl(color, tH, tS, tL);
    if (qAbs(tL - 360.0) < 10.0)
        tS = 0.0;  // 接近纯白时忽略色相（参考 Nearly.Equals(L, 360, 10)）

    // 扫描匹配色轮上的目标点（参考 SetColorBrush：色相差 <5、饱和度差 ≤32，
    // 且轮上亮度不低于目标亮度-10；命中后按亮度差反推明度滑条值）
    bool matched = false;
    for (int x = 0; x < kLogicalWheel && !matched; ++x) {
        for (int y = 0; y < kLogicalWheel; ++y) {
            if (!inCircle(x, y))
                continue;
            double cH = 0.0;
            double cS = 0.0;
            double cL = 0.0;
            hslAt(x, y, cH, cS, cL);
            if (qAbs(cH - tH) < 5.0 && qAbs(cS - tS) <= 32.0) {
                targetLogical_ = QPointF(x, y);
                if (cL >= tL - 10.0) {
                    const double dl = cL - tL;
                    if (dl > 0.0)
                        deltaBrightness_ = dl / cL * 100.0;
                    else if (qAbs(dl) <= 10.0)
                        deltaBrightness_ = 0.0;
                    matched = true;
                    break;
                }
            }
        }
    }
    if (matched) {
        rebuildWheelImage();
        currentColor_ = colorAtTarget();
    } else {
        currentColor_ = color;  // 不匹配：保留色轮位置/明度，只更新当前色（参考语义）
    }
    update();
}

void ColorPickerPanel::setTargetLogical(int x, int y) {
    if (!inCircle(x, y))
        return;  // 圆外忽略（参考 OnTargetPointChanged 的 IsInCircle 守卫）
    if (static_cast<int>(targetLogical_.x()) == x &&
        static_cast<int>(targetLogical_.y()) == y)
        return;
    targetLogical_ = QPointF(x, y);
    currentColor_ = colorAtTarget();
    update();
}

void ColorPickerPanel::setDelta(double value) {
    value = qBound(0.0, value, 100.0);
    if (qFuzzyCompare(value + 1.0, deltaBrightness_ + 1.0))
        return;
    deltaBrightness_ = value;
    rebuildWheelImage();  // 整张色轮实时变暗（参考 DrawColorCircleByBrightness）
    currentColor_ = colorAtTarget();
    update();
}

void ColorPickerPanel::updateSliderFromX(int x) {
    const QRect s = sliderRect();
    // 滑块中心可移动范围 [左端+10, 左端+110]（参考 WPF：20px 滑块 / 120px 轨道）
    setDelta(qBound(0.0, static_cast<double>(x - s.left()) - 10.0, 100.0));
}

QColor ColorPickerPanel::colorAtTarget() const {
    double h = 0.0;
    double s = 0.0;
    double l = 0.0;
    hslAt(static_cast<int>(targetLogical_.x()), static_cast<int>(targetLogical_.y()),
          h, s, l);
    l = l - l * deltaBrightness_ / 100.0;  // 参考：L - L*ratio
    int r = 0;
    int g = 0;
    int b = 0;
    hslToRgb(h, s, l, r, g, b);
    return QColor(r, g, b);
}

void ColorPickerPanel::rebuildWheelImage() {
    wheelImage_ = QImage(kLogicalWheel, kLogicalWheel, QImage::Format_ARGB32);
    wheelImage_.fill(Qt::transparent);
    const double ratio = deltaBrightness_ / 100.0;
    for (int x = 0; x < kLogicalWheel; ++x) {
        for (int y = 0; y < kLogicalWheel; ++y) {
            if (!inCircle(x, y))
                continue;
            double h = 0.0;
            double s = 0.0;
            double l = 0.0;
            hslAt(x, y, h, s, l);
            l = l - l * ratio;  // 明度滑条联动：整轮按比例变暗
            int r = 0;
            int g = 0;
            int b = 0;
            hslToRgb(h, s, l, r, g, b);
            wheelImage_.setPixelColor(x, y, QColor(r, g, b));
        }
    }
}

void ColorPickerPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // 面板背景 + 边框（圆角 8）
    p.setPen(QPen(kMenuBorder, 1.0));
    p.setBrush(kMenuBackground);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);

    // 色轮（180 逻辑位图平滑缩放到 126 显示）
    const QRect wr = wheelRect();
    p.setPen(Qt::NoPen);
    p.drawImage(wr, wheelImage_);

    // 色轮选中圆点（参考 PaletteSelect 16x16 × 0.7 → 11px）
    const QPixmap thumb(QStringLiteral(":/icons/palette_select.png"));
    const qreal wheelThumb = 11.0;
    const QPointF target(wr.left() + targetLogical_.x() * kScale,
                         wr.top() + targetLogical_.y() * kScale);
    p.drawPixmap(QRectF(target.x() - wheelThumb / 2.0, target.y() - wheelThumb / 2.0,
                        wheelThumb, wheelThumb),
                 thumb, QRectF(thumb.rect()));

    // 当前色圆点 30x30（白色彩点时描边，参考 BrushToBorderThicknessConvert）
    const QRect dot = dotRect();
    p.setBrush(currentColor_);
    p.setPen(currentColor_ == QColor(255, 255, 255) ? QPen(kDotBorder, 1.0) : Qt::NoPen);
    p.drawEllipse(QRectF(dot).adjusted(0.5, 0.5, -0.5, -0.5));

    // 明度滑条：轨道 = 120x20 圆角胶囊（当前色 → 黑的线性渐变，参考
    // GetBrightnessBitmapImage 216x12 拉伸填充）
    const QRect slider = sliderRect();
    double h = 0.0;
    double s = 0.0;
    double l = 0.0;
    hslAt(static_cast<int>(targetLogical_.x()), static_cast<int>(targetLogical_.y()),
          h, s, l);
    int r = 0;
    int g = 0;
    int b = 0;
    hslToRgb(h, s, l, r, g, b);
    QLinearGradient gradient(slider.topLeft(), slider.topRight());
    gradient.setColorAt(0.0, QColor(r, g, b));
    gradient.setColorAt(1.0, QColor(0, 0, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(gradient);
    p.drawRoundedRect(QRectF(slider), 10.0, 10.0);

    // 滑条滑块（PaletteSelect 14x14，中心 = 左端 +10 + 明度值）
    const qreal thumbX = slider.left() + 10.0 + deltaBrightness_;
    const qreal thumbY = slider.top() + kSliderH / 2.0;
    p.drawPixmap(QRectF(thumbX - 7.0, thumbY - 7.0, 14.0, 14.0),
                 thumb, QRectF(thumb.rect()));
}

void ColorPickerPanel::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    const QPoint pos = event->pos();
    const QRect wr = wheelRect();
    if (wr.contains(pos)) {
        const int lx = static_cast<int>((pos.x() - wr.left()) / kScale);
        const int ly = static_cast<int>((pos.y() - wr.top()) / kScale);
        if (inCircle(lx, ly)) {
            dragging_ = DragTarget::Wheel;
            setTargetLogical(lx, ly);
        }
        return;
    }
    if (sliderRect().contains(pos)) {
        dragging_ = DragTarget::Slider;
        updateSliderFromX(pos.x());
    }
}

void ColorPickerPanel::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton))
        return;
    const QPoint pos = event->pos();
    if (dragging_ == DragTarget::Wheel) {
        const QRect wr = wheelRect();
        const int lx = static_cast<int>((pos.x() - wr.left()) / kScale);
        const int ly = static_cast<int>((pos.y() - wr.top()) / kScale);
        setTargetLogical(lx, ly);  // 圆外点自动忽略，滑块停留在边缘
    } else if (dragging_ == DragTarget::Slider) {
        updateSliderFromX(pos.x());
    }
}

void ColorPickerPanel::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    // 参考：色轮 MouseUp / 滑条 PreviewMouseUp 才回调 SelectedBrushCompleted
    if (dragging_ == DragTarget::Wheel || dragging_ == DragTarget::Slider)
        emit colorChosen(currentColor_);
    dragging_ = DragTarget::None;
}
