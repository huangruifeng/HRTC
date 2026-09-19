#pragma once

#include <QByteArray>
#include <QMainWindow>
#include <QPixmap>

#include <cstdint>
#include <memory>

#include "BoardView.h"
#include "PenSettingPanel.h"
#include "WhiteboardSession.h"

class BoardToolBar;
class EraserPanel;
class SlideManagerPanel;
class SettingsPanel;
class MorePanel;
class ShapePickerPanel;
class TableSetupPanel;
class TextSetupPanel;
class WidgetSetupPanel;
class OtherToolsPanel;

class QEvent;
class QResizeEvent;

// 主窗口：全屏黑板画布 + 底部居中悬浮工具栏（工具 / 功能 / 页面三组）；
// 笔设置与滑动清屏为工具栏按钮弹出的面板（Qt::Popup）；窗口锁定 16:9 缩放。
// 显示模式：窗口模式（默认，可缩放 16:9 窗口）/ 黑板模式（全屏无标题栏、盖任务栏，
// 画布取 16:9 最大内接矩形居中）。
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* event) override;  // 16:9 等比锁定
    void changeEvent(QEvent* event) override;        // 全屏状态被外部改变时同步模式
    bool eventFilter(QObject* watched, QEvent* event) override;  // 中央区域 resize → 画布几何

private:
    void onToolSelected(BoardView::Tool tool);
    void onPenChanged(PenSettingPanel::PenKind kind, uint32_t color, int width);
    void onClear();
    void onInteract();                       // 互动白板（入口在"更多"面板）：加入/断开房间
    void onExitApp();                        // 退出程序（入口在"更多"面板）
    void onPrevPage();
    void onNextPage();
    void onPagePanelRequested();             // 页码按钮：弹出/收起缩略图面板
    void rebuildPagePanel();                 // 重建缩略图条目（页数据 + 当前页）
    void positionPagePanel();                // 面板定位到页码按钮上方
    void onPageActivated(int index);         // 点击缩略图切页并关闭面板
    void onPageDeleteRequested(int index);   // 点击缩略图删除按钮
    void updatePageButtons();
    void onSettingsRequested();                  // 设置入口：弹出/收起设置面板
    void rebuildSettingsPanel();                 // 重建背景网格（当前背景 + 面板尺寸）
    void positionSettingsPanel();                // 面板定位到更多按钮上方
    void onMoreRequested();                      // 更多按钮：弹出/收起更多面板
    void positionMorePanel();                    // 更多面板定位到更多按钮上方
    void onOtherRequested();                     // "其他"按钮：弹出/收起其他工具面板
    void positionOtherPanel();                   // "其他"面板定位到"其他"按钮上方
    void updateOtherButtonState();               // 刷新"其他"按钮激活态（面板可见或当前工具∈5类）
    void onSaveBoard();                          // 保存白板到文件（弹出路径选择）
    void onOpenBoard();                          // 从文件打开白板（弹出路径选择）
    void onBackgroundSelected(const QString& key, const QPixmap& pixmap);  // 应用黑板背景
    void setDisplayMode(bool boardMode);         // 切换显示模式（黑板=全屏 / 窗口）
    void updateBoardGeometry();                  // 计算画布几何（黑板模式 16:9 居中）
    void updateSessionStatus(const QString& text);
    void showPanelAbove(QWidget* panel, BoardView::Tool tool);  // 定位并弹出/收起面板
    void applyStyleSheet();

    BoardView* view_ = nullptr;
    BoardToolBar* toolBar_ = nullptr;
    PenSettingPanel* penPanel_ = nullptr;
    EraserPanel* eraserPanel_ = nullptr;
    SlideManagerPanel* pagePanel_ = nullptr;
    SettingsPanel* settingsPanel_ = nullptr;
    MorePanel* morePanel_ = nullptr;
    ShapePickerPanel* shapePanel_ = nullptr;
    TableSetupPanel* tablePanel_ = nullptr;
    TextSetupPanel* textPanel_ = nullptr;
    WidgetSetupPanel* widgetPanel_ = nullptr;
    OtherToolsPanel* otherPanel_ = nullptr;
    std::unique_ptr<WhiteboardSession> session_;

    QWidget* central_ = nullptr;      // 中央区域（画布 + 悬浮工具栏）
    bool boardMode_ = false;          // 当前显示模式（true=黑板模式全屏）
    QByteArray windowedGeometry_;     // 窗口模式几何（切全屏前保存，切回时恢复）
    uint32_t penColor_ = 0x00FFFFFF;  // 默认白色（COLORREF 语义 0x00BBGGRR）
    int penWidth_ = 3;                // 默认细档（3 / 6 / 12）
    uint32_t textColor_ = 0x00FFFFFF; // 文字工具颜色（COLORREF 语义 0x00BBGGRR，默认白色）
    int widgetKind_ = 0;              // 小工具类型（0 秒表 / 1 计时器 / 2 计算器 / 3 算盘 / 4 骰子 / 5 大转盘 / 6 点名器，面板选择）
    QString backgroundKey_;           // 当前背景标识（资源路径或图片文件绝对路径）
    QPixmap backgroundPixmap_;        // 当前背景原图
    // 更多面板因点击更多按钮而关闭（Popup 在按下阶段自动关闭）：
    // 同一次点击的释放阶段不重开面板
    bool moreButtonClosePending_ = false;
    // 页数面板因点击页码按钮而关闭：同上
    bool pageButtonClosePending_ = false;
    // "其他"面板因点击"其他"按钮而关闭：同上（释放阶段不重开）
    bool otherButtonClosePending_ = false;
    // 笔 / 擦除 / 图形 / 表格 / 文字 / 小工具面板因点击对应工具按钮在按下阶段自动关闭：同上（释放阶段不重开）
    bool penButtonClosePending_ = false;
    bool eraserButtonClosePending_ = false;
    bool shapeButtonClosePending_ = false;
    bool tableButtonClosePending_ = false;
    bool textButtonClosePending_ = false;
    bool widgetButtonClosePending_ = false;
    bool aspectLockGuard_ = false;    // 16:9 锁定 resize 递归保护
    QString windowTitleBase_;
};
