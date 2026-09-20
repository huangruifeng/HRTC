#include "PageRail.h"

#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHideEvent>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

#include "BoardIcons.h"
#include "BoardView.h"

namespace {

// ---------- 标签 ----------
const QColor kTagBg(35, 39, 42, 204);      // 同工具栏背景
const QColor kTagActive(0xFF, 0x7D, 0x00);  // 展开中描边（同激活橙）

// ---------- 面板（参考 SlideManagerPanel 风格） ----------
constexpr int kPanelW = 256;
constexpr int kTitleHeight = 34;
constexpr int kNavHeight = 60;       // 底部按钮行（44px 图标钮 + 上下边距）
constexpr int kMaxListHeight = 540;  // 缩略图列表最大高度（超出滚动）

constexpr int kItemW = 224;
constexpr int kItemH = 156;
constexpr int kItemSpacing = 12;
constexpr int kHeaderH = 28;
constexpr int kThumbW = 222;
constexpr int kThumbH = 126;
constexpr int kDeleteSize = 24;

const QColor kMenuBackground(0x23, 0x27, 0x2A, 0xCC);
const QColor kMenuBorder(0x33, 0x63, 0x6C, 0x73);
const QColor kItemBorder(0x3F, 0x44, 0x48);
const QColor kItemHeader(0x3F, 0x44, 0x48);
const QColor kSelectOrange(0xFF, 0x6B, 0x00);

// 通用图标钮：QPixmap 两态（normal/active）+ 禁用态；hover 高亮、点击回调
class IconButton : public QWidget {
public:
    IconButton(const QPixmap& normal, const QPixmap& active, QWidget* parent)
        : QWidget(parent), normal_(normal), active_(active) {
        setFixedSize(normal.size().isEmpty() ? QSize(24, 24) : normal.size());
        setCursor(Qt::PointingHandCursor);
    }

    void setDisabledPixmap(const QPixmap& disabled) { disabled_ = disabled; update(); }
    void setActionEnabled(bool on) { actionEnabled_ = on; update(); }
    std::function<void()> onClick;

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        // 绘制时缩放到控件尺寸（图标源尺寸与控件尺寸不一致时自适应，如 96px 图标 → 24px 钮）
        if (!actionEnabled_) {
            if (!disabled_.isNull()) {
                p.drawPixmap(rect(), disabled_);
                return;
            }
            // 无专用禁用图：半透明绘制普通图
            p.setOpacity(0.35);
            p.drawPixmap(rect(), normal_);
            return;
        }
        p.drawPixmap(rect(), hovered_ || pressed_ ? active_ : normal_);
    }

    void enterEvent(QEvent* event) override {
        QWidget::enterEvent(event);
        hovered_ = true;
        update();
    }

    void leaveEvent(QEvent* event) override {
        QWidget::leaveEvent(event);
        hovered_ = false;
        pressed_ = false;
        update();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        pressed_ = true;
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        const bool inside = rect().contains(event->pos());
        pressed_ = false;
        update();
        if (inside && actionEnabled_ && onClick)
            onClick();
    }

private:
    QPixmap normal_;
    QPixmap active_;
    QPixmap disabled_;
    bool hovered_ = false;
    bool pressed_ = false;
    bool actionEnabled_ = true;
};

