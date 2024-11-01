#pragma once
#include <vector>
#include <list>
#include <string>
#include <cstdint>
#include <cmath>
#include <chrono>
namespace whiteboard
{
template<class T>
class PointTemplate {
public:
    T x, y;
    bool insert;
    bool IsInsert(){return insert;}
    PointTemplate() : x(0), y(0),insert(false) { }
    PointTemplate(int xx, int yy,bool in = false) : x(xx), y(yy),insert(in) { }
    PointTemplate<T>& operator+=(const PointTemplate<T>& p) { x += p.x; y += p.y; return *this; }
    PointTemplate<T>& operator-=(const PointTemplate<T>& p) { x -= p.x; y -= p.y; return *this; }
    uint32_t static  Distance(const PointTemplate<T>& pt1, const PointTemplate<T>& pt2)
    {
        return (uint32_t)std::sqrt(std::pow((pt2.x - pt1.x), 2) + std::pow((pt2.y - pt1.y), 2));
    }
};

template<class T>
// comparison
inline bool operator==(const PointTemplate<T>& p1, const PointTemplate<T>& p2)
{
    return p1.x == p2.x && p1.y == p2.y;
}
template<class T>
inline bool operator!=(const PointTemplate<T>& p1, const PointTemplate<T>& p2)
{
    return !(p1 == p2);
}

using Point = PointTemplate<int>;
using FloatPoint = PointTemplate<float>;
template<>
// comparison
inline bool operator==(const FloatPoint& p1, const FloatPoint& p2)
{
    return (abs(p1.x - p2.x)<1e-8) && abs(p1.y - p2.y) < 1e-8;
}

class Rect
{
public:
    Rect()
        : x(0), y(0), width(0), height(0)
        { }
    Rect(int xx, int yy, int ww, int hh)
        : x(xx), y(yy), width(ww), height(hh)
        { }

    int GetX() const { return x; }
    void SetX(int xx) { x = xx; }

    int GetY() const { return y; }
    void SetY(int yy) { y = yy; }

    int GetWidth() const { return width; }
    void SetWidth(int w) { width = w; }

    int GetHeight() const { return height; }
    void SetHeight(int h) { height = h; }

    Point GetPosition() const { return Point(x, y); }
    void SetPosition( const Point &p ) { x = p.x; y = p.y; }

    bool IsEmpty() const { return (width <= 0) || (height <= 0); }

    int GetLeft()   const { return x; }
    int GetTop()    const { return y; }
    int GetBottom() const { return y + height - 1; }
    int GetRight()  const { return x + width - 1; }

    void SetLeft(int left) { x = left; }
    void SetRight(int right) { width = right - x + 1; }
    void SetTop(int top) { y = top; }
    void SetBottom(int bottom) { height = bottom - y + 1; }

    Point GetTopLeft() const { return GetPosition(); }
    Point GetLeftTop() const { return GetTopLeft(); }
    void SetTopLeft(const Point &p) { SetPosition(p); }
    void SetLeftTop(const Point &p) { SetTopLeft(p); }

    Point GetBottomRight() const { return Point(GetRight(), GetBottom()); }
    Point GetRightBottom() const { return GetBottomRight(); }
    void SetBottomRight(const Point &p) { SetRight(p.x); SetBottom(p.y); }
    void SetRightBottom(const Point &p) { SetBottomRight(p); }

    Point GetTopRight() const { return Point(GetRight(), GetTop()); }
    Point GetRightTop() const { return GetTopRight(); }
    void SetTopRight(const Point &p) { SetRight(p.x); SetTop(p.y); }
    void SetRightTop(const Point &p) { SetTopRight(p); }

    Point GetBottomLeft() const { return Point(GetLeft(), GetBottom()); }
    Point GetLeftBottom() const { return GetBottomLeft(); }
    void SetBottomLeft(const Point &p) { SetLeft(p.x); SetBottom(p.y); }
    void SetLeftBottom(const Point &p) { SetBottomLeft(p); }

    // return true if the point is (not strcitly) inside the rect
    bool Contains(int cx, int cy) const{
        return ( (cx >= x) && (cy >= y)
          && ((cy - y) < height)
          && ((cx - x) < width));
    }
    bool Contains(const Point& pt) const { return Contains(pt.x, pt.y); }
    // return true if the rectangle 'rect' is (not strictly) inside this rect
    bool Contains(const Rect& rect) const {
        return Contains(rect.GetTopLeft()) && Contains(rect.GetBottomRight());
    }

