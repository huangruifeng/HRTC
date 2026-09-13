#include "PathProbe.h"

#include <QGraphicsPathItem>
#include <QPainterPath>

#include <fstream>
#include <string>

namespace {
void logLine(const std::string& msg) {
    std::ofstream f("E:/code/HRTC/build/board_debug.log", std::ios::app);
    f << msg << std::endl;
}
}

void runPathProbe(const char*) {
    {
        QPainterPath path;
        path.moveTo(QPointF(333, 222));
        logLine("probe A: local moveTo-only elemCount=" + std::to_string(path.elementCount()));
        QGraphicsPathItem stackItem(path);
        QPainterPath p = stackItem.path();
        logLine("probe A: stack item elemCount=" + std::to_string(p.elementCount()) +
                " curPos=(" + std::to_string(p.currentPosition().x()) + "," +
                std::to_string(p.currentPosition().y()) + ")");
    }
    {
        QPainterPath path;
        path.moveTo(QPointF(100, 100));
        path.lineTo(QPointF(200, 200));
        logLine("probe B: local moveTo+lineTo elemCount=" + std::to_string(path.elementCount()));
        QGraphicsPathItem stackItem(path);
        QPainterPath p = stackItem.path();
        logLine("probe B: stack item elemCount=" + std::to_string(p.elementCount()) +
                " curPos=(" + std::to_string(p.currentPosition().x()) + "," +
                std::to_string(p.currentPosition().y()) + ")");
    }
}