QPixmap scaledIcon(const QString& res, int size) {
    return QPixmap(res).scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// 缩略图条目：头部条（序号 + 右上复制/删除按钮常显，复制在删除左边）+ 缩略图；
// 选中页橙色；按钮 hover 高亮（灰 → 橙）
class RailItemWidget : public QWidget {
public:
    RailItemWidget(int index, const QPixmap& thumb, bool selected, QWidget* parent)
        : QWidget(parent), index_(index), thumb_(thumb), selected_(selected) {
        setFixedSize(kItemW, kItemH);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);

        copyButton_ = new IconButton(BoardIcons::pixmap(BoardIcons::Glyph::Copy, false),
                                     BoardIcons::pixmap(BoardIcons::Glyph::Copy, true), this);
        copyButton_->setFixedSize(kDeleteSize, kDeleteSize);
        copyButton_->move(kItemW - 8 - kDeleteSize - kDeleteSize - 4, (kHeaderH - kDeleteSize) / 2);
        copyButton_->onClick = [this]() {
            if (onCopy)
                onCopy();
        };  // 常显（同删除按钮，hover 高亮）

        deleteButton_ = new IconButton(scaledIcon(QStringLiteral(":/icons/page_delete_normal.png"), kDeleteSize),
                                       scaledIcon(QStringLiteral(":/icons/page_delete_pressed.png"), kDeleteSize), this);
        deleteButton_->setFixedSize(kDeleteSize, kDeleteSize);
        deleteButton_->move(kItemW - 8 - kDeleteSize, (kHeaderH - kDeleteSize) / 2);
        deleteButton_->onClick = [this]() {
            if (onDelete)
                onDelete();
        };
    }

    void setDeleteVisible(bool on) { deleteButton_->setVisible(on); }
    void setCopyEnabled(bool on) { copyButton_->setActionEnabled(on); }

    std::function<void()> onActivated;
    std::function<void()> onDelete;
    std::function<void()> onCopy;

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(box, 2.0, 2.0);
        p.setClipPath(clip);

        p.drawPixmap(QPoint(1, kHeaderH + 1), thumb_);
        p.setPen(Qt::NoPen);
        p.setBrush(selected_ ? kSelectOrange : kItemHeader);
        p.drawRect(QRectF(1.0, 1.0, kItemW - 2.0, kHeaderH));
        if (pressed_) {
            p.setBrush(QColor(255, 255, 255, 18));
            p.drawRect(QRectF(1.0, 1.0, kItemW - 2.0, kItemH - 2.0));
        }
        p.setClipping(false);

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(selected_ ? kSelectOrange : kItemBorder, 1.0));
        p.drawRoundedRect(box, 2.0, 2.0);

        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(QColor(0xFF, 0xFF, 0xFF));
        p.drawText(QRect(9, 1, kItemW - kDeleteSize * 2 - 32, kHeaderH),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::number(index_ + 1));
    }

    void enterEvent(QEvent* event) override {
        QWidget::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent* event) override {
        QWidget::leaveEvent(event);
        update();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        pressed_ = true;
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        const bool inside = rect().contains(event->pos());
        pressed_ = false;
        update();
        if (inside && onActivated)
            onActivated();
    }

private:
    int index_ = 0;
    QPixmap thumb_;
    bool selected_ = false;
    bool pressed_ = false;
    IconButton* copyButton_ = nullptr;
    IconButton* deleteButton_ = nullptr;
};

}  // namespace

// ---------- 展开面板 ----------

// Qt::Popup 圆角面板：标题"画板" + 可滚动缩略图列表 + 底部按钮行
//（上一页 / 添加页 / 下一页）。点击外部自动关闭（hideEvent 通知 PageRail）。
class PageRail::Panel : public QWidget {
public:
    explicit Panel(QWidget* parent) : QWidget(parent) {
        // Windows 下 WA_TranslucentBackground 需要 FramelessWindowHint 才生效
        setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedWidth(kPanelW);

        auto* root = new QVBoxLayout(this);
        // 左右 1px、底部 8px 内缩（滚动区不覆盖面板边框与圆角）
        root->setContentsMargins(1, kTitleHeight, 1, 8);
        root->setSpacing(0);

        scrollArea_ = new QScrollArea(this);
        scrollArea_->setFrameShape(QFrame::NoFrame);
        scrollArea_->setWidgetResizable(true);
        scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scrollArea_->viewport()->setAutoFillBackground(false);
        // 列表底色：面板色叠画布的等效不透明色（透明子区域会"擦穿"分层弹窗）
        scrollArea_->setStyleSheet(QStringLiteral(
            "QScrollArea { background: #202728; }"
            "QScrollArea > QWidget { background: #202728; }"
            "QScrollArea > QWidget > QWidget { background: #202728; }"));
        // 滚动条：窄轨道圆角半透滑块（直接设在滚动条上，设在 QScrollArea 上不生效）
        scrollArea_->verticalScrollBar()->setStyleSheet(QStringLiteral(
            "QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }"
            "QScrollBar::handle:vertical {"
            "  background: rgba(255, 255, 255, 0.28);"
            "  border-radius: 4px;"
            "  min-height: 40px;"
            "  margin: 0 2px;"
            "}"
            "QScrollBar::handle:vertical:hover { background: rgba(255, 255, 255, 0.45); }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"));

        listContent_ = new QWidget;
        listLayout_ = new QVBoxLayout(listContent_);
        listLayout_->setContentsMargins(12, 0, 12, 12);
        listLayout_->setSpacing(kItemSpacing);
        scrollArea_->setWidget(listContent_);
        root->addWidget(scrollArea_, 1);

        // 底部按钮行：上一页 / 添加页 / 下一页（居中排列）
        auto* navRow = new QWidget;
        navRow->setFixedHeight(kNavHeight);
        auto* navLayout = new QHBoxLayout(navRow);
        navLayout->setContentsMargins(12, 8, 12, 8);
        navLayout->setSpacing(28);
        navLayout->addStretch(1);  // 前后各一 stretch：三钮整体居中

        const auto navButton = [navLayout](const QString& normal, const QString& pressed,
                                           const QString& disabled) {
            auto* button = new IconButton(scaledIcon(normal, 44), scaledIcon(pressed, 44), nullptr);
            button->setDisabledPixmap(scaledIcon(disabled, 44));
            navLayout->addWidget(button);
            return button;
        };
        prevButton_ = navButton(QStringLiteral(":/icons/prev_normal.png"),
                                QStringLiteral(":/icons/prev_pressed.png"),
                                QStringLiteral(":/icons/prev_disabled.png"));
        addButton_ = navButton(QStringLiteral(":/icons/add_page_normal.png"),
                               QStringLiteral(":/icons/add_page_pressed.png"),
                               QStringLiteral(":/icons/add_page_disabled.png"));
        nextButton_ = navButton(QStringLiteral(":/icons/next_normal.png"),
                                QStringLiteral(":/icons/next_pressed.png"),
                                QStringLiteral(":/icons/next_disabled.png"));
        navLayout->addStretch(1);  // 尾部 stretch：与头部对半分剩余空间，三钮居中

        prevButton_->onClick = [this]() { if (onPrev) onPrev(); };
        addButton_->onClick = [this]() { if (onAdd) onAdd(); };
        nextButton_->onClick = [this]() { if (onNext) onNext(); };
        root->addWidget(navRow);
    }

