#pragma once

#include <QWidget>

class QHideEvent;

// 滑动清屏弹出面板（Qt::Popup，184x72，#23272A 圆角）：
// 拖动滑块（152x48）至 ≥80% 松手 → 清空当前页并收起；否则 600ms OutCirc 回弹到起点
class EraserPanel : public QWidget {
    Q_OBJECT
public:
    explicit EraserPanel(QWidget* parent = nullptr);

signals:
    void clearRequested();
    void closed();  // 面板关闭（点击外部等）

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    class EraserSlider;
    EraserSlider* slider_ = nullptr;
};