    Rect& Intersect(const Rect& rect) {
        int x2 = GetRight(),
            y2 = GetBottom();

        if ( x < rect.x )
            x = rect.x;
        if ( y < rect.y )
            y = rect.y;
        if ( x2 > rect.GetRight() )
            x2 = rect.GetRight();
        if ( y2 > rect.GetBottom() )
            y2 = rect.GetBottom();

        width = x2 - x + 1;
        height = y2 - y + 1;

        if ( width <= 0 || height <= 0 )
        {
            width =
            height = 0;
        }

        return *this;
    }

    Rect Intersect(const Rect& rect) const {
        Rect r = *this;
        r.Intersect(rect);
        return r;
    }

    // return true if the rectangles have a non empty intersection
    bool Intersects(const Rect& rect) const {
        Rect r = Intersect(rect);

        // if there is no intersection, both width and height are 0
        return r.width != 0;
    }

public:
    int x, y, width, height;
};


class Linesegment
{
public:
    Linesegment(Point b,Point e):start(b),end(e)
    {}

    //调用此函数前必须确认线段与矩形是相交的，不然计算出的点是默认值
    Point Intersection(const Rect& rc)
    {
        Point ret;

        if (Intersection(Linesegment{ rc.GetBottomLeft(),rc.GetTopLeft()}, ret))
        {
            return ret;
        }
        if(Intersection(Linesegment{ rc.GetBottomRight(), rc.GetTopRight()}, ret))
        {
            return ret;
        }
        if(Intersection(Linesegment{ rc.GetTopLeft(),  rc.GetTopRight() },ret))
        {
            return ret;
        }
        if(Intersection(Linesegment{ rc.GetBottomLeft(),  rc.GetBottomRight() }, ret))
        {
            return ret;
        }
        return start;
    }