    void rebuild(const std::vector<whiteboard::Page>& pages, int currentIndex,
                 const QPixmap& background) {
        while (QLayoutItem* item = listLayout_->takeAt(0)) {
            if (QWidget* w = item->widget()) {
                w->hide();
                w->deleteLater();
            }
            delete item;
        }
        currentItem_ = nullptr;
        index_ = currentIndex;
        pageCount_ = static_cast<int>(pages.size());
        const bool copyEnabled = pageCount_ < BoardView::kMaxPages;

        for (int i = 0; i < pageCount_; ++i) {
            const QPixmap thumb =
                BoardView::renderPageThumbnail(pages[i], QSize(kThumbW, kThumbH), background);
            auto* item = new RailItemWidget(i, thumb, i == currentIndex, listContent_);
            item->setDeleteVisible(pageCount_ > 1);  // 仅 1 页时隐藏删除
            item->setCopyEnabled(copyEnabled);       // 页数达上限时禁用复制
            item->onActivated = [this, i]() {
                if (i == index_)
                    return;  // 点击当前页：保持面板
                if (onPageActivated)
                    onPageActivated(i);
            };
            item->onDelete = [this, i]() {
                if (pageCount_ > 1 && onPageDelete)
                    onPageDelete(i);
            };
            item->onCopy = [this, i]() {
                if (pageCount_ < BoardView::kMaxPages && onPageCopy)
                    onPageCopy(i);
            };
            listLayout_->addWidget(item);
            if (i == currentIndex)
                currentItem_ = item;
        }
        listLayout_->addStretch(1);

        // 高度 = 标题 + min(条目区, 列表上限) + 底部按钮行
        const int listHeight = pageCount_ * (kItemH + kItemSpacing) + 12;
        setFixedSize(kPanelW,
                     kTitleHeight + qMin(listHeight, kMaxListHeight) + kNavHeight);
        updateNavButtons();
    }

    // 按钮可用态（页码变化时由 PageRail 同步）
    void updateNavButtons() {
        prevButton_->setActionEnabled(index_ > 0);
        nextButton_->setActionEnabled(index_ >= 0 && index_ < pageCount_ - 1);
        addButton_->setActionEnabled(pageCount_ < BoardView::kMaxPages);
    }

    void setIndex(int index) {
        index_ = index;
        updateNavButtons();
    }

