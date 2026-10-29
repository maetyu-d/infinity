#include "infinite/WindowLayout.hpp"
#include <iostream>
#include <stdexcept>
using namespace infinite;
static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        for (auto desktop : {WindowRect{0, 25, 1440, 775}, WindowRect{0, 30, 800, 540},
                             WindowRect{-1920, -200, 1920, 1000}, WindowRect{1440, 0, 1200, 1800}})
            for (auto borders : {WindowBorders{32, 0, 0, 0}, WindowBorders{40, 8, 8, 8}, WindowBorders{}})
                for (auto requested :
                     {WindowRect{-10000, 10000, 1440, 900}, WindowRect{10000, -10000, 7680, 4320},
                      WindowRect{0, 0, 640, 480}, WindowRect{0, 0, 600, 1000}})
                    for (bool center : {false, true}) {
                        const auto result = fitWindow(requested, desktop, borders, center);
                        check(result.x - borders.left >= desktop.x && result.y - borders.top >= desktop.y &&
                                  result.x + result.w + borders.right <= desktop.x + desktop.w &&
                                  result.y + result.h + borders.bottom <= desktop.y + desktop.h,
                              "Window decorations escape the desktop work area");
                        check(result.w > 0 && result.h > 0 && result.w <= requested.w &&
                                  result.h <= requested.h,
                              "Fitting unexpectedly enlarged or collapsed the client");
                        check(std::abs(double(result.w) / result.h - double(requested.w) / requested.h) < .01,
                              "Fitting distorted the requested aspect");
                        const auto repeated = fitWindow(result, desktop, borders, false);
                        check(result.x == repeated.x && result.y == repeated.y && result.w == repeated.w &&
                                  result.h == repeated.h,
                              "Display-change fitting repeatedly moves or shrinks a fitted window");
                    }
        std::cout
            << "PASS: desktop work areas, decorations, small/portrait/multiple displays and stable refits\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
