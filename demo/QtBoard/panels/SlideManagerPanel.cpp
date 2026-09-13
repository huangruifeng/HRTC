#include "SlideManagerPanel.h"

#include <QEvent>
#include <QFrame>
#include <QHideEvent>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include "BoardView.h"

namespace {

// 面板尺寸（参考 DisplaySlideManagerView：256 宽、MaxHeight 775）
constexpr int kPanelW = 256;
constexpr int kTitleHeight = 34;     // 标题区："画板" 20px 文字（字符底 4，高 26）
constexpr int kMaxListHeight = 741;  // 列表最大高度（775 - 34，超出滚动）

// 条目（参考 SlideSideBarItem：224x156，头部条 28，缩略图 222x126）
constexpr int kItemW = 224;
constexpr int kItemH = 156;
constexpr int kItemSpacing = 12;
constexpr int kHeaderH = 28;
constexpr int kThumbW = 222;
constexpr int kThumbH = 126;
constexpr int kDeleteSize = 24;

// 颜色常量（参考 Main.xaml / SlideSideBar 样式）
const QColor kMenuBackground(0x23, 0x27, 0x2A, 0xCC);  // #CC23272A
const QColor kMenuBorder(0x33, 0x63, 0x6C, 0x73);      // #33636C73
const QColor kItemBorder(0x3F, 0x44, 0x48);            // #3F4448
const QColor kItemHeader(0x3F, 0x44, 0x48);            // 未选中头部条
const QColor kSelectOrange(0xFF, 0x6B, 0x00);          // #FF6B00 选中

// 删除按钮：正常/悬停按压两态图标（参考 ControlsHelper.Image/Image2 映射）
class DeleteIconButton : public QWidget {
public:
    explicit DeleteIconButton(QWidget* parent) : QWidget(parent) {
        setFixedSize(kDeleteSize, kDeleteSize);
        setCursor(Qt::PointingHandCursor);
    }

    std::function<void()> onClick;

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QPixmap icon = QPixmap(hovered_ || pressed_
                                         ? QStringLiteral(":/icons/page_delete_pressed.png")
                                         : QStringLiteral(":/icons/page_delete_normal.png"))
                                 .scaled(kDeleteSize, kDeleteSize, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
        p.drawPixmap(0, 0, icon);
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
        if (inside && onClick)
            onClick();
    }

private:
    bool hovered_ = false;
    bool pressed_ = false;
};

}  // namespace

// ---------- 缩略图条目 ----------

// 224x156 条目：头部条（序号 + 右上删除按钮）+ 222x126 缩略图；
// 选中页头部条与边框橙色。点击条目（删除按钮之外）触发 onActivated。
class SlideManagerPanel::SlideItemWidget : public QWidget {
public:
    SlideItemWidget(int index, const QPixmap& thumb, bool selected, QWidget* parent)
        : QWidget(parent), index_(index), thumb_(thumb), selected_(selected) {
        setFixedSize(kItemW, kItemH);
        setCursor(Qt::PointingHandCursor);
        deleteButton_ = new DeleteIconButton(this);
        deleteButton_->move(kItemW - 8 - kDeleteSize, (kHeaderH - kDeleteSize) / 2);
        deleteButton_->onClick = [this]() {
            if (onDelete)
                onDelete();
        };
    }

    void setDeleteVisible(bool on) { deleteButton_->setVisible(on); }

    std::function<void()> onActivated;
    std::function<void()> onDelete;

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

        // 缩略图（头下 1px 边框处，参考 1,29 位置）
        p.drawPixmap(QPoint(1, kHeaderH + 1), thumb_);
        // 头部条（上圆角 2）
        p.setPen(Qt::NoPen);
        p.setBrush(selected_ ? kSelectOrange : kItemHeader);
        p.drawRect(QRectF(1.0, 1.0, kItemW - 2.0, kHeaderH));
        if (pressed_) {
            p.setBrush(QColor(255, 255, 255, 18));
            p.drawRect(QRectF(1.0, 1.0, kItemW - 2.0, kItemH - 2.0));
        }
        p.setClipping(false);

        // 边框（选中橙色）
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(selected_ ? kSelectOrange : kItemBorder, 1.0));
        p.drawRoundedRect(box, 2.0, 2.0);

        // 序号（白色 12px，左缩进 9）
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(QColor(0xFF, 0xFF, 0xFF));
        p.drawText(QRect(9, 1, kItemW - kDeleteSize - 24, kHeaderH),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::number(index_ + 1));
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
    DeleteIconButton* deleteButton_ = nullptr;
};

