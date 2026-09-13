#pragma once

#include <QPixmap>
#include <QWidget>

#include <vector>

#include "QtBoardData.h"

class QScrollArea;
class QVBoxLayout;
class QHideEvent;
class QShowEvent;

// 页数缩略图面板（Qt::Popup，参考 MaxWhiteboard DisplaySlideManagerView）：
// 256 宽圆角面板 + 标题"画板"；下方可滚动缩略图列表，
// 条目 224x156：头部条 28（左侧序号 + 右上删除按钮，1 页时隐藏删除），
// 缩略图 222x126；选中页头部条与边框为橙色 #FF6B00。
// 点击条目切换页（点击当前页保持面板），点击删除按钮请求删页（至少保留 1 页）。
class SlideManagerPanel : public QWidget {
    Q_OBJECT
public:
    explicit SlideManagerPanel(QWidget* parent = nullptr);

    // 全量重建条目（页面数据按 data 层顺序 + 当前页下标；background 非空时缩略图以其铺底）
    void rebuild(const std::vector<whiteboard::Page>& pages, int currentIndex,
                 const QPixmap& background = QPixmap());

signals:
    void pageActivated(int index);        // 点击非当前页条目
    void pageDeleteRequested(int index);  // 点击条目删除按钮（页数 > 1 时）
    void closed();                        // 面板关闭（点击外部 / 隐藏）

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    class SlideItemWidget;

    QScrollArea* scrollArea_ = nullptr;
    QWidget* listContent_ = nullptr;
    QVBoxLayout* listLayout_ = nullptr;
    QWidget* currentItem_ = nullptr;  // 当前页条目（显示时滚动定位用）
    int currentIndex_ = 0;
    int pageCount_ = 1;
};
