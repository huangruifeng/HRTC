#pragma once

#include <QHash>
#include <QRect>
#include <QToolButton>
#include <QWidget>

#include "BoardView.h"

class QButtonGroup;
class QHBoxLayout;
class QPaintEvent;

// 工具栏按钮：上图标（48x48）下文字（12px）；选中/按压高亮换图并点亮文字
class BoardToolButton : public QToolButton {
    Q_OBJECT
public:
    explicit BoardToolButton(const QString& text, QWidget* parent = nullptr);

    void setPixmaps(const QPixmap& normal, const QPixmap& active);
    void setExtendPixmap(const QPixmap& extend);   // 展开态图（设置面板展开中显示）
    void setDisabledPixmap(const QPixmap& disabled);
    void setActiveState(bool on);  // 非 checkable 按钮的外部激活态（如更多/页码按钮展开中）
    void setExtendState(bool on);  // 设置面板展开中（对应参考 Extend：展开态图优先于选中态图）

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPixmap normalPixmap_;
    QPixmap activePixmap_;
    QPixmap extendPixmap_;
    QPixmap disabledPixmap_;
    bool activeState_ = false;
    bool extendState_ = false;
};

// 底部居中悬浮工具栏（参考 MaxWhiteboard DisplayToolBar）：
// 左侧工具组（互斥选中）/ 中部功能组（撤销重做缩放）/ 右侧"更多"入口，组间 12px 分隔；
// 页面管理（翻页/页码/添加页）已拆出至左侧 PageRail 页面栏。
class BoardToolBar : public QWidget {
    Q_OBJECT
public:
    explicit BoardToolBar(QWidget* parent = nullptr);

    void setCurrentTool(BoardView::Tool tool);
    void setToolPanelExtended(BoardView::Tool tool, bool extended);  // 笔/擦除设置面板展开态（按钮换展开态图）
    void setZoomPercent(qreal percent);      // 更新"100%"按钮文字（如 "150%"）
    void setMoreButtonActive(bool active);   // 更多面板展开中点亮更多按钮
    void setOtherButtonActive(bool active);   // "其他"面板展开中/5类工具激活中点亮"其他"按钮

    // 指定工具按钮的全局矩形（弹出面板定位用；按钮不存在时返回空矩形）
    QRect toolButtonGlobalRect(BoardView::Tool tool) const;
    // 更多按钮的全局矩形（更多/设置面板定位用）
    QRect moreButtonGlobalRect() const;
    // "其他"按钮的全局矩形（图形/表格/文字/小工具子面板及"其他"面板定位用）
    QRect otherButtonGlobalRect() const;

protected:
    void paintEvent(QPaintEvent* event) override;  // 自绘圆角背景（圆角外透明，露出画布）

signals:
    void toolSelected(BoardView::Tool tool);
    void undoRequested();
    void redoRequested();
    void zoomInRequested();
    void zoomOutRequested();
    void zoomResetRequested();
    void moreRequested();       // 更多按钮：弹出/收起更多面板（互动/保存/打开/设置/退出）
    void otherRequested();      // "其他"按钮：弹出/收起其他工具面板（图形/导图/表格/文字/小工具）

private:
    BoardToolButton* createButton(const QString& text, const QPixmap& normal,
                                  const QPixmap& active, bool checkable,
                                  const QPixmap& extend = QPixmap());

    QButtonGroup* toolGroup_ = nullptr;
    QHash<int, BoardToolButton*> toolButtons_;  // key = static_cast<int>(BoardView::Tool)
    BoardToolButton* zoomResetButton_ = nullptr;
    BoardToolButton* moreButton_ = nullptr;
    BoardToolButton* otherButton_ = nullptr;  // 左侧工具组"其他"入口（非 checkable，仿 moreButton_）
};
