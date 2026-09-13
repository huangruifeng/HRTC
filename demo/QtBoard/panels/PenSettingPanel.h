#pragma once

#include <QWidget>

#include <cstdint>

class QButtonGroup;
class QHideEvent;
class ColorPickerPanel;

// 笔设置弹出面板（Qt::Popup，参考 MaxWhiteboard PenSettingContent）：
// 顶部笔类型栏 + 44 高预览条（当前颜色背景 + 笔迹示例 + 粗细文字）
// 左列 3 档粗细（细/中/粗 = 笔宽 3/6/12）/ 右列 4x3 色格（11 色板 + 色轮）。
// 点击色轮格子：面板向右加宽 6+222，显示内联颜色选择盘（ColorPickerPanel，
// 底部对齐）；点击色板或关闭面板时收起。
class PenSettingPanel : public QWidget {
    Q_OBJECT
public:
    explicit PenSettingPanel(QWidget* parent = nullptr);

    // 同步外部当前状态（预览 + 选中高亮），不触发信号
    void setCurrent(uint32_t color, int width);

signals:
    void penColorChanged(uint32_t color);  // COLORREF 语义 (0x00BBGGRR)
    void penWidthChanged(int width);
    void closed();                         // 面板关闭（点击外部等）

protected:
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    class PreviewBar;
    class ColorDotButton;
    class ThicknessButton;

    void updateChecks();  // 依据 color_ / width_ 刷新预览与选中态
    void onWheelClicked();                       // 色轮格子：展开内联颜色选择盘
    void onPickerColorChosen(const QColor& c);   // 取色完成（保持展开）
    void collapsePicker();                       // 收起颜色选择盘（恢复 350 宽）

    PreviewBar* preview_ = nullptr;
    QButtonGroup* thicknessGroup_ = nullptr;
    QButtonGroup* colorGroup_ = nullptr;
    ColorDotButton* wheelButton_ = nullptr;
    ColorPickerPanel* picker_ = nullptr;
    bool pickerVisible_ = false;

    uint32_t color_ = 0x00FFFFFF;  // COLORREF 语义，默认白色
    int width_ = 3;                // 笔宽：3 / 6 / 12（细 / 中 / 粗）
};
