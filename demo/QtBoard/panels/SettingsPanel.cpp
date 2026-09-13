#include "SettingsPanel.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>

#include <functional>

#include "BoardView.h"

namespace {

// 面板尺寸（与 SlideManagerPanel 同宽）
constexpr int kPanelW = 256;
constexpr int kTitleHeight = 34;  // 标题区："设置" 20px 文字

// 背景缩略图（16:9 原图比例，2 列）
constexpr int kThumbW = 111;
constexpr int kThumbH = 62;
constexpr int kGridSpacing = 10;
constexpr int kSectionH = 20;   // 分区标题
constexpr int kButtonH = 30;    // "浏览…" / 模式切换按钮
constexpr int kMaxItems = 12;   // 最多展示背景数（含内置默认）
// 显示模式分区总高：标题 + 间距 8 + 按钮行 + 间距 10
constexpr int kModeSectionH = kSectionH + 8 + kButtonH + 10;

// 颜色常量（同 SlideManagerPanel）
const QColor kMenuBackground(0x23, 0x27, 0x2A, 0xCC);  // #CC23272A
const QColor kMenuBorder(0x33, 0x63, 0x6C, 0x73);      // #33636C73
const QColor kItemBorder(0x3F, 0x44, 0x48);            // #3F4448
const QColor kSelectOrange(0xFF, 0x6B, 0x00);          // #FF6B00 选中
const QColor kSectionText(0x99, 0x99, 0x99);           // 分区标题灰

// 内置默认背景（参考 Image.Background.Default：board_bg_default.png）
const char* const kDefaultBgKey = ":/images/board_bg_default.png";
// 原色背景（旧版纯墨绿底色）：以单像素纯色图作背景源，画布拉伸铺满即纯色
const char* const kSolidBgKey = "solid://default";

// 显示模式分段按钮样式（选中橙色 = 主题选中色）
const char* const kModeButtonQss =
    "QPushButton { background: #32383B; border: 1px solid #3F4448; border-radius: 4px;"
    "              color: #CCCCCC; font-size: 13px; }"
    "QPushButton:hover { background: #3A4045; }"
    "QPushButton:pressed { background: #2A2F32; }"
    "QPushButton:checked { background: #FF6B00; border: 1px solid #FF6B00;"
    "                      color: #FFFFFF; }";

}  // namespace

// 背景缩略图：背景图等比缩放 + 圆角 4 裁剪；1px 灰框（选中 2px 橙框），悬停微亮
class SettingsPanel::BackgroundThumb : public QWidget {
public:
    BackgroundThumb(const QString& key, const QPixmap& thumb, bool selected, QWidget* parent)
        : QWidget(parent), key_(key), thumb_(thumb), selected_(selected) {
        setFixedSize(kThumbW, kThumbH);
        setCursor(Qt::PointingHandCursor);
    }

    const QString& key() const { return key_; }

    void setSelected(bool on) {
        if (selected_ == on)
            return;
        selected_ = on;
        update();
    }

    std::function<void()> onClick;

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(box, 4.0, 4.0);
        p.setClipPath(clip);
        p.drawPixmap(0, 0, thumb_);
        if (hovered_ && !selected_) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 14));
            p.drawRect(rect());
        }
        p.setClipping(false);

        // 边框（选中橙色 2px，未选中灰 1px）
        p.setBrush(Qt::NoBrush);
        if (selected_) {
            p.setPen(QPen(kSelectOrange, 2.0));
            p.drawRoundedRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0), 4.0, 4.0);
        } else {
            p.setPen(QPen(kItemBorder, 1.0));
            p.drawRoundedRect(box, 4.0, 4.0);
        }
    }

    void enterEvent(QEvent* event) override {
        QWidget::enterEvent(event);
        hovered_ = true;
        update();
    }

    void leaveEvent(QEvent* event) override {
        QWidget::leaveEvent(event);
        hovered_ = false;
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        if (rect().contains(event->pos()) && onClick)
            onClick();
    }

