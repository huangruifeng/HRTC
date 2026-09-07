#pragma once
#include "Whiteboard/geometry/point.h"
#include <cmath>

namespace whiteboard {

// Combines scale, translation and rotation into a single world<->screen mapping:
//   screen = Rotate(rotation) * (world * scale) + translate
// Rotation is stored in radians and applied counter-clockwise around the origin.
class Transform {
public:
    float scale = 1.0f;
    float translateX = 0.0f;
    float translateY = 0.0f;
    float rotation = 0.0f;

    Point WorldToScreen(const Point& p) const {
        const float sx = p.x * scale;
        const float sy = p.y * scale;
        const float c = std::cos(rotation);
        const float s = std::sin(rotation);
        const float rx = sx * c - sy * s;
        const float ry = sx * s + sy * c;
        return Point(static_cast<int>(std::round(rx + translateX)),
                     static_cast<int>(std::round(ry + translateY)));
    }

    Point ScreenToWorld(const Point& p) const {
        const float dx = p.x - translateX;
        const float dy = p.y - translateY;
        const float c = std::cos(rotation);
        const float s = std::sin(rotation);
        const float wx = dx * c + dy * s;
        const float wy = -dx * s + dy * c;
        return Point(static_cast<int>(std::round(wx / scale)),
                     static_cast<int>(std::round(wy / scale)));
    }

    void ZoomAt(float factor, const Point& anchor) {
        const float c = std::cos(rotation);
        const float s = std::sin(rotation);
        const float ax = anchor.x * scale;
        const float ay = anchor.y * scale;
        const float sx = ax * c - ay * s + translateX;
        const float sy = ax * s + ay * c + translateY;
        scale *= factor;
        const float nx = anchor.x * scale;
        const float ny = anchor.y * scale;
        translateX = sx - (nx * c - ny * s);
        translateY = sy - (nx * s + ny * c);
    }

    void PanBy(float dx, float dy) { translateX += dx; translateY += dy; }
    void RotateBy(float radians) { rotation += radians; }

    void Reset() { scale = 1.0f; translateX = 0.0f; translateY = 0.0f; rotation = 0.0f; }

};

}