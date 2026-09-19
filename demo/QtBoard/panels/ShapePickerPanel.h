#pragma once

#include <QWidget>

// 图形选择面板（Qt::Popup，3x2 六形状：矩形/圆形/椭圆/三角形/五边形/五角星，索引 = GraphicKind）：
// 格子图标由数据层 BuildShape 轮廓实时生成（与落库几何一致）；悬停高亮、
// 当前选中形状橙色描边；点击发出 shapeSelected(kind) 并关闭。
class ShapePickerPanel : public QWidget {
    Q_OBJECT
public:
    explicit ShapePickerPanel(QWidget* parent = nullptr);

    void setCurrentKind(int kind);  // 同步外部当前形状（高亮）

signals:
    void shapeSelected(int kind);   // 选择形状（GraphicKind 整数值）
    void closed();                  // 面板关闭（点击外部 / 选中后）

protected:
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int cellAt(const QPoint& pos) const;  // 命中格子索引（-1 = 空白）

    int kind_ = 0;            // 当前选中形状（GraphicKind，默认矩形）
    int hoverIndex_ = -1;     // 悬停格子索引
};
