#pragma once
#include <QColor>
#include <cstdint>

// 白板库 color 为 uint32_t，沿用旧 demo 的 COLORREF 语义 (0x00BBGGRR)；
// bit24-31 为可选 alpha（荧光笔 = 0x4D 约 30% 透明度；普通笔恒 0 = 不透明）
inline QColor colorToQColor(uint32_t color) {
    QColor c(static_cast<int>(color & 0xFFu),
             static_cast<int>((color >> 8) & 0xFFu),
             static_cast<int>((color >> 16) & 0xFFu));
    const int alpha = static_cast<int>((color >> 24) & 0xFFu);
    if (alpha > 0)
        c.setAlpha(alpha);
    return c;
}

inline uint32_t qColorToColorRef(const QColor& c) {
    return (static_cast<uint32_t>(c.red() & 0xFF)) |
           (static_cast<uint32_t>(c.green() & 0xFF) << 8) |
           (static_cast<uint32_t>(c.blue() & 0xFF) << 16);
}
