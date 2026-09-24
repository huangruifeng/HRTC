#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTimer;

class BoardView;
class QtBoardData;
class QKeyEvent;
class QShowEvent;
class QHideEvent;

namespace ai {

class AiAgent;

// AI 助手对话面板（MainWindow 子部件，右下角悬浮，默认隐藏）：
// 标题 + 齿轮（设置行：API Key / Base URL / Model）+ 只读对话记录 +
// 输入行（Enter 发送）+ 发送/停止 + 美化选中笔迹（无选中禁用）+ 清空对话。
// 思考中状态、工具执行摘要与错误均在面板内展示。
class AiPanel : public QWidget {
    Q_OBJECT
public:
    AiPanel(BoardView& view, QtBoardData& data, QWidget* parent = nullptr);

    void AlignBottomRight();  // 定位到父窗口右下角（父窗口 resize 后调用）
    void ShowAndFocus();      // 打开并聚焦输入框（未配置 Key 时自动展开设置行）

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;  // Esc 收起面板

private:
    void BuildUi();
    void LoadConfigToUi();
    void SaveConfigFromUi();
    void AppendLine(const QString& line);
    void OnSendClicked();
    void OnBeautifyClicked();
    void OnBusyChanged(bool busy);
    void RefreshBeautifyEnabled();

    BoardView& view_;
    QtBoardData& data_;
    AiAgent* agent_ = nullptr;

    QPushButton* settingsButton_ = nullptr;  // 齿轮：展开/收起设置行
    QPushButton* closeButton_ = nullptr;     // ✕：收起面板
    QWidget* settingsRow_ = nullptr;
    QLineEdit* apiKeyEdit_ = nullptr;
    QLineEdit* baseUrlEdit_ = nullptr;
    QComboBox* modelCombo_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLineEdit* inputEdit_ = nullptr;
    QPushButton* sendButton_ = nullptr;  // 空闲"发送" / 忙碌"停止"
    QPushButton* beautifyButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QTimer* selectionTimer_ = nullptr;  // 轮询选中态刷新"美化"按钮可用性
};

}  // namespace ai
