#pragma once

#include <QHash>
#include <QPixmap>
#include <QString>
#include <QVector>
#include <QWidget>

class QButtonGroup;
class QGridLayout;
class QHideEvent;
class QPushButton;

// 设置面板（Qt::Popup，风格同 PageRail 面板）：
// 256 宽圆角面板 + 标题"设置"；"显示模式"分区以分段按钮切换黑板模式（全屏）/ 窗口模式；
// "工具栏模式"分区切换底部工具栏 / 桌面圆盘（悬浮球）两种模式；
// "黑板背景"分区以 2 列缩略图网格展示可选背景——
// 内置默认图（:/images/board_bg_default.png，参考 Image.Background.Default）
// + exe 同级 Backgrounds 目录扫描图（参考 AppPath.BackgroundPath），
// 另有"从文件选择…"按钮自定义背景。点击缩略图立即应用（橙框选中态）。
class SettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPanel(QWidget* parent = nullptr);

    // 重建背景网格（currentKey：当前背景标识——资源路径或图片文件绝对路径）
    void rebuild(const QString& currentKey);
    // 更新显示模式选中态（true=黑板模式全屏，false=窗口模式；不发出信号）
    void setDisplayMode(bool boardMode);
    // 更新工具栏模式选中态（true=桌面圆盘，false=底部工具栏；不发出信号）
    void setToolBarMode(bool diskMode);

signals:
    void backgroundSelected(const QString& key, const QPixmap& pixmap);  // 点击背景缩略图
    void displayModeChanged(bool boardMode);  // 切换显示模式（黑板模式 / 窗口模式）
    void toolBarModeChanged(bool diskMode);   // 切换工具栏模式（桌面圆盘 / 底部工具栏）
    void closed();  // 面板关闭（点击外部 / 隐藏）

protected:
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    class BackgroundThumb;

    void clearItems();
    void addItem(const QString& key, const QPixmap& source);
    void selectKey(const QString& key);  // 更新选中态并发出应用信号
    void updatePanelSize();
    void onBrowse();  // "从文件选择…"

    QGridLayout* grid_ = nullptr;
    QVector<BackgroundThumb*> thumbs_;
    QHash<QString, QPixmap> sources_;  // key -> 原图
    QString selectedKey_;
    QButtonGroup* modeGroup_ = nullptr;
    QPushButton* boardModeButton_ = nullptr;   // "黑板模式"（全屏 16:9）
    QPushButton* windowModeButton_ = nullptr;  // "窗口模式"（保持原窗口）
    QButtonGroup* toolBarGroup_ = nullptr;     // 工具栏模式（互斥）
    QPushButton* barModeButton_ = nullptr;     // "底部工具栏"
    QPushButton* diskModeButton_ = nullptr;    // "桌面圆盘"（悬浮球）
};
