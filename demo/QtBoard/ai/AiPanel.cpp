#include "AiPanel.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include "AiAgent.h"
#include "AiClient.h"
#include "BoardIcons.h"
#include "BoardView.h"
#include "QtBoardData.h"

namespace ai {

namespace {

// 颜色与 MorePanel/SettingsPanel 风格一致（覆盖画布，略高不透明度）
const QColor kPanelBackground(0x23, 0x27, 0x2A, 0xF0);  // #F023272A
const QColor kPanelBorder(0x33, 0x63, 0x6C, 0x73);      // #33636C73

// 子控件深色 QSS（面板背景由 paintEvent 自绘）
const char* const kChildQss =
    "QLineEdit, QPlainTextEdit, QComboBox {"
    "    background: #1E2225; color: #DDDDDD; border: 1px solid #3F4448;"
    "    border-radius: 4px; selection-background-color: #FF7D00; }"
    "QLineEdit { padding: 4px 8px; font-size: 13px; }"
    "QPlainTextEdit { font-size: 13px; }"
    "QComboBox { padding: 3px 6px; font-size: 13px; }"
    "QComboBox::drop-down { border: none; width: 18px; }"
    "QComboBox QAbstractItemView { background: #23272A; color: #DDDDDD;"
    "    selection-background-color: #FF7D00; }"
    "QPushButton { background: #32383B; border: 1px solid #3F4448; border-radius: 4px;"
    "    color: #CCCCCC; font-size: 13px; padding: 4px 10px; }"
    "QPushButton:hover { background: #3A4045; }"
    "QPushButton:pressed { background: #2A2F32; }"
    "QPushButton:disabled { color: #767676; background: #2A2E31; }";

const char* const kFlatButtonQss =
    "QPushButton { background: transparent; border: none; color: #CCCCCC; font-size: 15px; }"
    "QPushButton:hover { background: #3A4045; border-radius: 4px; }";

constexpr int kPanelW = 380;
constexpr int kPanelH = 470;
constexpr int kMargin = 16;  // 相对父窗口右下角的边距

}  // namespace

AiPanel::AiPanel(BoardView& view, QtBoardData& data, QWidget* parent)
    : QWidget(parent), view_(view), data_(data) {
    BuildUi();

    agent_ = new AiAgent(view, data, this);
    connect(agent_, &AiAgent::busyChanged, this, &AiPanel::OnBusyChanged);
    connect(agent_, &AiAgent::statusChanged, this,
            [this](const QString& status) { statusLabel_->setText(status); });
    connect(agent_, &AiAgent::actionDone, this, [this](const QString& summary) {
        AppendLine(QStringLiteral("· ") + summary);
    });
    connect(agent_, &AiAgent::replyReady, this, [this](const QString& text) {
        AppendLine(QStringLiteral("助手：") + text);
        statusLabel_->clear();
    });
    connect(agent_, &AiAgent::errorOccurred, this, [this](const QString& error) {
        AppendLine(QStringLiteral("⚠ ") + error);
        statusLabel_->clear();
    });

    // 选中态无信号：可见期间定时刷新"美化"按钮
    selectionTimer_ = new QTimer(this);
    selectionTimer_->setInterval(400);
    connect(selectionTimer_, &QTimer::timeout, this, &AiPanel::RefreshBeautifyEnabled);

    hide();
}

void AiPanel::BuildUi() {
    setFixedSize(kPanelW, kPanelH);
    setStyleSheet(QString::fromLatin1(kChildQss));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 8, 12, 12);
    root->setSpacing(8);

    // ---------- 标题行：标题 + 齿轮 + 关闭 ----------
    auto* titleRow = new QHBoxLayout();
    titleRow->setSpacing(4);
    auto* title = new QLabel(QStringLiteral("AI 助手"), this);
    title->setStyleSheet(QStringLiteral("QLabel { color: #EEEEEE; font-size: 17px; }"));
    titleRow->addWidget(title);
    titleRow->addStretch(1);

    settingsButton_ = new QPushButton(this);
    settingsButton_->setFixedSize(26, 26);
    settingsButton_->setCursor(Qt::PointingHandCursor);
    settingsButton_->setFocusPolicy(Qt::NoFocus);
    settingsButton_->setToolTip(QStringLiteral("设置（API Key / Base URL / 模型）"));
    settingsButton_->setIcon(QIcon(BoardIcons::pixmap(BoardIcons::Glyph::Settings, false)));
    settingsButton_->setIconSize(QSize(16, 16));
    settingsButton_->setStyleSheet(QString::fromLatin1(kFlatButtonQss));
    titleRow->addWidget(settingsButton_);

