#pragma once
#include <algorithm>
#include <cmath>

namespace infinite {
struct WindowRect {
    int x = 0, y = 0, w = 1, h = 1;
};
struct WindowBorders {
    int top = 0, left = 0, bottom = 0, right = 0;
};
// Desktop/window coordinates, deliberately independent of framebuffer pixels.
// Include decorations and a small margin; retain the requested client aspect.
inline WindowRect fitWindow(WindowRect requested, WindowRect usable, WindowBorders borders,
                            bool center = true) {
    const int margin = std::min(16, std::max(0, std::min(usable.w, usable.h) / 16));
    const int availableW = std::max(1, usable.w - borders.left - borders.right - margin * 2);
    const int availableH = std::max(1, usable.h - borders.top - borders.bottom - margin * 2);
    const double factor = std::min(
        {1., double(availableW) / std::max(1, requested.w), double(availableH) / std::max(1, requested.h)});
    WindowRect result{requested.x, requested.y,
                      std::clamp(int(std::floor(std::max(1, requested.w) * factor)), 1, availableW),
                      std::clamp(int(std::floor(std::max(1, requested.h) * factor)), 1, availableH)};
    const int left = usable.x + margin + borders.left, top = usable.y + margin + borders.top;
    result.x = center ? left + (availableW - result.w) / 2
                      : std::clamp(result.x, left, left + availableW - result.w);
    result.y =
        center ? top + (availableH - result.h) / 2 : std::clamp(result.y, top, top + availableH - result.h);
    return result;
}
} // namespace infinite
