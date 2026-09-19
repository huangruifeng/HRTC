#pragma once

#include <QWidget>

class QPainter;

// 小工具类型选择弹出面板（Qt::Popup）：选类型（秒表 / 计时器 / 计算器 / 算盘 /
// 骰子 / 大转盘 / 点名器），点击任一项 → widgetChosen(kind) + 关闭。具体参数
//（倒计时时长、骰子面数/颗数、转盘选项、点名名单等）在小工具卡片内部设置，
// 面板不再承担。
class WidgetSetupPanel : public QWidget {
    Q_OBJECT
public:
    explicit WidgetSetupPanel(QWidget* parent = nullptr);

    void setCurrent(int kind);  // 同步外部选择（0=秒表 1=计时器 2=计算器 3=算盘 4=骰子 5=大转盘 6=点名器）

signals:
    void widgetChosen(int kind);  // 选择类型
    void closed();                // 面板关闭（点击外部 / 选中后）

protected:
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRect tabRect(int tab) const;  // Tab 矩形（0~6）
    void paintTabs(QPainter& p);
    void paintHint(QPainter& p);

    int kind_ = 0;        // 0=秒表 1=计时器 2=计算器 3=算盘 4=骰子 5=大转盘 6=点名器
    int hoverTab_ = -1;   // 悬停 Tab（-1 = 无）
};