    std::function<void(int)> onPageActivated;
    std::function<void(int)> onPageDelete;
    std::function<void(int)> onPageCopy;
    std::function<void()> onPrev;
    std::function<void()> onNext;
    std::function<void()> onAdd;
    std::function<void()> onClosed;

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);

        p.setPen(QPen(kMenuBorder, 1.0));
        p.setBrush(kMenuBackground);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);

        QFont f = font();
        f.setPixelSize(20);
        p.setFont(f);
        p.setPen(QColor(0xEE, 0xEE, 0xEE));
        p.drawText(QRect(16, 4, width() - 32, 26), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("画板"));
    }

    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        QTimer::singleShot(0, this, [this]() {
            if (currentItem_)
                scrollArea_->ensureWidgetVisible(currentItem_, 0, 36);
        });
    }

    void hideEvent(QHideEvent* event) override {
        QWidget::hideEvent(event);
        if (onClosed)
            onClosed();
    }

private:
    QScrollArea* scrollArea_ = nullptr;
    QWidget* listContent_ = nullptr;
    QVBoxLayout* listLayout_ = nullptr;
    QWidget* currentItem_ = nullptr;
    IconButton* prevButton_ = nullptr;
    IconButton* addButton_ = nullptr;
    IconButton* nextButton_ = nullptr;
    int index_ = 0;
    int pageCount_ = 1;
};

// ---------- PageRail 标签 ----------

PageRail::PageRail(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("pageRail"));
    setFixedSize(kTagW, kTagH);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

void PageRail::setPageInfo(int index, int total) {
    index_ = qMax(0, index);
    total_ = qMax(1, total);
    if (panel_)
        panel_->setIndex(index_);
    update();
}

void PageRail::setBounds(const QRect& rect) {
    bounds_ = rect;
    // 首次有效几何：贴画布左缘垂直居中（构造期矩形尺寸未就绪，高度足够才定位）
    if (!positioned_ && rect.isValid() && rect.height() >= kTagH * 2) {
        positioned_ = true;
        move(rect.left(), rect.center().y() - kTagH / 2);
        return;
    }
    move(clampedPos(QPointF(geometry().center())));
}

bool PageRail::expanded() const { return panel_ && panel_->isVisible(); }

void PageRail::setExpanded(bool on) {
    if (on) {
        if (!panel_) {
            panel_ = new Panel(this);
            panel_->onPageActivated = [this](int index) { emit pageActivated(index); };
            panel_->onPageDelete = [this](int index) { emit pageDeleteRequested(index); };
            panel_->onPageCopy = [this](int index) { emit pageCopyRequested(index); };
            panel_->onPrev = [this]() { emit prevPageRequested(); };
            panel_->onNext = [this]() { emit nextPageRequested(); };
            panel_->onAdd = [this]() { emit addPageRequested(); };
            panel_->onClosed = [this]() {
                if (!active_)
                    return;
                active_ = false;
                // Popup 因点击标签在按下阶段自动关闭：置标志防释放阶段重开
                if ((QGuiApplication::mouseButtons() & Qt::LeftButton) &&
                    QRect(mapToGlobal(QPoint(0, 0)), size()).contains(QCursor::pos()))
                    panelClosePending_ = true;
                emit closed();
                update();
            };
        }
        emit expandRequested();  // 外部先重建缩略图数据（定面板高度后再定位）
        positionPanel();
        panel_->show();
        active_ = true;
        update();
    } else if (panel_ && panel_->isVisible()) {
        panel_->hide();  // hideEvent 走 onClosed 复位
    }
}

void PageRail::rebuild(const std::vector<whiteboard::Page>& pages, int currentIndex,
                       const QPixmap& background) {
    if (!panel_)
        return;  // 未创建面板（未展开过）无需重建
    panel_->rebuild(pages, currentIndex, background);
    if (panel_->isVisible())
        positionPanel();
}

QPoint PageRail::clampedPos(const QPointF& center) const {
    QRect area = bounds_;
    if (area.isNull() && parentWidget())
        area = parentWidget()->rect();
    if (area.isNull())
        return QPoint(qRound(center.x() - kTagW / 2.0), qRound(center.y() - kTagH / 2.0));
    const qreal x = qBound(area.left() + kTagW / 2.0, center.x(),
                           area.right() - kTagW / 2.0 + 1.0);
    const qreal y = qBound(area.top() + kTagH / 2.0, center.y(),
                           area.bottom() - kTagH / 2.0 + 1.0);
    return QPoint(qRound(x - kTagW / 2.0), qRound(y - kTagH / 2.0));
}

// 贴边侧切换/吸附：x 吸附对应边缘，y 保持（clamp 画布内；同侧也重新吸附）
void PageRail::applySide(bool leftSide) {
    leftSide_ = leftSide;
    QRect area = bounds_;
    if (area.isNull() && parentWidget())
        area = parentWidget()->rect();
    if (area.isNull())
        return;
    const int y = qBound(area.top(), geometry().y(), area.bottom() - kTagH + 1);
    move(leftSide_ ? area.left() : area.right() - kTagW + 1, y);
    if (expanded())
        positionPanel();
}