    closeButton_ = new QPushButton(QStringLiteral("✕"), this);
    closeButton_->setFixedSize(26, 26);
    closeButton_->setCursor(Qt::PointingHandCursor);
    closeButton_->setFocusPolicy(Qt::NoFocus);
    closeButton_->setToolTip(QStringLiteral("收起"));
    closeButton_->setStyleSheet(QString::fromLatin1(kFlatButtonQss));
    titleRow->addWidget(closeButton_);
    root->addLayout(titleRow);

    // ---------- 设置行（默认隐藏，齿轮展开） ----------
    settingsRow_ = new QWidget(this);
    auto* grid = new QGridLayout(settingsRow_);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    const auto addLabel = [this, grid](const QString& text, int row) {
        auto* label = new QLabel(text, this);
        label->setStyleSheet(QStringLiteral("QLabel { color: #AAAAAA; font-size: 12px; }"));
        grid->addWidget(label, row, 0);
    };
    addLabel(QStringLiteral("API Key"), 0);
    apiKeyEdit_ = new QLineEdit(settingsRow_);
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
    apiKeyEdit_->setPlaceholderText(QStringLiteral("sk-…（留空读环境变量 DEEPSEEK_API_KEY）"));
    grid->addWidget(apiKeyEdit_, 0, 1);
    addLabel(QStringLiteral("Base URL"), 1);
    baseUrlEdit_ = new QLineEdit(settingsRow_);
    baseUrlEdit_->setPlaceholderText(QStringLiteral("https://api.deepseek.com"));
    grid->addWidget(baseUrlEdit_, 1, 1);
    addLabel(QStringLiteral("模型"), 2);
    modelCombo_ = new QComboBox(settingsRow_);
    modelCombo_->setEditable(true);
    modelCombo_->addItems({ QStringLiteral("deepseek-chat"), QStringLiteral("deepseek-reasoner") });
    grid->addWidget(modelCombo_, 2, 1);
    auto* note = new QLabel(
        QStringLiteral("API Key 以明文保存在本机（QSettings）；deepseek-reasoner 不支持工具调用（仅纯对话）"),
        settingsRow_);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("QLabel { color: #888888; font-size: 11px; }"));
    grid->addWidget(note, 3, 0, 1, 2);
    settingsRow_->setVisible(false);
    root->addWidget(settingsRow_);

    // ---------- 对话记录（只读） ----------
    logView_ = new QPlainTextEdit(this);
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(500);  // 防无限增长
    logView_->setPlaceholderText(QStringLiteral("对话记录"));
    root->addWidget(logView_, 1);

    // ---------- 状态行 ----------
    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet(QStringLiteral("QLabel { color: #FF9D40; font-size: 12px; }"));
    statusLabel_->setFixedHeight(16);
    root->addWidget(statusLabel_);

    // ---------- 输入行 ----------
    auto* inputRow = new QHBoxLayout();
    inputRow->setSpacing(8);
    inputEdit_ = new QLineEdit(this);
    inputEdit_->setPlaceholderText(QStringLiteral("输入指令，如：画一只黄色的小狗（Enter 发送）"));
    inputRow->addWidget(inputEdit_, 1);
    sendButton_ = new QPushButton(QStringLiteral("发送"), this);
    sendButton_->setFixedWidth(64);
    sendButton_->setCursor(Qt::PointingHandCursor);
    sendButton_->setFocusPolicy(Qt::NoFocus);
    inputRow->addWidget(sendButton_);
    root->addLayout(inputRow);

    // ---------- 操作行：美化 / 清空 ----------
    auto* actionRow = new QHBoxLayout();
    actionRow->setSpacing(8);
    beautifyButton_ = new QPushButton(QStringLiteral("美化选中笔迹"), this);
    beautifyButton_->setCursor(Qt::PointingHandCursor);
    beautifyButton_->setFocusPolicy(Qt::NoFocus);
    beautifyButton_->setEnabled(false);
    actionRow->addWidget(beautifyButton_);
    actionRow->addStretch(1);
    clearButton_ = new QPushButton(QStringLiteral("清空对话"), this);
    clearButton_->setCursor(Qt::PointingHandCursor);
    clearButton_->setFocusPolicy(Qt::NoFocus);
    actionRow->addWidget(clearButton_);
    root->addLayout(actionRow);

    // ---------- 交互 ----------
    LoadConfigToUi();
    connect(settingsButton_, &QPushButton::clicked, this,
            [this]() { settingsRow_->setVisible(!settingsRow_->isVisible()); });
    connect(closeButton_, &QPushButton::clicked, this, &QWidget::hide);
    connect(sendButton_, &QPushButton::clicked, this, &AiPanel::OnSendClicked);
    connect(inputEdit_, &QLineEdit::returnPressed, this, &AiPanel::OnSendClicked);
    connect(beautifyButton_, &QPushButton::clicked, this, &AiPanel::OnBeautifyClicked);
    connect(clearButton_, &QPushButton::clicked, this, [this]() {
        agent_->ClearHistory();
        logView_->clear();
        AppendLine(QStringLiteral("对话已清空"));
    });
    connect(apiKeyEdit_, &QLineEdit::editingFinished, this, &AiPanel::SaveConfigFromUi);
    connect(baseUrlEdit_, &QLineEdit::editingFinished, this, &AiPanel::SaveConfigFromUi);
    connect(modelCombo_, &QComboBox::currentTextChanged, this, [this](const QString&) {
        SaveConfigFromUi();
    });

    AppendLine(QStringLiteral("AI 助手已就绪。示例："));
    AppendLine(QStringLiteral("· 画一只黄色的小狗 / 加一个 5 分钟计时器"));
    AppendLine(QStringLiteral("· 先选中我画的三角形，点“美化选中笔迹”"));
}

