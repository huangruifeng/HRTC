#include "BoardToolBar.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPainter>

#include "BoardIcons.h"

// ---------- BoardToolButton ----------

BoardToolButton::BoardToolButton(const QString& text, QWidget* parent)
    : QToolButton(parent) {
    setText(text);
    setFixedSize(56, 66);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

void BoardToolButton::setPixmaps(const QPixmap& normal, const QPixmap& active) {
    normalPixmap_ = normal;
    activePixmap_ = active;
    update();
}

void BoardToolButton::setExtendPixmap(const QPixmap& extend) {
    extendPixmap_ = extend;
    update();
}

void BoardToolButton::setDisabledPixmap(const QPixmap& disabled) {
    disabledPixmap_ = disabled;
    update();
}

void BoardToolButton::setActiveState(bool on) {
    if (activeState_ == on)
        return;
    activeState_ = on;
    update();
}

void BoardToolButton::setExtendState(bool on) {
    if (extendState_ == on)
        return;
    extendState_ = on;
    update();
}

void BoardToolButton::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const bool checked = isCheckable() && isChecked();
    const bool highlight = isDown() || activeState_ || checked;
    QPixmap icon = highlight ? activePixmap_ : normalPixmap_;
    // 设置面板展开中：换展开态图（对应参考 Extend 触发器，优先于选中态）
    if (checked && extendState_ && !extendPixmap_.isNull())
        icon = extendPixmap_;
    if (!isEnabled() && !disabledPixmap_.isNull())
        icon = disabledPixmap_;
    if (!icon.isNull())
        p.drawPixmap(QRect(4, 1, 48, 48), icon);

    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);
    const QColor textColor = !isEnabled() ? QColor(0x5A, 0x5A, 0x5A)
                             : highlight  ? QColor(0xE0, 0xE0, 0xE0)
                                          : QColor(0x99, 0x99, 0x99);
    p.setPen(textColor);
    p.drawText(QRect(0, 49, width(), 16), Qt::AlignHCenter | Qt::AlignTop, text());
}

// ---------- BoardToolBar ----------

