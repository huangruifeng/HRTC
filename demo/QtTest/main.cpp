// 独立最小测试：用默认编译选项(v143)链接 v140 Qt DLL，验证 QGraphicsPathItem 是否丢 path
#include <QApplication>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QPainterPath>
#include <cstdio>

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QPainterPath path;
    path.moveTo(QPointF(100, 100));
    path.lineTo(QPointF(200, 200));
    printf("[1] local path after moveTo+lineTo: elemCount=%d curPos=(%.0f,%.0f)\n",
           path.elementCount(), path.currentPosition().x(), path.currentPosition().y());

    QPainterPath copy = path;
    printf("[2] copied path: elemCount=%d\n", copy.elementCount());

    {
        QGraphicsPathItem stackItem(path);
        QPainterPath p = stackItem.path();
        printf("[3] stack item after ctor: elemCount=%d curPos=(%.0f,%.0f)\n",
               p.elementCount(), p.currentPosition().x(), p.currentPosition().y());
    }

    {
        QGraphicsPathItem* heapItem = new QGraphicsPathItem(path);
        QPainterPath p = heapItem->path();
        printf("[4] heap item after ctor: elemCount=%d curPos=(%.0f,%.0f)\n",
               p.elementCount(), p.currentPosition().x(), p.currentPosition().y());

        QGraphicsScene scene;
        scene.addItem(heapItem);
        QPainterPath p2 = heapItem->path();
        printf("[5] heap item after addItem: elemCount=%d curPos=(%.0f,%.0f)\n",
               p2.elementCount(), p2.currentPosition().x(), p2.currentPosition().y());
    }

    printf("sizeof(QPainterPath)=%d sizeof(QGraphicsPathItem)=%d\n",
           (int)sizeof(QPainterPath), (int)sizeof(QGraphicsPathItem));
    return 0;
}