private:
    QString key_;
    QPixmap thumb_;
    bool selected_ = false;
    bool hovered_ = false;
};

// ---------- SettingsPanel ----------

SettingsPanel::SettingsPanel(QWidget* parent) : QWidget(parent) {
    // Windows 下 WA_TranslucentBackground 需要 FramelessWindowHint 才生效，
    // 否则圆角外未绘制区域会被渲染成黑块
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(kPanelW, kTitleHeight + kModeSectionH + kSectionH + 8 + kThumbH + 10 + kButtonH + 12);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, kTitleHeight, 12, 12);
    root->setSpacing(0);

    // 分区标题（不用 QSS，避免分层弹窗内透明背景擦穿）
    const auto makeSectionLabel = [this](const QString& text) {
        auto* label = new QLabel(text, this);
        QFont labelFont = label->font();
        labelFont.setPixelSize(13);
        label->setFont(labelFont);
        QPalette labelPalette = label->palette();
        labelPalette.setColor(QPalette::WindowText, kSectionText);
        label->setPalette(labelPalette);
        label->setFixedHeight(kSectionH);
        return label;
    };

    // ---- 显示模式：黑板模式（全屏 16:9）/ 窗口模式 ----
    root->addWidget(makeSectionLabel(QStringLiteral("显示模式")));
    root->addSpacing(8);
    modeGroup_ = new QButtonGroup(this);
    modeGroup_->setExclusive(true);
    boardModeButton_ = new QPushButton(QStringLiteral("黑板模式"), this);
    windowModeButton_ = new QPushButton(QStringLiteral("窗口模式"), this);
    modeGroup_->addButton(boardModeButton_);
    modeGroup_->addButton(windowModeButton_);
    for (QPushButton* btn : {boardModeButton_, windowModeButton_}) {
        btn->setCheckable(true);
        btn->setFixedHeight(kButtonH);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setStyleSheet(QString::fromLatin1(kModeButtonQss));
    }
    windowModeButton_->setChecked(true);  // 默认窗口模式（保持现状）
    connect(boardModeButton_, &QPushButton::clicked, this,
            [this]() { emit displayModeChanged(true); });
    connect(windowModeButton_, &QPushButton::clicked, this,
            [this]() { emit displayModeChanged(false); });
    auto* modeRow = new QHBoxLayout;
    modeRow->setContentsMargins(0, 0, 0, 0);
    modeRow->setSpacing(kGridSpacing);
    modeRow->addWidget(boardModeButton_);
    modeRow->addWidget(windowModeButton_);
    root->addLayout(modeRow);
    root->addSpacing(10);

    // ---- 黑板背景 ----
    root->addWidget(makeSectionLabel(QStringLiteral("黑板背景")));
    root->addSpacing(8);

    grid_ = new QGridLayout;
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(kGridSpacing);
    grid_->setVerticalSpacing(kGridSpacing);
    root->addLayout(grid_);
    root->addSpacing(10);

    // 自定义背景："从文件选择…"（QSS 底色必须不透明——分层弹窗内透明区域会擦穿）
    auto* browse = new QPushButton(QStringLiteral("浏览…"), this);
    browse->setFixedHeight(kButtonH);
    browse->setCursor(Qt::PointingHandCursor);
    browse->setFocusPolicy(Qt::NoFocus);
    browse->setStyleSheet(QStringLiteral(
        "QPushButton { background: #32383B; border: 1px solid #3F4448; border-radius: 4px;"
        "              color: #CCCCCC; font-size: 13px; }"
        "QPushButton:hover { background: #3A4045; }"
        "QPushButton:pressed { background: #2A2F32; }"));
    connect(browse, &QPushButton::clicked, this, &SettingsPanel::onBrowse);
    root->addWidget(browse);
}