// 面板锚定标签内侧（左缘面板在右，右缘面板在左；垂直居中于标签，防出屏）
void PageRail::positionPanel() {
    if (!panel_)
        return;
    const QRect tag(QRect(mapToGlobal(QPoint(0, 0)), size()));
    QPoint pos = leftSide_ ? QPoint(tag.right() + 8, tag.center().y() - panel_->height() / 2)
                           : QPoint(tag.left() - 8 - panel_->width(),
                                    tag.center().y() - panel_->height() / 2);
    QScreen* screen = QGuiApplication::screenAt(tag.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        pos.setX(qBound(available.left() + 8, pos.x(), available.right() - panel_->width() - 8));
        pos.setY(qBound(available.top() + 8, pos.y(),
                        available.bottom() - panel_->height() - 8));
    }
    panel_->move(pos);
}

// 标签自绘：圆角胶囊 + 页码两行（当前页大字 / 总页数小字）；展开中橙描边
void PageRail::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(active_ ? QPen(kTagActive, 2.0) : Qt::NoPen);
    p.setBrush(kTagBg);
    p.drawRoundedRect(box, 8.0, 8.0);

    // 单行页码 "2/8"：当前页大号白字 + "/总数"小号灰字（基线对齐，水平居中）
    QFont big = font();
    big.setPixelSize(16);
    big.setBold(true);
    QFont small = font();
    small.setPixelSize(11);
    const QString cur = QString::number(index_ + 1);
    const QString rest = QStringLiteral("/%1").arg(total_);
    const QFontMetrics fmBig(big);
    const QFontMetrics fmSmall(small);
    const int textW = fmBig.horizontalAdvance(cur) + fmSmall.horizontalAdvance(rest);
    const int baseline = rect().center().y() + fmBig.height() / 2 - fmBig.descent();
    int x = (width() - textW) / 2;
    p.setFont(big);
    p.setPen(QColor(0xE8, 0xE8, 0xE8));
    p.drawText(x, baseline, cur);
    p.setFont(small);
    p.setPen(QColor(0x99, 0x99, 0x99));
    p.drawText(x + fmBig.horizontalAdvance(cur), baseline, rest);
}

void PageRail::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    pressGlobalPos_ = event->globalPos();
    pressTopLeft_ = geometry().topLeft();
    pressMoved_ = false;
    grabMouse();  // 独占鼠标事件：拖快出界仍连续跟踪（release 必达）
    event->accept();
}

void PageRail::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const QPoint delta = event->globalPos() - pressGlobalPos_;
    if (!pressMoved_ && delta.manhattanLength() > 5)
        pressMoved_ = true;
    if (pressMoved_) {
        QRect area = bounds_;
        if (area.isNull() && parentWidget())
            area = parentWidget()->rect();
        if (!area.isNull()) {
            // 左右不跟随（x 始终贴边）：鼠标越过画布水平中线实时换边预览
            const QPoint mousePos = parentWidget()->mapFromGlobal(event->globalPos());
            const bool mouseLeft = mousePos.x() < area.center().x();
            if (mouseLeft != leftSide_)
                applySide(mouseLeft);  // 换边（y 保持）
            // 仅上下拖动：目标 = 按下位置 + 全量位移（y clamp 画布内）
            const qreal cy = qBound(area.top() + kTagH / 2.0,
                                    pressTopLeft_.y() + delta.y() + kTagH / 2.0,
                                    area.bottom() - kTagH / 2.0 + 1.0);
            move(leftSide_ ? area.left() : area.right() - kTagW + 1,
                 qRound(cy - kTagH / 2.0));
        }
    }
    event->accept();
}

void PageRail::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    releaseMouse();
    if (pressMoved_) {
        // 松手：按鼠标释放位置立马吸附最近边（拖动中已实时预览换边）
        QRect area = bounds_;
        if (area.isNull() && parentWidget())
            area = parentWidget()->rect();
        if (!area.isNull()) {
            const QPoint mousePos = parentWidget()->mapFromGlobal(event->globalPos());
            applySide(mousePos.x() < area.center().x());
        }
    } else if (panelClosePending_) {
        panelClosePending_ = false;  // Popup 因本次点击关闭：释放阶段不重开
    } else {
        setExpanded(!expanded());  // 点击：展开/收起
    }
    event->accept();
}
