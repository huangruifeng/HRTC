#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

#include <cstdint>

class QButtonGroup;
class ColorPickerPanel;
class QPainter;

// 文字设置弹出面板（Qt::Popup）：
// 左侧垂直滚轮字号（档位 16/24/32/48/64/96；中央橙色高亮当前档、上下露出
// 相邻档并渐小渐淡；鼠标滚轮滚动切换且即时生效，点击档位选中并关闭）；
// 右侧 4x3 色格（11 色板 + 色轮）。点击色轮格子：面板向右加宽 6+222，
// 显示内联颜色选择盘（ColorPickerPanel，与面板等高）；点击色板或关闭面板时收起。
class TextSetupPanel : public QWidget {
    Q_OBJECT
public:
    explicit TextSetupPanel(QWidget* parent = nullptr);

    void setCurrentSize(int size);         // 同步外部当前字号（滚轮中央高亮）
    void setCurrentColor(uint32_t color);  // 同步外部当前颜色（色板/色轮选中态）

signals:
    void fontSizeChanged(int size);     // 选择字号（像素）
    void colorChanged(uint32_t color);  // 选择颜色（COLORREF 语义 0x00BBGGRR）
    void closed();                      // 面板关闭（点击外部 / 选中字号后）

protected:
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    class ColorDotButton;

    static const QVector<int>& sizeOptions();        // 字号档位表
    int nearestIndex() const;                        // 当前字号在档位表中的最近下标
    QRect slotRect(int slot) const;                  // 滚轮槽矩形（面板坐标）
    QRect rollerRect() const;                        // 滚轮整体区域（滚轮事件判定）
    int optionIndexAt(const QPoint& pos) const;      // 命中档位下标（-1 = 无）
    void selectOption(int optionIndex, bool close);  // 选中档位（发信号，可关闭面板）
    void paintRoller(QPainter& p);                   // 绘制垂直滚轮

    void updateChecks();                        // 刷新色板/色轮选中态
    void onWheelColorClicked();                 // 色轮格子：展开内联颜色选择盘
    void onPickerColorChosen(const QColor& c);  // 取色完成（保持展开）
    void collapsePicker();                      // 收起颜色选择盘（恢复 350 宽）

    int current_ = 32;       // 当前字号（点击 / 滚轮确认值）
    int hoverSlot_ = -1;     // 悬停槽位（-1 = 无）
    QButtonGroup* colorGroup_ = nullptr;
    ColorDotButton* wheelButton_ = nullptr;
    ColorPickerPanel* picker_ = nullptr;
    bool pickerVisible_ = false;
    uint32_t color_ = 0x00FFFFFF;  // 当前文字颜色（COLORREF 语义，默认白色）
};
