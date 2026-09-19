#include "BoardIcons.h"

#include <QPainter>
#include <QPainterPath>

namespace BoardIcons {
namespace {

constexpr int kLogicalSize = 48;  // 与工具栏图标 48x48 一致

const QColor kNormalColor(0x99, 0x99, 0x99);
const QColor kActiveColor(0xFF, 0x7D, 0x00);

QPen iconPen(const QColor& color, qreal width = 3.0) {
    return QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

// 套索：虚线圈选 + 三个节点 + 收尾小尾巴
void drawLasso(QPainter& p, const QColor& c) {
    QPen dashed = iconPen(c);
    dashed.setStyle(Qt::CustomDashLine);
    dashed.setDashPattern({3.0, 2.0});
    p.setPen(dashed);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(11.0, 11.0, 26.0, 20.0));

    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QPointF(37.0, 21.0), 2.4, 2.4);
    p.drawEllipse(QPointF(17.5, 12.3), 2.4, 2.4);
    p.drawEllipse(QPointF(17.5, 29.7), 2.4, 2.4);

    QPainterPath tail;
    tail.moveTo(29.5, 29.0);
    tail.quadTo(27.0, 37.0, 20.5, 39.5);
    p.setPen(iconPen(c));
    p.setBrush(Qt::NoBrush);
    p.drawPath(tail);
}

// 抓手：四指 + 掌心 + 拇指
void drawHand(QPainter& p, const QColor& c) {
    p.setPen(iconPen(c));
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(17.5, 26.0), QPointF(17.5, 19.0));
    p.drawLine(QPointF(23.0, 26.0), QPointF(23.0, 14.5));
    p.drawLine(QPointF(28.5, 26.0), QPointF(28.5, 16.0));
    p.drawLine(QPointF(34.0, 26.0), QPointF(34.0, 20.0));
    p.drawRoundedRect(QRectF(16.0, 26.0, 19.5, 13.0), 6.0, 6.0);
    p.drawLine(QPointF(16.0, 31.5), QPointF(11.0, 26.5));
}

// 撤销：顶部向左箭头 + 逆时针弧（顶部 → 左 → 底）
void drawUndo(QPainter& p, const QColor& c) {
    p.setPen(iconPen(c));
    p.setBrush(Qt::NoBrush);
    const QRectF rect(13.5, 16.5, 21.0, 21.0);  // 圆心 (24, 27)，半径 10.5
    QPainterPath arc;
    arc.arcMoveTo(rect, 90.0);
    arc.arcTo(rect, 90.0, 200.0);
    p.drawPath(arc);
    p.drawLine(QPointF(24.0, 16.5), QPointF(30.9, 12.5));
    p.drawLine(QPointF(24.0, 16.5), QPointF(30.9, 20.5));
}

// 重做：顶部向右箭头 + 顺时针弧（顶部 → 右 → 底）
void drawRedo(QPainter& p, const QColor& c) {
    p.setPen(iconPen(c));
    p.setBrush(Qt::NoBrush);
    const QRectF rect(13.5, 16.5, 21.0, 21.0);
    QPainterPath arc;
    arc.arcMoveTo(rect, 90.0);
    arc.arcTo(rect, 90.0, -200.0);
    p.drawPath(arc);
    p.drawLine(QPointF(24.0, 16.5), QPointF(17.1, 12.5));
    p.drawLine(QPointF(24.0, 16.5), QPointF(17.1, 20.5));
}

// 放大 / 缩小：放大镜（plus=true 加号，否则减号）
void drawZoom(QPainter& p, const QColor& c, bool plus) {
    p.setPen(iconPen(c));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(11.0, 11.0, 20.0, 20.0));
    p.drawLine(QPointF(28.1, 28.1), QPointF(37.0, 37.0));
    p.drawLine(QPointF(16.0, 21.0), QPointF(26.0, 21.0));
    if (plus)
        p.drawLine(QPointF(21.0, 16.0), QPointF(21.0, 26.0));
}

// 100%：四角取景框（复位缩放）
void drawZoomReset(QPainter& p, const QColor& c) {
    p.setPen(iconPen(c));
    p.setBrush(Qt::NoBrush);
    const QPointF corners[4][3] = {
        {QPointF(13.5, 19.0), QPointF(13.5, 13.5), QPointF(19.0, 13.5)},
        {QPointF(29.0, 13.5), QPointF(34.5, 13.5), QPointF(34.5, 19.0)},
        {QPointF(34.5, 29.0), QPointF(34.5, 34.5), QPointF(29.0, 34.5)},
        {QPointF(19.0, 34.5), QPointF(13.5, 34.5), QPointF(13.5, 29.0)},
    };
    for (const auto& corner : corners) {
        QPainterPath path;
        path.moveTo(corner[0]);
        path.lineTo(corner[1]);
        path.lineTo(corner[2]);
        p.drawPath(path);
    }
}

