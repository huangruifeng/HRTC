#pragma once
#include "Whiteboard/geometry/point.h"
#include "Whiteboard/geometry/rect.h"
#include <cmath>

namespace whiteboard {

class Linesegment
{
public:
    Linesegment(Point b, Point e) : start(b), end(e)
    {}

    // Caller must ensure the segment intersects the rect; otherwise the returned point is a default value.
    Point Intersection(const Rect& rc)
    {
        Point ret;

        if (Intersection(Linesegment{ rc.GetBottomLeft(), rc.GetTopLeft() }, ret))
        {
            return ret;
        }
        if (Intersection(Linesegment{ rc.GetBottomRight(), rc.GetTopRight() }, ret))
        {
            return ret;
        }
        if (Intersection(Linesegment{ rc.GetTopLeft(), rc.GetTopRight() }, ret))
        {
            return ret;
        }
        if (Intersection(Linesegment{ rc.GetBottomLeft(), rc.GetBottomRight() }, ret))
        {
            return ret;
        }
        return start;
    }

    // Detect segment intersection and compute the intersection point.
    bool Intersection(const Linesegment& L2, Point& point)
    {
        float x, y = 0.0;
        const auto result = GetLineIntersection((float)start.x, (float)start.y, (float)end.x, (float)end.y,
            (float)L2.start.x, (float)L2.start.y, (float)L2.end.x, (float)L2.end.y, &x, &y);
        if (result == 1)
        {
            point.x = (int)std::round(x);
            point.y = (int)std::round(y);
        }
        return result == 1;
    }

private:
    int GetLineIntersection(float p0_x, float p0_y, float p1_x, float p1_y,
        float p2_x, float p2_y, float p3_x, float p3_y, float* i_x, float* i_y) const
    {
        const auto s10_x = p1_x - p0_x;
        const auto s10_y = p1_y - p0_y;
        const auto s32_x = p3_x - p2_x;
        const auto s32_y = p3_y - p2_y;

        const auto denom = s10_x * s32_y - s32_x * s10_y;
        if (denom == 0) // parallel or collinear
            return 0; // Collinear
        const auto denomPositive = denom > 0;

        const auto s02_x = p0_x - p2_x;
        const auto s02_y = p0_y - p2_y;
        const auto s_numer = s10_x * s02_y - s10_y * s02_x;
        if ((s_numer < 0) == denomPositive) // numerator and denominator must share sign with numerator <= denominator
            return 0; // No collision

        const auto t_numer = s32_x * s02_y - s32_y * s02_x;
        if ((t_numer < 0) == denomPositive)
            return 0; // No collision

        if (fabs(s_numer) > fabs(denom) || fabs(t_numer) > fabs(denom))
            return 0; // No collision
        // Collision detected
        const auto t = t_numer / denom;
        if (i_x != nullptr)
            *i_x = p0_x + (t * s10_x);
        if (i_y != nullptr)
            *i_y = p0_y + (t * s10_y);

        return 1;
    }

    Point start;
    Point end;
};

}