// ---------- SlideManagerPanel ----------

SlideManagerPanel::SlideManagerPanel(QWidget* parent) : QWidget(parent) {
    // Windows 下 WA_TranslucentBackground 需要 FramelessWindowHint 才生效，
    // 否则圆角外未绘制区域会被渲染成黑块
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(kPanelW);

    auto* root = new QVBoxLayout(this);
    // 左右 1px、底部 8px 内缩：滚动区不能覆盖面板 1px 边框与底部 8px 圆角区域，
    // 否则不透明列表底色会盖掉圆角、底下两角变方
    root->setContentsMargins(1, kTitleHeight, 1, 8);
    root->setSpacing(0);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea_->viewport()->setAutoFillBackground(false);
    // 列表底色：面板色 rgba(35,39,42,0.8) 叠在画布上的等效不透明色 #202728。
    // 不能用 transparent——分层弹窗下透明子区域会"擦穿"面板底色（透出画布），
    // 非分层时又会残留黑块
    // 细滑块：4px #1AFFFFFF 圆角 2（参考 SlideSideBar 滚动条样式）
    scrollArea_->setStyleSheet(QStringLiteral(
        "QScrollArea { background: #202728; }"
        "QScrollArea > QWidget { background: #202728; }"
        "QScrollArea > QWidget > QWidget { background: #202728; }"
        "QScrollBar:vertical { background: #202728; width: 8px; margin: 0; }"
        "QScrollBar::handle:vertical {"
        "  background: rgba(255, 255, 255, 0.1);"
        "  border-radius: 2px;"
        "  min-height: 64px;"
        "  max-height: 64px;"
        "  margin: 0 2px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: #202728; }"));

    listContent_ = new QWidget;
    // 条目间隙透出视口已铺的不透明底色即可；不能再声明透明（QSS transparent /
    // WA_TranslucentBackground）——分层弹窗下透明子区域会擦穿成洞透出画布
    listLayout_ = new QVBoxLayout(listContent_);
    listLayout_->setContentsMargins(12, 0, 12, 12);
    listLayout_->setSpacing(kItemSpacing);
    scrollArea_->setWidget(listContent_);
    root->addWidget(scrollArea_);
}

void SlideManagerPanel::rebuild(const std::vector<whiteboard::Page>& pages, int currentIndex,
                                const QPixmap& background) {
    // 清空旧条目：deleteLater 保证在删除按钮回调链中安全销毁
    while (QLayoutItem* item = listLayout_->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    currentItem_ = nullptr;
    currentIndex_ = currentIndex;
    pageCount_ = static_cast<int>(pages.size());

    for (int i = 0; i < pageCount_; ++i) {
        const QPixmap thumb =
                    BoardView::renderPageThumbnail(pages[i], QSize(kThumbW, kThumbH), background);
        auto* item = new SlideItemWidget(i, thumb, i == currentIndex, listContent_);
        item->setDeleteVisible(pageCount_ > 1);  // 仅 1 页时隐藏删除（参考）
        item->onActivated = [this, i]() {
            if (i == currentIndex_)
                return;  // 点击当前页：保持面板（参考）
            emit pageActivated(i);
        };
        item->onDelete = [this, i]() {
            if (pageCount_ > 1)
                emit pageDeleteRequested(i);
        };
        listLayout_->addWidget(item);
        if (i == currentIndex)
            currentItem_ = item;
    }
    listLayout_->addStretch(1);

    // 高度 = 标题 + min(条目区 + 尾部留白, 列表上限)
    setFixedSize(kPanelW,
                 kTitleHeight + qMin(pageCount_ * (kItemH + kItemSpacing) + 12, kMaxListHeight));
}

void SlideManagerPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // 圆角背景 + 边框
    p.setPen(QPen(kMenuBorder, 1.0));
    p.setBrush(kMenuBackground);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);

    // 标题"画板"（20px #EEEEEE）
    QFont f = font();
    f.setPixelSize(20);
    p.setFont(f);
    p.setPen(QColor(0xEE, 0xEE, 0xEE));
    p.drawText(QRect(16, 4, width() - 32, 26), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("画板"));
}

void SlideManagerPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // 滚动定位到当前页条目（保留 36px 头部余量）
    QTimer::singleShot(0, this, [this]() {
        if (currentItem_)
            scrollArea_->ensureWidgetVisible(currentItem_, 0, 36);
    });
}

void SlideManagerPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    emit closed();
}
