#pragma once
#include "infinite/WindowLayout.hpp"
#include <SDL3/SDL.h>
#include <iostream>

namespace infinite {
inline WindowBorders windowBorders(SDL_Window* window) {
    WindowBorders borders;
    // Cocoa in the pinned SDL release does not implement this query. Reserve
    // a conservative title bar and frame there (and on other unsupported WMs).
    if (!SDL_GetWindowBordersSize(window, &borders.top, &borders.left, &borders.bottom, &borders.right))
        borders = {32, 8, 8, 8};
    return borders;
}
inline bool fitWindowToDisplay(SDL_Window* window, bool center) {
    if (SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED))
        return false;
    SDL_Rect usable;
    if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &usable))
        return false;
    WindowRect current;
    SDL_GetWindowPosition(window, &current.x, &current.y);
    SDL_GetWindowSize(window, &current.w, &current.h);
    const auto borders = windowBorders(window);
    const auto fitted = fitWindow(current, {usable.x, usable.y, usable.w, usable.h}, borders, center);
    // A small desktop must not be defeated by a larger minimum client size.
    SDL_SetWindowMinimumSize(window, std::min(640, fitted.w), std::min(400, fitted.h));
    if (current.w != fitted.w || current.h != fitted.h)
        SDL_SetWindowSize(window, fitted.w, fitted.h);
    if (current.x != fitted.x || current.y != fitted.y)
        SDL_SetWindowPosition(window, fitted.x, fitted.y);
    SDL_SyncWindow(window);
    std::cout << "WINDOW_FIT client=" << fitted.x << ',' << fitted.y << ',' << fitted.w << ',' << fitted.h
              << " usable=" << usable.x << ',' << usable.y << ',' << usable.w << ',' << usable.h
              << " borders=" << borders.top << ',' << borders.left << ',' << borders.bottom << ','
              << borders.right << '\n';
    return true;
}
} // namespace infinite
