#include "EraserPanel.h"

#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QSlider>

namespace {
constexpr int kSlideThreshold = 80;  // 松手清屏阈值（百分比）
}

// 滑动清屏滑块：自绘背景图 / 箭头 / 文案 / 滑块，完全接管鼠标拖动
class EraserPanel::EraserSlider : public QSlider {
public:
    explicit EraserSlider(QWidget* parent) : QSlider(Qt::Horizontal, parent) {
        setRange(0, 100);
        setValue(0);
        setFixedSize(152, 48);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        connect(this, &QSlider::valueChanged, this, QOverload<>::of(&QWidget::update));
    }

    // 600ms OutCirc 回弹到起点
    void startBackAnimation() {
        if (!animation_) {
            animation_ = new QPropertyAnimation(this, "value", this);
            animation_->setDuration(600);
            animation_->setEasingCurve(QEasingCurve::OutCirc);
        }
        animation_->stop();
        animation_->setStartValue(value());
        animation_->setEndValue(0);
        animation_->start();
    }

    // 重置到起点（每次弹出面板时调用）
    void reset() {
        if (animation_)
            animation_->stop();
        dragging_ = false;
        setValue(0);
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        p.drawPixmap(rect(), QPixmap(QStringLiteral(":/icons/eraser_bg.png")));

        // ≥ 阈值：箭头隐藏、文案切"松开"；否则箭头 + "滑动清屏"
        const bool released = value() >= kSlideThreshold;
        if (!released)
            p.drawPixmap(QRect(49, 13, 22, 22), QPixmap(QStringLiteral(":/icons/eraser_arrow.png")));

        QFont f = font();
        f.setPixelSize(14);
        p.setFont(f);
        p.setPen(QColor(0xAA, 0xAA, 0xAA));
        const int textX = released ? 49 : 73;
        p.drawText(QRect(textX, 0, 152 - textX - 4, 48), Qt::AlignVCenter | Qt::AlignLeft,
                   released ? QStringLiteral("松开") : QStringLiteral("滑动清屏"));

        // 滑块：value 0 → 左端 (5,4)，value 100 → 右端 (107,4)
        const qreal thumbX = 5.0 + value() * 1.02;
        const QPixmap thumb(dragging_ ? QStringLiteral(":/icons/eraser_thumb_pressed.png")
                                      : QStringLiteral(":/icons/eraser_thumb.png"));
        p.drawPixmap(QRectF(thumbX, 4.0, 40.0, 40.0), thumb, QRectF(thumb.rect()));
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        if (animation_)
            animation_->stop();
        dragging_ = true;
        updateValueFromX(event->pos().x());
        update();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!dragging_)
            return;
        updateValueFromX(event->pos().x());
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !dragging_)
            return;
        dragging_ = false;
        update();
        emit sliderReleased();
    }

private:
    // 光标映射：滑块中心跟随鼠标（滑块左端 5 → 右端 107）
    void updateValueFromX(int x) {
        const qreal thumbX = qBound(5.0, static_cast<qreal>(x) - 20.0, 107.0);
        setValue(qRound((thumbX - 5.0) / 1.02));
    }

    QPropertyAnimation* animation_ = nullptr;
    bool dragging_ = false;
};

// ---------- EraserPanel ----------

EraserPanel::EraserPanel(QWidget* parent) : QWidget(parent) {
    // Windows 下 WA_TranslucentBackground 需要 FramelessWindowHint 才生效，
    // 否则圆角外未绘制区域会被渲染成黑块
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(184, 72);

    slider_ = new EraserSlider(this);
    slider_->move(16, 12);

    connect(slider_, &QSlider::sliderReleased, this, [this]() {
        if (slider_->value() >= kSlideThreshold) {
            hide();
            emit clearRequested();
        } else {
            slider_->startBackAnimation();
        }
    });
}

void EraserPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x23, 0x27, 0x2A));  // #23272A
    p.drawRoundedRect(QRectF(rect()), 8.0, 8.0);
}

void EraserPanel::showEvent(QShowEvent* event) {
    slider_->reset();  // 每次弹出重置到起点
    QWidget::showEvent(event);
}

void EraserPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    emit closed();
}
