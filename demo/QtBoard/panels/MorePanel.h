#pragma once

#include <QWidget>

class QHideEvent;
class QPushButton;

// "更多"面板（Qt::Popup，风格同 SettingsPanel）：
// 256 宽圆角面板 + 标题"更多"；条目：互动白板 / AI 助手 / 保存白板 / 打开白板 / 设置 / 退出程序。
class MorePanel : public QWidget {
    Q_OBJECT
public:
    explicit MorePanel(QWidget* parent = nullptr);

signals:
    void interactRequested();  // 互动白板（加入/断开房间）
    void aiRequested();        // AI 助手（打开/收起对话面板）
    void saveRequested();      // 保存白板到文件（弹出路径选择）
    void openRequested();      // 从文件打开白板（弹出路径选择）
    void settingsRequested();  // 进入设置（打开设置面板）
    void exitRequested();      // 退出程序
    void closed();             // 面板关闭（点击外部 / 隐藏）

protected:
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QPushButton* interactButton_ = nullptr;
    QPushButton* aiButton_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPushButton* openButton_ = nullptr;
    QPushButton* settingsButton_ = nullptr;
    QPushButton* exitButton_ = nullptr;
};
