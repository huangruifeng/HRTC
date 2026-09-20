#pragma once

#include <QPixmap>
#include <QWidget>

#include <vector>

#include "QtBoardData.h"

class QHideEvent;
class QMouseEvent;
class QPaintEvent;

// 左侧页面栏（页面管理从底部工具栏拆出，两种工具栏模式共用）：
// 收缩态 = 贴画布左/右边缘的可拖动胶囊标签（显示 "当前页/总页数"，如 2/12），
// 点击展开缩略图面板；拖动中上下自由移动、左右实时跟随，松手时标签中心越过
// 画布水平中线则吸附另一侧边缘（始终贴边）。
// 展开态 = Qt::Popup 圆角面板（点击外部自动关闭 = 失焦收缩），锚定标签内侧：
// 可滚动缩略图纵向列表（当前页橙色描边、条目 hover 显示删除+复制按钮，
// 复制在删除旁边）+ 底部按钮行（上一页 / 添加页 / 下一页）。
class PageRail : public QWidget {
    Q_OBJECT
public:
    explicit PageRail(QWidget* parent = nullptr);

    // 页码信息（0-based index；同步标签文字与面板按钮可用态）
    void setPageInfo(int index, int total);
    // 画布活动范围（标签拖动 clamp / 吸附中线判定；空矩形 = 父 widget 区域）
    void setBounds(const QRect& rect);

    bool expanded() const;                // 面板展开中（cpp 实现：Panel 为前向声明）
    void setExpanded(bool on);  // 展开/收起面板（定位锚定标签）

    // 全量重建条目（页面数据按 data 层顺序 + 当前页下标；background 非空时缩略图以其铺底）
    void rebuild(const std::vector<whiteboard::Page>& pages, int currentIndex,
                 const QPixmap& background = QPixmap());

signals:
    void expandRequested();               // 即将展开（外部先重建缩略图数据）
    void pageActivated(int index);        // 点击非当前页条目（面板保持）
    void pageDeleteRequested(int index);  // 点击条目删除按钮（页数 > 1 时）
    void pageCopyRequested(int index);    // 点击条目复制按钮（页数达上限时禁用）
    void prevPageRequested();             // 底部"上一页"
    void nextPageRequested();             // 底部"下一页"
    void addPageRequested();              // 底部"添加页"
    void closed();                        // 面板关闭（点击外部 / 收起）

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    class Panel;  // 展开面板（Qt::Popup；cpp 内定义）

    void applySide(bool leftSide);       // 切换贴边侧（x 吸附，y 保持）
    QPoint clampedPos(const QPointF& center) const;  // 标签中心 → clamp 后左上角
    void positionPanel();                // 面板锚定标签内侧（防出屏）

    Panel* panel_ = nullptr;
    QRect bounds_;                       // 画布活动范围（空 = 父 widget 区域）
    int index_ = 0;     // 当前页（0-based）
    int total_ = 1;     // 总页数
    bool leftSide_ = true;   // 贴左缘（false = 右缘）
    bool active_ = false;    // 展开中：标签点亮
    bool positioned_ = false;  // 初始位置已设置（首次 setBounds 后贴左缘中部）

    // 标签拖动状态
    bool pressMoved_ = false;
    QPoint pressGlobalPos_;
    QPoint pressTopLeft_;   // 按下时标签左上角（父坐标；拖动目标 = 此位置 + 全量位移）
    // 面板因点击标签在按下阶段自动关闭：释放阶段不重开（同工具栏 pending 模式）
    bool panelClosePending_ = false;

    static constexpr int kTagW = 44;    // 标签宽
    static constexpr int kTagH = 88;    // 标签高
};
