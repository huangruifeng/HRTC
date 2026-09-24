#include "SketchBuilder.h"

#include <QColor>
#include <QJsonObject>
#include <QJsonValue>
#include <QtMath>

#include <algorithm>
#include <cmath>

#include "BoardUtil.h"

namespace ai {

namespace {

constexpr double kMaxCoord = 100000.0;  // 场景坐标上限（与工具层一致）

// 归一化 (u,v) → board 坐标（frame 内仿射映射）
QPointF ToBoard(const QRectF& frame, double u, double v) {
    const double x = frame.left() + u * frame.width();
    const double y = frame.top() + v * frame.height();
    return QPointF(qBound(-kMaxCoord, x, kMaxCoord), qBound(-kMaxCoord, y, kMaxCoord));
}

// [[u,v], ...] → 归一化点列表；任一项非法返回 false
bool ParsePointArray(const QJsonValue& value, std::vector<QPointF>* out) {
    if (!value.isArray())
        return false;
    const QJsonArray arr = value.toArray();
    out->reserve(arr.size());
    for (const QJsonValue& item : arr) {
        const QJsonArray pair = item.toArray();
        if (pair.size() < 2 || !pair.at(0).isDouble() || !pair.at(1).isDouble())
            return false;
        out->push_back(QPointF(pair.at(0).toDouble(), pair.at(1).toDouble()));
    }
    return !out->empty();
}

// 二次贝塞尔采样（segments 段 → segments+1 点）
std::vector<QPointF> SampleQuad(const QPointF& p0, const QPointF& p1, const QPointF& p2,
                                int segments) {
    std::vector<QPointF> pts;
    pts.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const double t = static_cast<double>(i) / segments;
        const double mt = 1.0 - t;
        pts.push_back(QPointF(mt * mt * p0.x() + 2.0 * mt * t * p1.x() + t * t * p2.x(),
                              mt * mt * p0.y() + 2.0 * mt * t * p1.y() + t * t * p2.y()));
    }
    return pts;
}

// 三次贝塞尔采样（segments 段 → segments+1 点）
std::vector<QPointF> SampleCubic(const QPointF& p0, const QPointF& p1, const QPointF& p2,
                                 const QPointF& p3, int segments) {
    std::vector<QPointF> pts;
    pts.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const double t = static_cast<double>(i) / segments;
        const double mt = 1.0 - t;
        const double a = mt * mt * mt;
        const double b = 3.0 * mt * mt * t;
        const double c = 3.0 * mt * t * t;
        const double d = t * t * t;
        pts.push_back(QPointF(a * p0.x() + b * p1.x() + c * p2.x() + d * p3.x(),
                              a * p0.y() + b * p1.y() + c * p2.y() + d * p3.y()));
    }
    return pts;
}

std::shared_ptr<whiteboard::Stroke> MakeStroke(const std::vector<QPointF>& pts, uint32_t color,
                                               int width) {
    auto stroke = std::make_shared<whiteboard::Stroke>();
    stroke->Reset();
    stroke->color = color;
    stroke->width = qMax(1, width);
    for (const QPointF& p : pts)
        stroke->Append(whiteboard::Point(qRound(p.x()), qRound(p.y())));
    return stroke;
}

// ---------- RDP ----------

double PerpDistanceSq(const whiteboard::Point& p, const whiteboard::Point& a,
                      const whiteboard::Point& b) {
    const double dx = static_cast<double>(b.x - a.x);
    const double dy = static_cast<double>(b.y - a.y);
    const double lenSq = dx * dx + dy * dy;
    const double px = static_cast<double>(p.x - a.x);
    const double py = static_cast<double>(p.y - a.y);
    if (lenSq <= 1e-9)
        return px * px + py * py;
    double t = (px * dx + py * dy) / lenSq;
    t = qBound(0.0, t, 1.0);
    const double projX = px - t * dx;
    const double projY = py - t * dy;
    return projX * projX + projY * projY;
}