    //判断线段是否相交，并计算出交点
    bool Intersection(const Linesegment& L2,Point& point)
    {
        float x, y = 0.0;
        const auto result = GetLineIntersection((float)start.x, (float)start.y, (float)end.x, (float)end.y, (float)L2.start.x, (float)L2.start.y, (float)L2.end.x, (float)L2.end.y,&x,&y);
        if(result == 1)
        {
            point.x = (int)std::round(x);
            point.y =(int)std::round(y);
        }
        return result==1;
    }
private:
    int GetLineIntersection(float p0_x, float p0_y, float p1_x, float p1_y,
        float p2_x, float p2_y, float p3_x, float p3_y, float *i_x, float *i_y) const
    {
        const auto s10_x = p1_x - p0_x;
        const auto s10_y = p1_y - p0_y;
        const auto s32_x = p3_x - p2_x;
        const auto s32_y = p3_y - p2_y;

        const auto denom = s10_x * s32_y - s32_x * s10_y;
        if (denom == 0)//平行或共线
            return 0; // Collinear
        const auto denomPositive = denom > 0;

        const auto s02_x = p0_x - p2_x;
        const auto s02_y = p0_y - p2_y;
        const auto s_numer = s10_x * s02_y - s10_y * s02_x;
        if ((s_numer < 0) == denomPositive)//参数是大于等于0且小于等于1的，分子分母必须同号且分子小于等于分母
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


struct BoundaryRect
{ 
#define DEFAULTMINVALUE 9999999
    BoundaryRect():minx(DEFAULTMINVALUE),maxx(0),miny(DEFAULTMINVALUE),maxy(0){}
    BoundaryRect(const Rect &rx):minx(rx.x),maxx(rx.x + rx.width),miny(rx.y),maxy(rx.y + rx.height){}
    Rect ToRect() const{
        int w = maxx - minx;
        int h = maxy - miny;
        if(0 == w)
            w = 1;
        if(0 == h)
            h = 1;
        return {minx,miny,w,h};
    }
    void Reset(){
        minx = DEFAULTMINVALUE;
        miny = DEFAULTMINVALUE;
        maxx = 0;
        maxy = 0;
    }
    void Update(int x, int y)
    {
        if(x > maxx)
            maxx = x;
        if(x < minx)
            minx = x;
        if(y > maxy)
            maxy = y;
        if(y < miny)
            miny = y;
    }

public:
    int minx, maxx,miny,maxy;
};

class Path {
public:
	Path(){
        enableInsertInternalPoint = true;
		Reset();
	}
	Path(const Path &other)
	{
		id = other.id;
		width = other.width;
		color = other.color;
		points = other.points;
        bounding = other.bounding;
        minDistance = other.minDistance;
	}
    void SetMinDistance(int min){
        minDistance = min;
    };
    void Append(const Point& point){
        InsertPoint(points,point,minDistance);
        points.push_back(point);
        bounding.Update(point.x,point.y);
    }

    void EnablePointInsert(bool enable) {
        enableInsertInternalPoint = enable;
    }

    bool PointInsertEnabled() {
        return enableInsertInternalPoint;
    }

    bool operator==(const Path& p){
        return id == p.id;
    }

    bool operator!=(const Path&p){
        return !(*this == p);
    }

	void Reset()
	{
        std::chrono::system_clock::duration d =
            std::chrono::system_clock::now().time_since_epoch();
        id = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
		width = 0;
		color = 0;
		points.clear();
        bounding = Rect{0,0,0,0};
        minDistance = 20;
	}

    static void InsertPoint(std::vector<Point>& points, const Point&end, uint32_t minDistance)
    {
        if (points.empty())
            return;
        auto begin = *points.rbegin();
        auto distance = Point::Distance(begin,end);
        if (distance > minDistance)
        {
            const auto count = static_cast<int>(distance / minDistance);
            std::list<Point> path;
            for (auto i = 0; i < count; i++)
            {
                const auto pt = GetInterPoint({begin.x,begin.y}, {end.x,end.y}, minDistance*(count - i));
                if (path.empty() && pt == *points.rbegin())
                    continue;
                if (!path.empty() && pt == *path.rbegin())
                    continue;
                path.push_back({ static_cast<int>(pt.x),{static_cast<int>(pt.y)},true });
            }
            points.insert(points.end(),path.rbegin(),path.rend());
        }
    }

    static Point GetInterPoint(const FloatPoint& begin, const FloatPoint& end, uint32_t distance)
    {
        if (std::abs(end.x - begin.x) < 0.0001) //斜率不存在的情况
        {
            if ((end.y - begin.y) >= 0)
            {
                return { (int)std::round(begin.x), (int)std::round(begin.y) + (int)distance };
            }
            else
            {
                return { (int)std::round(begin.x), (int)std::round(begin.y) - (int)distance };
            }
        }
        else
        {
            const auto k = (end.y - begin.y) / (end.x - begin.x);
            const auto b = begin.y - k * begin.x;
            /*
            * 勾股定理 distance2 = x2 + y2
            * y = kx + b
            * 配方法解一元二次方程
            */
            const auto A = std::pow(k, 2) + 1;
            const auto B = 2 * ((b - begin.y)*k - begin.x);

            const auto C = std::pow(b - begin.y, 2) + std::pow(begin.x, 2) - std::pow(distance, 2);

            const auto x1 = (-B + std::sqrt(std::pow(B, 2) - 4 * A * C)) / (2 * A);
            const auto x2 = (-B - std::sqrt(std::pow(B, 2) - 4 * A * C)) / (2 * A);
            auto x = x1;

            if (x1 == x2) {
                x = x1;
            }
            else if (begin.x <= x1 && x1 <= end.x || end.x <= x1
                && x1 <= begin.x) {
                x = x1;
            }
            else if (begin.x <= x2 && x2 <= end.x || end.x <= x2
                && x2 <= begin.x) {
                x = x2;
            }

            const auto y = k * x + b;

            return { (int)x, (int)y};
        }
    }

	std::string						id;
	int								width;
	uint32_t						color;
	std::vector<Point>			    points;
    int                             minDistance;
    //笔迹的外接矩形，在擦除的时候用来判断橡皮檫与笔迹是否相交，用以减少遍历的点数
    BoundaryRect                    bounding;
    bool                            enableInsertInternalPoint;
};

class Page {
public:
    void Append(const Path& p){
        paths.push_back(p);
    }

    Page() {
        scale = 1.0;
        EnableEraserInsert(false);
    };

    Page(const Page& p) {
        pageId = p.pageId;
        paths = p.paths;
        scale = p.scale;
    }

    void Delete(const Path&p){
        auto it = std::find(paths.begin(),paths.end(),p);
        if(it != paths.end()){
            paths.erase(it);
        }
    }

    void Clear() {
        paths.clear();
    }

    void EnableEraserInsert(bool enable) {
        eraserSessionId = -1;
        enableEraserInsert = enable;
        insertInstance = 5;
    }

    bool EraserInsertEnabled() {
        return enableEraserInsert;
    }

    void Eraser(const Rect& rc, int sid) {
        if (enableEraserInsert) {

            if (sid != eraserSessionId) {
                insertPoints.clear();
                eraserSessionId = sid;
            }
            else {
                if (!insertPoints.empty()) {
                    Path::InsertPoint(insertPoints, rc.GetLeftTop(), insertInstance);
                }
            }
            
            insertPoints.push_back(rc.GetLeftTop());
        }

        for (auto& p : insertPoints) {
            Eraser({p.x,p.y,rc.width,rc.height});
        }

        if (enableEraserInsert) {
            insertPoints.clear();
            insertPoints.push_back(rc.GetLeftTop());
        }

    }

    void Eraser(const Rect& rc){
        for(auto it = paths.begin(); it != paths.end();)
        {
            if(!rc.Intersects(it->bounding.ToRect()))
            {
                ++it;
                continue;
            }

            auto &points = it->points;
            bool contain = rc.Contains(*points.begin());
            const auto firstContain = contain;
            bool lastContained = contain;

            //这里记算线段与矩形的交点，用以后续新线段的生成
            std::vector<std::vector<Point>::iterator> partPoints;
            if (!firstContain)
            {
                partPoints.push_back(points.begin());
            }
            for (auto p = points.begin(); p != points.end(); ++p)
            {
                contain = rc.Contains(*p);
                if (lastContained != contain)
                {
                    partPoints.push_back(p);
                    lastContained = contain;
                }
            }

            if (partPoints.empty())
            {
                it = paths.erase(it);
            }
            else if (1 == partPoints.size() && !firstContain)
            {
                ++it;
            }
            else
            {
                std::vector<Path> newPaths;
                if(partPoints.size()>1)
                {
                    for(auto i = 0; i+1 < partPoints.size(); i+=2)
                    {
                        Path newPath;
                        newPath.color = it->color;
                        newPath.width = it->width;

                        std::chrono::system_clock::duration d = 
                            std::chrono::system_clock::now().time_since_epoch();
                        newPath.id = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count()); 
                    
                        if (!firstContain && i == 0)
                        {
                            newPath.points.resize(partPoints[i + 1] - partPoints[i] + 1);
        
                            auto begin = newPath.points.begin();

                            while (partPoints[0] != partPoints[1])
                            {
                                newPath.bounding.Update(partPoints[0]->x, partPoints[0]->y);
                                *begin++ = *partPoints[0]++;
                            }

                            Point point = PointOfIntersection(partPoints[1],rc);
                            
                            *begin = point;
                            newPath.bounding.Update(point.x, point.y);
                        }
                        else 
                        {
                            newPath.points.resize(partPoints[i + 1] - partPoints[i] + 2);

                            auto begin = newPath.points.begin();
                            Point bpoint = PointOfIntersection(partPoints[i], rc);
                            newPath.bounding.Update(bpoint.x, bpoint.y);
                            *begin++ = bpoint;
                            while (partPoints[i] != partPoints[i + 1])
                            { 
                                newPath.bounding.Update(partPoints[i]->x, partPoints[i]->y);
                                *begin++ = *partPoints[i]++;
                            }

                            Point epoint = PointOfIntersection(partPoints[i + 1], rc);
                            
                            newPath.bounding.Update(epoint.x, epoint.y);
                            *begin = epoint;
                        }

                        newPaths.emplace_back(newPath);
                    }
                }

                const auto endConatain = rc.Contains(*points.rbegin());
                if (!endConatain)
                {
                    Path newPath;
                    newPath.color = it->color;
                    newPath.width = it->width;
                    std::chrono::system_clock::duration d = 
                            std::chrono::system_clock::now().time_since_epoch();
                    newPath.id = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count()); 
                    auto oBegin = *partPoints.rbegin();

                    newPath.points.resize(points.end() - 1 - oBegin + 2);//加两个点
                    Point point = point = PointOfIntersection(oBegin, rc);;

                    auto begin = newPath.points.begin();
                    *begin++ = point;
                    newPath.bounding.Update(point.x, point.y);
                    
                    while(oBegin != points.end())
                    {
                        newPath.bounding.Update(oBegin->x, oBegin->y);
                        *begin++ = *oBegin++;
                    }
                    newPaths.emplace_back(newPath);
                }

                if(!newPaths.empty())
                {
                    it = paths.erase(it);
                    it = paths.insert(it, newPaths.begin(), newPaths.end());
                    int i = 0;
                    while(i < newPaths.size())
                    {
                        ++it;
                        ++i;
                    }
                }else
                {
                    ++it;
                }
            }
        }
    }

    std::vector<Path> GetIntersectPath(const Rect& r){
        std::vector<Path> ret;
        for(auto& pa : paths){
            if(r.Intersects(pa.bounding.ToRect())){
                ret.push_back(pa);
            }
        }
        return std::move(ret);
    }
    
    std::string pageId;
    std::list<Path> paths;
    float scale;
    bool enableEraserInsert;
    int eraserSessionId;
    int insertInstance;
    std::vector<Point> insertPoints;
private:
    Point PointOfIntersection(const std::vector<Point>::iterator& it,const Rect& orc)
    {
        Rect rc{ orc.x - 1,orc.y - 1,orc.width + 2,orc.height + 2 };
        return (Linesegment(*(it-1), *it).Intersection(orc));
    }
};
}