// 重建背景网格：内置默认图 + 原色 + exe 同级 Backgrounds 目录扫描（每次展开刷新）
void SettingsPanel::rebuild(const QString& currentKey) {
    selectedKey_ = currentKey;
    clearItems();

    // 内置默认背景（固定第一项）
    const QString defaultKey = QString::fromLatin1(kDefaultBgKey);
    addItem(defaultKey, QPixmap(defaultKey));

    // 原色背景（固定第二项）：旧版纯墨绿底色
    QPixmap solid(1, 1);
    solid.fill(BoardView::boardDefaultColor());
    addItem(QString::fromLatin1(kSolidBgKey), solid);

    // 外置背景目录：exe 同级 Backgrounds（参考 MaxWhiteboard AppPath.BackgroundPath）
    QDir dir(QCoreApplication::applicationDirPath() + QStringLiteral("/Backgrounds"));
    if (dir.exists()) {
        const QStringList files = dir.entryList(
            QStringList() << QStringLiteral("*.png") << QStringLiteral("*.jpg")
                          << QStringLiteral("*.jpeg") << QStringLiteral("*.bmp"),
            QDir::Files, QDir::Name);
        for (const QString& name : files) {
            if (thumbs_.size() >= kMaxItems)
                break;
            const QString path = dir.absoluteFilePath(name);
            addItem(path, QPixmap(path));
        }
    }
    updatePanelSize();
}

void SettingsPanel::clearItems() {
    for (BackgroundThumb* thumb : thumbs_) {
        grid_->removeWidget(thumb);
        thumb->hide();
        thumb->deleteLater();
    }
    thumbs_.clear();
    sources_.clear();
}

// 追加一个背景项（重复 key / 空图 / 超上限时忽略）
void SettingsPanel::addItem(const QString& key, const QPixmap& source) {
    if (source.isNull() || sources_.contains(key) || thumbs_.size() >= kMaxItems)
        return;
    sources_.insert(key, source);

    const int index = thumbs_.size();
    auto* thumb = new BackgroundThumb(
        key, source.scaled(kThumbW, kThumbH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
        key == selectedKey_, this);
    thumb->onClick = [this, key]() {
        selectKey(key);
    };
    grid_->addWidget(thumb, index / 2, index % 2);
    thumbs_.append(thumb);
}

// 点击背景：切换选中态并请求应用（点击当前背景保持面板，参考页数面板行为）
void SettingsPanel::selectKey(const QString& key) {
    if (key == selectedKey_)
        return;
    selectedKey_ = key;
    for (BackgroundThumb* thumb : thumbs_)
        thumb->setSelected(thumb->key() == key);
    emit backgroundSelected(key, sources_.value(key));
}

void SettingsPanel::updatePanelSize() {
    const int rows = qMax(1, (thumbs_.size() + 1) / 2);
    const int gridH = rows * kThumbH + (rows - 1) * kGridSpacing;
    setFixedSize(kPanelW,
                 kTitleHeight + kModeSectionH + kSectionH + 8 + gridH + 10 + kButtonH + 12);
}

// 更新显示模式选中态（由主窗口在模式切换 / 恢复时同步；不发出信号）
void SettingsPanel::setDisplayMode(bool boardMode) {
    (boardMode ? boardModeButton_ : windowModeButton_)->setChecked(true);
}

// "从文件选择…"：任选图片作为自定义背景，加入网格并立即应用
void SettingsPanel::onBrowse() {
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择黑板背景"), QString(),
        QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp)"));
    if (file.isEmpty())
        return;
    const QPixmap source(file);
    if (source.isNull())
        return;
    if (!sources_.contains(file)) {
        addItem(file, source);
        updatePanelSize();
    }
    selectKey(file);
}

void SettingsPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // 圆角背景 + 边框（同 SlideManagerPanel 风格）
    p.setPen(QPen(kMenuBorder, 1.0));
    p.setBrush(kMenuBackground);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);

    // 标题"设置"（20px #EEEEEE）
    QFont f = font();
    f.setPixelSize(20);
    p.setFont(f);
    p.setPen(QColor(0xEE, 0xEE, 0xEE));
    p.drawText(QRect(16, 4, width() - 32, 26), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("设置"));
}

void SettingsPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    emit closed();
}
