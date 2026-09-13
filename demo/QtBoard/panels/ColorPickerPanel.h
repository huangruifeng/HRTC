#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QWidget>

// 内联颜色选择盘（参考 MaxWhiteboard ColorPickerControl + ColorWheelPanel）：
// 126x126 色轮（逻辑 180x180 按 0.7 缩放显示；中心白、边缘 L=0.5，
// 色相 0° 在正上方、顺时针递增）+ 底部行（30x30 当前色圆点 + 120x20 明度滑条）。
// 明度滑条实时调暗整张色轮；色轮/滑条释放时发出 colorChosen（参考 SelectedBrushCompleted）。
class ColorPickerPanel : public QWidget {
    Q_OBJECT
public:
    explicit ColorPickerPanel(QWidget* parent = nullptr);

    // 同步外部当前色：匹配色轮目标点与明度滑条（参考 ColorWheelPanel.SetColorBrush）
    void setColor(const QColor& color);
    QColor currentColor() const { return currentColor_; }

signals:
    void colorChosen(const QColor& color);  // 释放时（色轮 MouseUp / 滑条 MouseUp）

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class DragTarget { None, Wheel, Slider };

    void setTargetLogical(int x, int y);   // 逻辑坐标目标点（仅圆内生效）
    void setDelta(double value);           // 明度滑条值 0..100
    void updateSliderFromX(int x);
    void rebuildWheelImage();              // 依明度系数重绘整张色轮位图
    QColor colorAtTarget() const;          // 目标点颜色 × (1 - 明度系数)

    QImage wheelImage_;                     // 180x180 色轮位图（显示时平滑缩放到 126）
    QPointF targetLogical_{ 90.0, 90.0 };   // 目标点（逻辑 180 坐标系）
    double deltaBrightness_ = 0.0;          // 明度滑条值 0..100
    QColor currentColor_ = Qt::white;
    DragTarget dragging_ = DragTarget::None;
};