// 递归标记保留下标（升序）；跳过与首点重合的尾点以外全部由 maxDist 判定
void RdpMark(const std::vector<whiteboard::Point>& src, int first, int last, double epsilonSq,
             std::vector<int>* keep) {
    if (last <= first + 1)
        return;
    double maxDist = 0.0;
    int index = -1;
    for (int i = first + 1; i < last; ++i) {
        const double d = PerpDistanceSq(src[static_cast<size_t>(i)], src[static_cast<size_t>(first)],
                                        src[static_cast<size_t>(last)]);
        if (d > maxDist) {
            maxDist = d;
            index = i;
        }
    }
    if (index < 0 || maxDist <= epsilonSq)
        return;
    RdpMark(src, first, index, epsilonSq, keep);
    keep->push_back(index);
    RdpMark(src, index, last, epsilonSq, keep);
}

std::vector<whiteboard::Point> UniformSample(const std::vector<whiteboard::Point>& src,
                                             int maxPoints) {
    std::vector<whiteboard::Point> out;
    out.reserve(static_cast<size_t>(maxPoints));
    const double step = static_cast<double>(src.size() - 1) / (maxPoints - 1);
    for (int i = 0; i < maxPoints; ++i)
        out.push_back(src[static_cast<size_t>(std::lround(i * step))]);
    return out;
}

}  // namespace

uint32_t ColorFromHex(const QString& hex, uint32_t fallback) {
    const QColor c(hex.trimmed());
    if (!c.isValid())
        return fallback;
    uint32_t color = qColorToColorRef(c);
    if (c.alpha() < 255)
        color |= (static_cast<uint32_t>(c.alpha()) << 24);
    return color;
}

QString HexFromColor(uint32_t color) {
    return QString::asprintf("#%02X%02X%02X", static_cast<int>(color & 0xFFu),
                             static_cast<int>((color >> 8) & 0xFFu),
                             static_cast<int>((color >> 16) & 0xFFu));
}

std::vector<whiteboard::Point> SimplifyPoints(const std::vector<whiteboard::Point>& points,
                                              int maxPoints) {
    if (maxPoints < 2 || points.size() <= static_cast<size_t>(maxPoints))
        return points;

    // 初始 epsilon：包围盒对角线 / 64，随迭代放大直到保留点数达标
    int minX = points[0].x, maxX = points[0].x, minY = points[0].y, maxY = points[0].y;
    for (const auto& p : points) {
        minX = qMin(minX, p.x);
        maxX = qMax(maxX, p.x);
        minY = qMin(minY, p.y);
        maxY = qMax(maxY, p.y);
    }
    const double diag = std::sqrt(static_cast<double>(maxX - minX) * (maxX - minX) +
                                  static_cast<double>(maxY - minY) * (maxY - minY));
    double epsilon = qMax(1.0, diag / 64.0);

    const int last = static_cast<int>(points.size()) - 1;
    std::vector<int> keep;
    for (int attempt = 0; attempt < 16; ++attempt) {
        keep.clear();
        keep.push_back(0);
        RdpMark(points, 0, last, epsilon * epsilon, &keep);
        keep.push_back(last);
        if (static_cast<int>(keep.size()) <= maxPoints)
            break;
        epsilon *= 1.6;
    }
    if (static_cast<int>(keep.size()) > maxPoints)
        return UniformSample(points, maxPoints);

    std::vector<whiteboard::Point> out;
    out.reserve(keep.size());
    for (int idx : keep)
        out.push_back(points[static_cast<size_t>(idx)]);
    return out;
}

