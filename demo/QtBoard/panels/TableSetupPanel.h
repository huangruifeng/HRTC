#pragma once

#include <QWidget>

// 表格尺寸选择面板（Qt::Popup，8x8 网格悬停预选行列，范围 2~8）：
// 悬停高亮前 rows x cols 区域并在底部显示尺寸文字；展开时预选 = 当前设置；
// 点击发出 tableSizeChanged(rows, cols) 并关闭。
class TableSetupPanel : public QWidget {
    Q_OBJECT
public:
    explicit TableSetupPanel(QWidget* parent = nullptr);

    void setCurrentSize(int rows, int cols);  // 同步外部当前行列（高亮初值）

signals:
    void tableSizeChanged(int rows, int cols);  // 选择表格行列数（2~8）
    void closed();                              // 面板关闭（点击外部 / 选中后）

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QRect gridRect() const;  // 8x8 格子区域（场景 = 面板坐标）

    int rows_ = 3;        // 当前选中行列（点击确认值）
    int cols_ = 3;
    int hoverRows_ = 3;   // 悬停预选
    int hoverCols_ = 3;
};