BoardToolBar::BoardToolBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("boardToolBar"));
    setFixedHeight(72);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(6, 3, 6, 3);
    root->setSpacing(0);

    // ---------- 左侧工具组（互斥选中）----------
    toolGroup_ = new QButtonGroup(this);
    toolGroup_->setExclusive(true);

    auto* leftLayout = new QHBoxLayout;
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    const auto addTool = [&](BoardView::Tool tool, const QString& text,
                             const QPixmap& normal, const QPixmap& active,
                             const QPixmap& extend = QPixmap()) {
        BoardToolButton* button = createButton(text, normal, active, true, extend);
        toolGroup_->addButton(button);
        toolButtons_.insert(static_cast<int>(tool), button);
        leftLayout->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, tool]() {
            emit toolSelected(tool);
        });
    };

    addTool(BoardView::Tool::Pen, QStringLiteral("书写"),
            QPixmap(QStringLiteral(":/icons/pen_normal.png")),
            QPixmap(QStringLiteral(":/icons/pen_pressed.png")),
            QPixmap(QStringLiteral(":/icons/pen_check_pressed.png")));
    addTool(BoardView::Tool::Eraser, QStringLiteral("擦除"),
            QPixmap(QStringLiteral(":/icons/eraser_normal.png")),
            QPixmap(QStringLiteral(":/icons/eraser_pressed.png")),
            QPixmap(QStringLiteral(":/icons/eraser_check_pressed.png")));
    addTool(BoardView::Tool::Select, QStringLiteral("选择"),
            QPixmap(QStringLiteral(":/icons/select_normal.png")),
            QPixmap(QStringLiteral(":/icons/select_pressed.png")));
    addTool(BoardView::Tool::Lasso, QStringLiteral("套索"),
            BoardIcons::pixmap(BoardIcons::Glyph::Lasso, false),
            BoardIcons::pixmap(BoardIcons::Glyph::Lasso, true));

    // 其他：图形/导图/表格/文字/小工具汇总入口（非 checkable，仿 moreButton_；
    // 点击弹出 OtherToolsPanel，激活态由 MainWindow 统一控制）
    otherButton_ = createButton(
        QStringLiteral("其他"),
        BoardIcons::pixmap(BoardIcons::Glyph::Other, false),
        BoardIcons::pixmap(BoardIcons::Glyph::Other, true), false);
    leftLayout->addWidget(otherButton_);
    connect(otherButton_, &QToolButton::clicked, this, &BoardToolBar::otherRequested);

    addTool(BoardView::Tool::Pan, QStringLiteral("抓手"),
            BoardIcons::pixmap(BoardIcons::Glyph::Hand, false),
            BoardIcons::pixmap(BoardIcons::Glyph::Hand, true));

    // ---------- 中部功能组 ----------
    auto* midLayout = new QHBoxLayout;
    midLayout->setContentsMargins(0, 0, 0, 0);
    midLayout->setSpacing(6);

    const auto addAction = [&](BoardToolButton*& slot, const QString& text,
                               const QPixmap& normal, const QPixmap& active) {
        slot = createButton(text, normal, active, false);
        midLayout->addWidget(slot);
    };

    BoardToolButton* undoButton = nullptr;
    BoardToolButton* redoButton = nullptr;
    BoardToolButton* zoomOutButton = nullptr;
    BoardToolButton* zoomInButton = nullptr;
    addAction(undoButton, QStringLiteral("撤销"),
              BoardIcons::pixmap(BoardIcons::Glyph::Undo, false),
              BoardIcons::pixmap(BoardIcons::Glyph::Undo, true));
    addAction(redoButton, QStringLiteral("重做"),
              BoardIcons::pixmap(BoardIcons::Glyph::Redo, false),
              BoardIcons::pixmap(BoardIcons::Glyph::Redo, true));
    addAction(zoomOutButton, QStringLiteral("缩小"),
              BoardIcons::pixmap(BoardIcons::Glyph::ZoomOut, false),
              BoardIcons::pixmap(BoardIcons::Glyph::ZoomOut, true));
    addAction(zoomResetButton_, QStringLiteral("100%"),
              BoardIcons::pixmap(BoardIcons::Glyph::ZoomReset, false),
              BoardIcons::pixmap(BoardIcons::Glyph::ZoomReset, true));
    addAction(zoomInButton, QStringLiteral("放大"),
              BoardIcons::pixmap(BoardIcons::Glyph::ZoomIn, false),
              BoardIcons::pixmap(BoardIcons::Glyph::ZoomIn, true));

    connect(undoButton, &QToolButton::clicked, this, &BoardToolBar::undoRequested);
    connect(redoButton, &QToolButton::clicked, this, &BoardToolBar::redoRequested);
    connect(zoomOutButton, &QToolButton::clicked, this, &BoardToolBar::zoomOutRequested);
    connect(zoomResetButton_, &QToolButton::clicked, this, &BoardToolBar::zoomResetRequested);
    connect(zoomInButton, &QToolButton::clicked, this, &BoardToolBar::zoomInRequested);

    // ---------- 右侧页面组 ----------
    auto* rightLayout = new QHBoxLayout;
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    BoardToolButton* addPageButton = createButton(
        QStringLiteral("添加页"),
        QPixmap(QStringLiteral(":/icons/add_page_normal.png")),
        QPixmap(QStringLiteral(":/icons/add_page_pressed.png")), false);
    addPageButton->setDisabledPixmap(QPixmap(QStringLiteral(":/icons/add_page_disabled.png")));
    rightLayout->addWidget(addPageButton);
    addPageButton_ = addPageButton;

    prevButton_ = createButton(
        QStringLiteral("上一页"),
        QPixmap(QStringLiteral(":/icons/prev_normal.png")),
        QPixmap(QStringLiteral(":/icons/prev_pressed.png")), false);
    prevButton_->setDisabledPixmap(QPixmap(QStringLiteral(":/icons/prev_disabled.png")));
    rightLayout->addWidget(prevButton_);

    pageButton_ = createButton(
        QStringLiteral("1/1"),
        QPixmap(QStringLiteral(":/icons/page_normal.png")),
        QPixmap(QStringLiteral(":/icons/page_pressed.png")), false);
    rightLayout->addWidget(pageButton_);

    nextButton_ = createButton(
        QStringLiteral("下一页"),
        QPixmap(QStringLiteral(":/icons/next_normal.png")),
        QPixmap(QStringLiteral(":/icons/next_pressed.png")), false);
    nextButton_->setDisabledPixmap(QPixmap(QStringLiteral(":/icons/next_disabled.png")));
    rightLayout->addWidget(nextButton_);

    // 更多：面板风格应用级入口（保存/打开/设置），置于工具栏最右端
    moreButton_ = createButton(
        QStringLiteral("更多"),
        BoardIcons::pixmap(BoardIcons::Glyph::More, false),
        BoardIcons::pixmap(BoardIcons::Glyph::More, true), false);
    rightLayout->addWidget(moreButton_);

    connect(addPageButton, &QToolButton::clicked, this, &BoardToolBar::addPageRequested);
    connect(prevButton_, &QToolButton::clicked, this, &BoardToolBar::prevPageRequested);
    connect(nextButton_, &QToolButton::clicked, this, &BoardToolBar::nextPageRequested);
    connect(moreButton_, &QToolButton::clicked, this, &BoardToolBar::moreRequested);
    connect(pageButton_, &QToolButton::clicked, this, [this]() {
        emit pagePanelRequested();
    });

    // ---------- 组装 ----------
    root->addLayout(leftLayout);
    root->addSpacing(12);
    root->addLayout(midLayout);
    root->addSpacing(12);
    root->addLayout(rightLayout);
}

