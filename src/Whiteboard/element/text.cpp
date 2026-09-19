#include "Whiteboard/element/text.h"
#include <chrono>

namespace whiteboard {

TextElement::TextElement()
{
    Reset();
}

void TextElement::Reset()
{
    std::chrono::system_clock::duration d =
        std::chrono::system_clock::now().time_since_epoch();
    id = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
    text.clear();
    x = 0;
    y = 0;
    fontSize = 32;
    rotation = 0.0f;
    color = 0x00FFFFFF;
    bounds = Rect();
}

}
