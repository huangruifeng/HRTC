#include "Whiteboard/element/widget.h"
#include <chrono>

namespace whiteboard {

WidgetElement::WidgetElement()
{
    Reset();
}

void WidgetElement::Reset()
{
    std::chrono::system_clock::duration d =
        std::chrono::system_clock::now().time_since_epoch();
    id = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
    kind = 0;
    x = 0;
    y = 0;
    durationSec = 300;
    scale = 1.0f;
    diceSides = 6;
    diceCount = 2;
    options.clear();
    dedup = false;
    pickCount = 1;
}

}