// 自绘圆角背景：QSS border-radius 不会把圆角外区域变透明（四角会残留黑块），
// 改为手绘圆角矩形、圆角外不绘制像素，透出下方画布内容
void BoardToolBar::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(35, 39, 42, 204));  // rgba(35,39,42,0.8)，同参考 DisplayToolBar
    p.drawRoundedRect(QRectF(rect()), 8.0, 8.0);
}

BoardToolButton* BoardToolBar::createButton(const QString& text, const QPixmap& normal,
                                            const QPixmap& active, bool checkable,
                                            const QPixmap& extend) {
    auto* button = new BoardToolButton(text, this);
    button->setPixmaps(normal, active);
    if (!extend.isNull())
        button->setExtendPixmap(extend);
    button->setCheckable(checkable);
    return button;
}

// 同步工具选中态：工具按钮存在时点亮；图形/导图/表格/文字/小工具已移入
// "其他"面板（无对应按钮）→ 清空工具组选中态（避免残留旧工具高亮）
void BoardToolBar::setCurrentTool(BoardView::Tool tool) {
    BoardToolButton* button = toolButtons_.value(static_cast<int>(tool), nullptr);
    if (button) {
        if (!button->isChecked())
            button->setChecked(true);
        return;
    }
    if (QAbstractButton* checked = toolGroup_->checkedButton()) {
        toolGroup_->setExclusive(false);
        checked->setChecked(false);
        toolGroup_->setExclusive(true);
    }
}

void BoardToolBar::setToolPanelExtended(BoardView::Tool tool, bool extended) {
    BoardToolButton* button = toolButtons_.value(static_cast<int>(tool), nullptr);
    if (button)
        button->setExtendState(extended);
}

void BoardToolBar::setZoomPercent(qreal percent) {
    zoomResetButton_->setText(QStringLiteral("%1%").arg(qRound(percent)));
}

void BoardToolBar::setPageInfo(int index, int total) {
    pageButton_->setText(QStringLiteral("%1/%2").arg(index).arg(total));
}

void BoardToolBar::setPrevNextEnabled(bool prev, bool next) {
    prevButton_->setEnabled(prev);
    nextButton_->setEnabled(next);
}

void BoardToolBar::setAddPageEnabled(bool enabled) {
    addPageButton_->setEnabled(enabled);
}

void BoardToolBar::setPageButtonActive(bool active) {
    pageButton_->setActiveState(active);
}

void BoardToolBar::setMoreButtonActive(bool active) {
    moreButton_->setActiveState(active);
}

void BoardToolBar::setOtherButtonActive(bool active) {
    otherButton_->setActiveState(active);
}

QRect BoardToolBar::pageButtonGlobalRect() const {
    return QRect(pageButton_->mapToGlobal(QPoint(0, 0)), pageButton_->size());
}

QRect BoardToolBar::moreButtonGlobalRect() const {
    return QRect(moreButton_->mapToGlobal(QPoint(0, 0)), moreButton_->size());
}

QRect BoardToolBar::otherButtonGlobalRect() const {
    return QRect(otherButton_->mapToGlobal(QPoint(0, 0)), otherButton_->size());
}

QRect BoardToolBar::toolButtonGlobalRect(BoardView::Tool tool) const {
    BoardToolButton* button = toolButtons_.value(static_cast<int>(tool), nullptr);
    if (!button)
        return QRect();
    return QRect(button->mapToGlobal(QPoint(0, 0)), button->size());
}
