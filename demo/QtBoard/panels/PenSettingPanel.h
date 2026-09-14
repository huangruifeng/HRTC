#pragma once

#include <QWidget>

#include <cstdint>

class QButtonGroup;
class QHideEvent;
class ColorPickerPanel;

// 笔设置弹出面板（Qt::Popup，参考 MaxWhiteboard PenSettingContent）：
// 顶部笔类型切换（常规笔/手指 | 荧光笔）+ 44 高预览条（当前颜色背景 + 笔迹示例 + 粗细文字）
// 左列 3 档粗细（常规笔 3/6/12，荧光笔 12/24/48）/ 右列 4x3 色格（11 色板 + 色轮）。
// 点击色轮格子：面板向右加宽 6+222，显示内联颜色选择盘（ColorPickerPanel，
// 底部对齐）；点击色板或关闭面板时收起。两类笔各自记忆颜色与档位；
// 荧光笔透明度由 MainWindow 在颜色高 8 位统一编码（渲染层 colorToQColor 提取）。
class PenSettingPanel : public QWidget {
    Q_OBJECT
public:
    enum class PenKind { Normal, Highlighter };  // 笔类型：常规笔/手指 / 荧光笔

    explicit PenSettingPanel(QWidget* parent = nullptr);

    // 同步外部当前状态（类型 + 颜色 + 宽度：预览 + 选中高亮），不触发信号
    void setCurrent(PenKind kind, uint32_t color, int width);

signals:
    // 笔设置变化（类型/颜色/宽度任一变化都发全量状态；颜色为 COLORREF 语义 0x00BBGGRR）
    void penChanged(PenKind kind, uint32_t color, int width);
    void closed();  // 面板关闭（点击外部等）

protected:
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    class PreviewBar;
    class ColorDotButton;
    class ThicknessButton;
    class TypeSwitchButton;

    void updateChecks();  // 依据 kind_ 与当前类型的颜色/宽度刷新预览与选中态
    void onWheelClicked();                       // 色轮格子：展开内联颜色选择盘
    void onPickerColorChosen(const QColor& c);   // 取色完成（保持展开）
    void onTypeClicked(PenKind kind);            // 切换笔类型（两类各自记忆参数）
    void collapsePicker();                       // 收起颜色选择盘（恢复 350 宽）

    uint32_t& colorRef();  // 当前类型的颜色（常规 color_ / 荧光 hlColor_）
    int& widthRef();       // 当前类型的宽度（常规 width_ / 荧光 hlWidth_）
    static int widthFor(PenKind kind, int index);  // 档位 index → 实际宽度（3/6/12 或 12/24/48）

    PreviewBar* preview_ = nullptr;
    TypeSwitchButton* normalTypeButton_ = nullptr;
    TypeSwitchButton* highlighterTypeButton_ = nullptr;
    QButtonGroup* thicknessGroup_ = nullptr;
    QButtonGroup* colorGroup_ = nullptr;
    ColorDotButton* wheelButton_ = nullptr;
    ColorPickerPanel* picker_ = nullptr;
    bool pickerVisible_ = false;

    PenKind kind_ = PenKind::Normal;
    uint32_t color_ = 0x00FFFFFF;    // 常规笔颜色（COLORREF 语义，默认白色）
    int width_ = 3;                  // 常规笔宽：3 / 6 / 12（细 / 中 / 粗）
    uint32_t hlColor_ = 0x0000E8FF;  // 荧光笔颜色（默认亮黄 RGB(255,232,0)）
    int hlWidth_ = 24;               // 荧光笔宽：12 / 24 / 48（细 / 中 / 粗）
};
