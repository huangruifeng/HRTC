#pragma once
#include <QGraphicsObject>
#include <QList>
#include <QVector>

class QGraphicsPathItem;

// 选择框：围绕目标笔画集合的虚线外接矩形（并集）+ 8 个缩放手柄 + 顶部旋转手柄。
// 位置/形状随所有目标 item 的变换实时同步（场景坐标），自身不做命中遮挡之外的交互。
class SelectionFrame : public QGraphicsObject {
    Q_OBJECT
public:
    enum Handle {
        None = 0,
        TL, TR, BL, BR,   // 四角：对角缩放（自由纵横比）
        L, R, T, B,       // 四边中点：单轴缩放
        Rotate,           // 顶部旋转手柄
        HandleCount
    };

    explicit SelectionFrame(const QList<QGraphicsPathItem*>& targets);

    const QList<QGraphicsPathItem*>& targets() const { return targets_; }
    QRectF rect() const { return rect_; }
    QPointF center() const { return rect_.center(); }

    // 场景坐标命中检测（优先手柄，其次框内）
    Handle handleAt(const QPointF& scenePos) const;
    // 按目标当前变换刷新外接矩形与手柄位置
    void sync();

    // 单个笔记的外接矩形：优先 data(1)（数据层 bounding 同步），无则按 path 重算
    static QRectF itemRect(const QGraphicsPathItem* item);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

    static constexpr qreal kHandleSize = 10;
    static constexpr qreal kRotateOffset = 26;   // 旋转手柄距顶边距离
    static constexpr qreal kRotateRadius = 5;

private:
    void layoutHandles();

    QList<QGraphicsPathItem*> targets_;
    QRectF rect_;                  // 所有目标当前外接矩形的并集（场景坐标）
    QVector<QPointF> handlePos_;   // Handle 枚举序，9 个手柄中心（场景坐标）
    QVector<QRectF> handleRect_;   // 命中矩形（旋转手柄为圆，用外接方形近似）
    QVector<QGraphicsRectItem*> handleItems_;  // 8 个方块子图元
};
