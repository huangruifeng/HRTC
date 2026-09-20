#pragma once

#include <QRect>
#include <QWidget>

#include "BoardView.h"
#include "PenSettingPanel.h"

class QColor;
class QMouseEvent;
class QPaintEvent;
class QPixmap;

// 桌面圆盘模式工具栏（悬浮球形态）：收起 = 56px 可拖动悬浮球（当前工具图标），
// 点击展开；展开 = 中心球（点击收起 / 按住拖动圆盘）+ 内环 8 固定钮
//（画笔/荧光棒/擦除/选择/鼠标/撤销/重做/更多，45° 均布）+ 外环随内环焦点的
// 动态设置钮。外环内容（焦点 = 最后点击的内环类目，撤销/重做无外环）：
//   画笔/荧光棒 → 3 档粗细 + 11 色板 + 色轮（弹 ColorPickerPanel 取色）
//   擦除        → 5 档橡皮大小（长方形擦除区）+ 清屏（弹滑动清屏面板）
//   选择        → 选择框（Tool::Select）/ 套索（Tool::Lasso）
//   鼠标        → 缩小 / 100% / 放大
//   更多        → 文字 / 图形（弹形状面板）/ 表格 / 小工具 / 菜单（弹 MorePanel）
// 全部自绘（圆形深色半透背景，同工具栏 rgba(35,39,42,204) 风格）。
class BoardDisk : public QWidget {
    Q_OBJECT
public:
    explicit BoardDisk(QWidget* parent = nullptr);

    // ---------- 状态同步（与底部工具栏共享；外部工具切换/参数变化时回灌） ----------
    void setCurrentTool(BoardView::Tool tool);  // 同步悬浮球图标与内环高亮/外环焦点
    void setPenState(PenSettingPanel::PenKind kind, uint32_t color, int width);  // 回灌对应笔类目
    void setEraserSize(int size);
    void setZoomPercent(qreal percent);   // 更新外环 "100%" 按钮文字

    // 画布活动范围（悬浮球/圆盘拖动 clamp 用；空矩形 = 父 widget 区域）
    void setBounds(const QRect& rect);

    bool expanded() const { return expanded_; }
    void setExpanded(bool on);  // 展开/收起（中心点保持不变）

    // 展开态圆盘外沿全局矩形（弹窗面板锚点定位用；收起态返回悬浮球矩形）
    QRect diskGlobalRect() const;

    // 笔宽度档位（与 PenSettingPanel 一致：常规笔 3/6/12，荧光笔 12/24/48）
    static int widthForKind(PenSettingPanel::PenKind kind, int index);

signals:
    void toolSelected(BoardView::Tool tool);  // 内环类目：擦除/选择/鼠标（笔类目走 penParamChanged）
    // 笔参数变化（类型/颜色/宽度全量；颜色 COLORREF 语义 0x00BBGGRR；含切笔类型）
    void penParamChanged(PenSettingPanel::PenKind kind, uint32_t color, int width);
    void eraserSizeChanged(int size);
    void clearPanelRequested();         // 外环"清屏"：弹滑动清屏面板（由 MainWindow 定位）
    void undoRequested();
    void redoRequested();
    void zoomInRequested();
    void zoomOutRequested();
    void zoomResetRequested();
    void textToolRequested();           // 更多外环：文字（MainWindow 弹文字设置面板）
    void shapePanelRequested();         // 更多外环：图形（MainWindow 弹形状选择面板）
    void tableToolRequested();          // 更多外环：表格（MainWindow 弹表格设置面板）
    void widgetToolRequested();         // 更多外环：小工具（MainWindow 弹小工具面板）
    void menuRequested();               // 更多外环：菜单（MainWindow 弹 MorePanel）

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    // 外环按钮（自绘 + 命中检测）
    struct RingButton {
        enum class Type {
            PenWidth,    // 笔粗细档（id = 档位 index 0~2）
            PenColor,    // 色板格（id = 色板 index 0~10）
            Wheel,       // 色轮（弹 ColorPickerPanel）
            EraserSize,  // 橡皮大小档（id = 档位 index 0~4）
            Clear,       // 清屏
            RubberBand,  // 选择框
            Lasso,       // 套索
            ZoomOut, ZoomReset, ZoomIn,
            Shape,       // 图形（四种形状合一入口：弹 ShapePickerPanel）
            Widget,      // 小工具（弹 WidgetSetupPanel）
            Text, Table, Menu,
        };
        QRect rect;
        Type type;
        int id = 0;
        bool checked = false;
    };

    // 内环按钮
    struct InnerButton {
        enum class Type { Pen, Highlighter, Eraser, Select, Mouse, Undo, Redo, More };
        QRect rect;
        Type type;
        bool checked = false;  // 当前工具对应类目点亮
        bool hot = false;      // 外环焦点类目（决定外环内容）描边提示
    };

    // 悬浮球/中心球图标（当前工具映射；重建缓存 ballIcon_）
    void refreshBallIcon();
    void rebuildRing();             // 依据 focusKind_ 重建外环按钮表
    void applyGeometry(bool expand);  // 展开/收起几何（中心点不变 + clamp）
    QPoint clampedPos(const QSize& size, const QPointF& center) const;  // 中心点 → clamp 后左上角
    void showWheelPopup(const QRect& anchor);  // 色轮钮：弹出取色盘（锚定外环钮）
    void ringClicked(const RingButton& button);
    void innerClicked(const InnerButton& button);

    // 拖动状态
    bool dragging_ = false;          // 位移超过阈值进入拖动
    bool pressOnBall_ = false;       // 按下点在悬浮球/中心球上（可拖动/点击展开收起）
    bool pressMoved_ = false;        // 按下后是否产生位移
    QPoint pressGlobalPos_;
    QPoint pressWidgetPos_;   // 按下时部件左上角（父坐标；拖动目标 = 此位置 + 全量位移）
    int dragThreshold_ = 5;

    // 悬浮球图标缓存（避免每帧重载）
    QPixmap ballIcon_;
    BoardView::Tool iconTool_ = BoardView::Tool::Pen;
    PenSettingPanel::PenKind iconPenKind_ = PenSettingPanel::PenKind::Normal;

    // 内环/外环几何
    QVector<InnerButton> innerButtons_;
    QVector<RingButton> ringButtons_;
    QRect ballRect_;  // 悬浮球（收起态整窗）/中心球（展开态中心）

    // 状态
    bool expanded_ = false;
    BoardView::Tool tool_ = BoardView::Tool::Pen;
    InnerButton::Type focusKind_ = InnerButton::Type::Pen;  // 外环焦点类目
    PenSettingPanel::PenKind penKind_ = PenSettingPanel::PenKind::Normal;
    // 两类笔各自记忆（同 PenSettingPanel）
    uint32_t normalColor_ = 0x00FFFFFF;
    int normalWidth_ = 3;
    uint32_t hlColor_ = 0x0000E8FF;
    int hlWidth_ = 24;
    int eraserSize_ = 40;
    qreal zoomPercent_ = 100.0;
    QRect bounds_;  // 画布活动范围（空 = 不限制）
    bool positioned_ = false;  // 初始位置已设置（首次 setBounds 后贴右缘中部）

    QWidget* wheelPopup_ = nullptr;       // 色轮弹出容器（Qt::Popup）
    class ColorPickerPanel* wheelPicker_ = nullptr;

    static const int kPaletteCount = 11;  // 色板颜色数（同 PenSettingPanel）
};
