#pragma once

#include <QWidget>

class QEvent;

// "其他"工具面板（Qt::Popup）：图形 / 导图 / 表格 / 文字 / 小工具 5 条目横向排列；
// 点击条目 → 发出对应请求信号 + 关闭；当前工具条目橙色描边（setCurrentIndex 同步）、
// 悬停条目浅白高亮。交互模式同 WidgetSetupPanel（mouseMove 悬停、mousePress 命中、
// 点击面板外交基类、hideEvent 发 closed）。
class OtherToolsPanel : public QWidget {
    Q_OBJECT
public:
    // 条目下标（与信号一一对应）
    enum Item {
        ItemShape = 0,  // 图形
        ItemMindMap,    // 导图
        ItemTable,      // 表格
        ItemText,       // 文字
        ItemWidget,     // 小工具
        ItemCount,
    };

    explicit OtherToolsPanel(QWidget* parent = nullptr);

    void setCurrentIndex(int index);  // 当前工具对应条目 0~4（-1=无）

signals:
    void shapeRequested();    // 图形
    void mindMapRequested();  // 导图
    void tableRequested();    // 表格
    void textRequested();     // 文字
    void widgetRequested();   // 小工具
    void closed();            // 面板关闭（点击外部 / 选中条目后）

protected:
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRect itemRect(int index) const;  // 条目矩形（0~4）

    int currentIndex_ = -1;  // 当前工具对应条目（-1=无）
    int hoverIndex_ = -1;    // 悬停条目（-1=无）
};
