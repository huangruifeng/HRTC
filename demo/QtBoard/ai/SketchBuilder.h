#pragma once

#include <QJsonArray>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

#include "Whiteboard/whiteboard.h"

namespace ai {

// ---------- 颜色工具 ----------
// "#RRGGBB" / "#AARRGGBB" / 颜色名 → COLORREF 语义 uint32_t（bit24-31 为可选 alpha）；
// 非法输入返回 fallback
uint32_t ColorFromHex(const QString& hex, uint32_t fallback);
// COLORREF → "#RRGGBB"（不含 alpha，AI 展示用）
QString HexFromColor(uint32_t color);

// ---------- RDP 抽稀 ----------
// Ramer–Douglas–Peucker 抽稀（epsilon 自适应放大直到 ≤ maxPoints），
// 仍超限时退化为等距抽样；首尾点保留。maxPoints < 2 或点数已达标时原样返回。
std::vector<whiteboard::Point> SimplifyPoints(const std::vector<whiteboard::Point>& points,
                                              int maxPoints);

// ---------- 绘图 DSL → Stroke ----------
// shapes 数组元素（坐标均为 frame 内归一化 0~1，(0,0) = 左上角）：
//   { "prim": "polyline" | "quad" | "cubic" | "ellipse",
//     "points": [[u,v], ...],            // polyline ≥2 点；quad 恰 3 个控制点；
//                                        // cubic 恰 4 个控制点
//     "center": [u,v], "rx": r, "ry": r, // ellipse：归一化中心与半径（rx 相对宽、
//                                        // ry 相对高）
//     "closed": bool,                    // 可选：首尾闭合（polyline 用，末点接回首点）
//     "color": "#RRGGBB", "width": int } // 可选：缺省用 defaultColor / defaultWidth
// 采样：polyline 原样；quad 二次贝塞尔 16 段；cubic 三次贝塞尔 20 段；
// ellipse 48 段闭合。每条 shape → 一个 Stroke（Reset + 逐点 Append，自动维护
// points/rawPoints/bounding）。任一条解析失败返回空列表并写 error。
std::vector<std::shared_ptr<whiteboard::Stroke>> BuildStrokes(const QJsonArray& shapes,
                                                              const QRectF& frame,
                                                              uint32_t defaultColor,
                                                              int defaultWidth,
                                                              QString* error);

}  // namespace ai