// 配置载入：QSettings > 环境变量 > 默认值（AiConfig::Load 已含优先级）
void AiPanel::LoadConfigToUi() {
    const AiConfig config = AiConfig::Load();
    apiKeyEdit_->setText(config.apiKey);
    baseUrlEdit_->setText(config.baseUrl);
    const int index = modelCombo_->findText(config.model);
    if (index >= 0)
        modelCombo_->setCurrentIndex(index);
    else
        modelCombo_->setEditText(config.model);
}

void AiPanel::SaveConfigFromUi() {
    AiConfig config;
    config.apiKey = apiKeyEdit_->text().trimmed();
    config.baseUrl = baseUrlEdit_->text().trimmed();
    if (config.baseUrl.isEmpty())
        config.baseUrl = QStringLiteral("https://api.deepseek.com");
    config.model = modelCombo_->currentText().trimmed();
    if (config.model.isEmpty())
        config.model = QStringLiteral("deepseek-chat");
    config.Save();
    statusLabel_->setText(QStringLiteral("配置已保存"));
}

void AiPanel::AppendLine(const QString& line) {
    logView_->appendPlainText(line);
    QScrollBar* bar = logView_->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void AiPanel::OnSendClicked() {
    if (agent_->IsBusy()) {
        agent_->Cancel();
        return;
    }
    const QString text = inputEdit_->text().trimmed();
    if (text.isEmpty())
        return;
    AppendLine(QStringLiteral("用户：") + text);
    inputEdit_->clear();
    agent_->Send(text);
}

void AiPanel::OnBeautifyClicked() {
    if (agent_->IsBusy())
        return;
    AppendLine(QStringLiteral("用户：[美化选中笔迹]"));
    agent_->BeautifySelection();
}

void AiPanel::OnBusyChanged(bool busy) {
    sendButton_->setText(busy ? QStringLiteral("停止") : QStringLiteral("发送"));
    inputEdit_->setEnabled(!busy);
    if (busy)
        statusLabel_->setText(QStringLiteral("正在处理…"));
    else
        statusLabel_->clear();
    RefreshBeautifyEnabled();
}

void AiPanel::RefreshBeautifyEnabled() {
    beautifyButton_->setEnabled(!agent_->IsBusy() && !view_.selectedElementIds().isEmpty());
}

void AiPanel::AlignBottomRight() {
    QWidget* host = parentWidget();
    if (!host)
        return;
    move(host->width() - width() - kMargin, host->height() - height() - kMargin);
}

void AiPanel::ShowAndFocus() {
    show();
    raise();
    AlignBottomRight();
    if (apiKeyEdit_->text().trimmed().isEmpty())
        settingsRow_->setVisible(true);  // 未配置 Key：直接展开设置行
    inputEdit_->setFocus();
}

void AiPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    selectionTimer_->start();
    RefreshBeautifyEnabled();
}

void AiPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    selectionTimer_->stop();
}

void AiPanel::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

void AiPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(kPanelBorder, 1.0));
    p.setBrush(kPanelBackground);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10.0, 10.0);
}

}  // namespace ai