// 设置：齿轮（环向 8 齿 + 齿圈 + 中心孔）
void drawSettings(QPainter& p, const QColor& c) {
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    for (int i = 0; i < 8; ++i) {
        p.save();
        p.translate(24.0, 24.0);
        p.rotate(i * 45.0);
        p.drawRoundedRect(QRectF(-2.4, -17.5, 4.8, 6.5), 1.8, 1.8);
        p.restore();
    }

    p.setPen(iconPen(c, 3.6));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(24.0, 24.0), 9.8, 9.8);
    p.setPen(iconPen(c, 2.4));
    p.drawEllipse(QPointF(24.0, 24.0), 3.2, 3.2);
}

// 互动：发射点 + 三道波纹
void drawInteract(QPainter& p, const QColor& c) {
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QPointF(13.5, 34.5), 2.6, 2.6);

    p.setPen(iconPen(c));
    p.setBrush(Qt::NoBrush);
    for (qreal r : {8.0, 15.0, 22.0}) {
        QPainterPath path;
        const QRectF rect(13.5 - r, 34.5 - r, r * 2.0, r * 2.0);
        path.arcMoveTo(rect, 0.0);
        path.arcTo(rect, 0.0, 90.0);
        p.drawPath(path);
    }
}

// 更多：横排三个圆点
void drawMore(QPainter& p, const QColor& c) {
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QPointF(12.5, 24.0), 3.2, 3.2);
    p.drawEllipse(QPointF(24.0, 24.0), 3.2, 3.2);
    p.drawEllipse(QPointF(35.5, 24.0), 3.2, 3.2);
}

// 保存：向下箭头 + 底线（保存到文件）
void drawSave(QPainter& p, const QColor& c) {
    p.setPen(iconPen(c));
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(24.0, 12.5), QPointF(24.0, 30.0));
    p.drawLine(QPointF(24.0, 30.0), QPointF(17.0, 23.0));
    p.drawLine(QPointF(24.0, 30.0), QPointF(31.0, 23.0));
    p.drawLine(QPointF(13.0, 36.0), QPointF(35.0, 36.0));
}

// 打开：文件夹轮廓（从文件读取）
void drawOpen(QPainter& p, const QColor& c) {
    p.setPen(iconPen(c));
    p.setBrush(Qt::NoBrush);
    QPainterPath folder;
    folder.moveTo(12.0, 34.0);
    folder.lineTo(12.0, 15.5);
    folder.lineTo(20.0, 15.5);
    folder.lineTo(24.0, 19.5);
    folder.lineTo(36.0, 19.5);
    folder.lineTo(36.0, 34.0);
    folder.closeSubpath();
    p.drawPath(folder);
}

// 退出：电源符号（顶部开口圆环 + 竖直电源线）
void drawExit(QPainter& p, const QColor& c) {
    p.setPen(iconPen(c));
    p.setBrush(Qt::NoBrush);
    const QRectF rect(13.0, 15.0, 22.0, 22.0);  // 圆心 (24, 26)，半径 11
    QPainterPath arc;
    arc.arcMoveTo(rect, 125.0);
    arc.arcTo(rect, 125.0, 290.0);  // 顶部 55°~125° 留开口
    p.drawPath(arc);
    p.drawLine(QPointF(24.0, 11.5), QPointF(24.0, 25.0));
}

// 荧光笔：斜置粗头笔 + 底部荧光线
void drawHighlighter(QPainter& p, const QColor& c) {
    // 荧光线：底部粗横线
    p.setPen(iconPen(c, 5.0));
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(9.0, 38.0), QPointF(39.0, 38.0));

    // 笔杆：斜线（右上）
    p.setPen(iconPen(c, 6.0));
    p.drawLine(QPointF(21.5, 25.5), QPointF(37.5, 9.5));

    // 笔头：短粗段（左下，更宽，指向荧光线）
    p.setPen(iconPen(c, 10.0));
    p.drawLine(QPointF(21.5, 25.5), QPointF(15.0, 32.0));
}

// 图形：圆 + 矩形叠加（形状工具入口）
void drawShape(QPainter& p, const QColor& c) {
    p.setPen(iconPen(c));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(10.0, 10.0, 20.0, 20.0));
    p.drawRect(QRectF(18.0, 18.0, 20.0, 20.0));
}

// 思维导图：中心节点 + 三个环绕节点 + 连线
void drawMindMap(QPainter& p, const QColor& c) {
    const QPointF center(24.0, 24.0);
    const QPointF nodes[3] = { QPointF(12.5, 13.5), QPointF(12.5, 34.5),
                               QPointF(35.5, 24.0) };
    p.setPen(iconPen(c, 2.6));
    p.setBrush(Qt::NoBrush);
    for (const QPointF& n : nodes)
        p.drawLine(center, n);
    p.setPen(iconPen(c, 3.0));
    p.drawEllipse(center, 6.0, 6.0);
    for (const QPointF& n : nodes)
        p.drawEllipse(n, 4.0, 4.0);
}