std::vector<std::shared_ptr<whiteboard::Stroke>> BuildStrokes(const QJsonArray& shapes,
                                                              const QRectF& frame,
                                                              uint32_t defaultColor,
                                                              int defaultWidth,
                                                              QString* error) {
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return std::vector<std::shared_ptr<whiteboard::Stroke>>();
    };

    if (shapes.isEmpty())
        return fail(QStringLiteral("shapes 为空"));
    if (frame.width() <= 0 || frame.height() <= 0)
        return fail(QStringLiteral("frame 尺寸非法"));

    std::vector<std::shared_ptr<whiteboard::Stroke>> strokes;
    strokes.reserve(static_cast<size_t>(shapes.size()));

    for (int i = 0; i < shapes.size(); ++i) {
        const QJsonObject shape = shapes.at(i).toObject();
        if (shape.isEmpty())
            return fail(QStringLiteral("shapes[%1] 不是对象").arg(i));

        const QString prim = shape.value(QStringLiteral("prim")).toString();
        const uint32_t color = ColorFromHex(shape.value(QStringLiteral("color")).toString(),
                                            defaultColor);
        const int width = shape.contains(QStringLiteral("width"))
                              ? shape.value(QStringLiteral("width")).toInt(defaultWidth)
                              : defaultWidth;

        // 归一化点（ellipse 用 center/rx/ry 生成）→ board 坐标点列
        std::vector<QPointF> boardPts;
        if (prim == QStringLiteral("ellipse")) {
            const QJsonArray center = shape.value(QStringLiteral("center")).toArray();
            if (center.size() < 2)
                return fail(QStringLiteral("shapes[%1].center 缺失").arg(i));
            const double rx = shape.value(QStringLiteral("rx")).toDouble(0.1);
            const double ry = shape.value(QStringLiteral("ry")).toDouble(0.1);
            if (rx <= 0.0 || ry <= 0.0)
                return fail(QStringLiteral("shapes[%1] rx/ry 必须为正").arg(i));
            const double cu = center.at(0).toDouble();
            const double cv = center.at(1).toDouble();
            constexpr int kSegments = 48;
            for (int s = 0; s < kSegments; ++s) {
                const double a = 2.0 * M_PI * s / kSegments;
                boardPts.push_back(ToBoard(frame, cu + rx * std::cos(a), cv + ry * std::sin(a)));
            }
            boardPts.push_back(boardPts.front());  // 闭合：末点接回首点
        } else {
            std::vector<QPointF> norm;
            if (!ParsePointArray(shape.value(QStringLiteral("points")), &norm))
                return fail(QStringLiteral("shapes[%1].points 缺失或非法").arg(i));

            if (prim == QStringLiteral("polyline")) {
                if (norm.size() < 2)
                    return fail(QStringLiteral("shapes[%1] polyline 至少 2 点").arg(i));
                for (const QPointF& n : norm)
                    boardPts.push_back(ToBoard(frame, n.x(), n.y()));
                if (shape.value(QStringLiteral("closed")).toBool(false))
                    boardPts.push_back(boardPts.front());
            } else if (prim == QStringLiteral("quad")) {
                if (norm.size() != 3)
                    return fail(QStringLiteral("shapes[%1] quad 需恰 3 个控制点").arg(i));
                std::vector<QPointF> ctrl;
                for (const QPointF& n : norm)
                    ctrl.push_back(ToBoard(frame, n.x(), n.y()));
                boardPts = SampleQuad(ctrl[0], ctrl[1], ctrl[2], 16);
            } else if (prim == QStringLiteral("cubic")) {
                if (norm.size() != 4)
                    return fail(QStringLiteral("shapes[%1] cubic 需恰 4 个控制点").arg(i));
                std::vector<QPointF> ctrl;
                for (const QPointF& n : norm)
                    ctrl.push_back(ToBoard(frame, n.x(), n.y()));
                boardPts = SampleCubic(ctrl[0], ctrl[1], ctrl[2], ctrl[3], 20);
            } else {
                return fail(QStringLiteral("shapes[%1] 未知 prim \"%2\"").arg(i).arg(prim));
            }
        }

        strokes.push_back(MakeStroke(boardPts, color, width));
    }
    return strokes;
}

}  // namespace ai
