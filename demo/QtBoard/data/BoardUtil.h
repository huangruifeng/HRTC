#pragma once
#include <QColor>
#include <cstdint>

// 白板库 color 为 uint32_t，沿用旧 demo 的 COLORREF 语义 (0x00BBGGRR)
inline QColor colorToQColor(uint32_t color) {
    return QColor(static_cast<int>(color & 0xFFu),
                  static_cast<int>((color >> 8) & 0xFFu),
                  static_cast<int>((color >> 16) & 0xFFu));
}

inline uint32_t qColorToColorRef(const QColor& c) {
    return (static_cast<uint32_t>(c.red() & 0xFF)) |
           (static_cast<uint32_t>(c.green() & 0xFF) << 8) |
           (static_cast<uint32_t>(c.blue() & 0xFF) << 16);
}