// 表格：外框 + 中横线 + 中竖线（2x2 网格）
void drawTable(QPainter& p, const QColor& c) {
    p.setPen(iconPen(c));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(11.0, 13.0, 26.0, 22.0));
    p.drawLine(QPointF(11.0, 24.0), QPointF(37.0, 24.0));
    p.drawLine(QPointF(24.0, 13.0), QPointF(24.0, 35.0));
}

// 文字：字母 T 线稿（顶横杆 + 竖杆，底部基线短划提示输入）
void drawTextGlyph(QPainter& p, const QColor& c) {
    p.setPen(iconPen(c));
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(12.0, 12.5), QPointF(36.0, 12.5));
    p.drawLine(QPointF(24.0, 12.5), QPointF(24.0, 33.5));
    p.setPen(iconPen(c, 2.4));
    p.drawLine(QPointF(15.0, 38.0), QPointF(33.0, 38.0));
}

// 小工具：秒表线稿（圆盘 + 顶冠钮 + 右上侧柄 + 指针）
void drawWidget(QPainter& p, const QColor& c) {
    p.setPen(iconPen(c));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(10.5, 14.0, 27.0, 27.0));  // 圆盘（圆心 24, 27.5）

    // 顶冠钮：顶部圆角小方块
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(20.5, 9.0, 7.0, 5.0), 2.0, 2.0);

    // 右上侧柄（秒表按钮）
    p.setPen(iconPen(c, 3.2));
    p.drawLine(QPointF(34.2, 16.6), QPointF(37.4, 13.4));

    // 指针：向上主针 + 右下副针
    p.setPen(iconPen(c, 2.6));
    p.drawLine(QPointF(24.0, 27.5), QPointF(24.0, 19.5));
    p.drawLine(QPointF(24.0, 27.5), QPointF(29.0, 30.0));
}

// 其他：2x2 圆角方块网格（图形/导图/表格/文字/小工具汇总入口）
void drawOther(QPainter& p, const QColor& c) {
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    const qreal s = 13.0;    // 方块边长
    const qreal gap = 3.5;   // 方块间距
    const qreal x0 = 24.0 - (s + gap / 2.0);
    const qreal y0 = 24.0 - (s + gap / 2.0);
    p.drawRoundedRect(QRectF(x0, y0, s, s), 3.0, 3.0);
    p.drawRoundedRect(QRectF(x0 + s + gap, y0, s, s), 3.0, 3.0);
    p.drawRoundedRect(QRectF(x0, y0 + s + gap, s, s), 3.0, 3.0);
    p.drawRoundedRect(QRectF(x0 + s + gap, y0 + s + gap, s, s), 3.0, 3.0);
}

}  // namespace

QPixmap pixmap(Glyph glyph, bool active) {
    QPixmap pm(kLogicalSize * 2, kLogicalSize * 2);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);

    const QColor color = active ? kActiveColor : kNormalColor;

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    switch (glyph) {
        case Glyph::Lasso:
            drawLasso(p, color);
            break;
        case Glyph::Hand:
            drawHand(p, color);
            break;
        case Glyph::Undo:
            drawUndo(p, color);
            break;
        case Glyph::Redo:
            drawRedo(p, color);
            break;
        case Glyph::ZoomIn:
            drawZoom(p, color, true);
            break;
        case Glyph::ZoomOut:
            drawZoom(p, color, false);
            break;
        case Glyph::ZoomReset:
            drawZoomReset(p, color);
            break;
        case Glyph::Interact:
            drawInteract(p, color);
            break;
        case Glyph::Settings:
            drawSettings(p, color);
            break;
        case Glyph::More:
            drawMore(p, color);
            break;
        case Glyph::Save:
            drawSave(p, color);
            break;
        case Glyph::Open:
            drawOpen(p, color);
            break;
        case Glyph::Exit:
            drawExit(p, color);
            break;
        case Glyph::Highlighter:
            drawHighlighter(p, color);
            break;
        case Glyph::Shape:
            drawShape(p, color);
            break;
        case Glyph::MindMap:
            drawMindMap(p, color);
            break;
        case Glyph::Table:
            drawTable(p, color);
            break;
        case Glyph::Text:
            drawTextGlyph(p, color);
            break;
        case Glyph::Widget:
            drawWidget(p, color);
            break;
        case Glyph::Other:
            drawOther(p, color);
            break;
    }
    return pm;
}

}  // namespace BoardIcons
