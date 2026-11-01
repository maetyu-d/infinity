#include "infinite/CockpitMotion.hpp"
#include "infinite/PointLight.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>
using namespace infinite;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
// Reference of the existing compact core and halo shader, including its
// circular support and edge taper. No invented twinkle animation is used.
double light(double x, double y, double radius, double radiance, bool compact) {
    const double r2 = (x * x + y * y) / (radius * radius), r = std::sqrt(r2);
    const double window =
        compact ? std::max(0., 1 - std::max(std::abs(x), std::abs(y)) / (radius * pointLightSupport)) : 1.;
    if (r > 1 || window <= 0)
        return 0;
    return radiance * window * (2.6 * std::exp(-48 * r2) + .16 * std::exp(-5 * r2)) *
           std::clamp((1 - r) * 5, 0., 1.);
}
double flux(double x, double y, double radius, double radiance, bool compact = false) {
    double sum = 0;
    for (int j = -16; j <= 16; ++j)
        for (int i = -16; i <= 16; ++i)
            sum += light(i - x, j - y, radius, radiance, compact);
    return sum;
}
double variation(const std::vector<double>& values) {
    const double mean = std::accumulate(values.begin(), values.end(), 0.) / values.size();
    double sum = 0;
    for (double v : values)
        sum += (v - mean) * (v - mean);
    return std::sqrt(sum / values.size()) / mean;
}
} // namespace
int main() {
    try {
        double worst = 0;
        for (double radius : {.5, 1.05, 1.5, 2., 2.7, 6., 24., 500.}) {
            const auto f = pointLightFootprint(radius);
            check(std::isfinite(f.radiusScale) && f.radiusScale >= 1 && f.radianceScale > 0 &&
                      f.radianceScale <= pointLightEnergy,
                  "Invalid optical footprint");
            check(std::abs(f.radiusScale * f.radiusScale * f.radianceScale / pointLightEnergy - 1) < 1e-12,
                  "Point filtering changes integrated core/halo energy");
            if (radius <= 2.7) {
                std::vector<double> original, filtered;
                for (unsigned i = 0; i < 32; ++i)
                    for (unsigned j = 0; j < 32; ++j) {
                        const double x = i / 32., y = j / 32.;
                        original.push_back(flux(x, y, radius, 1));
                        filtered.push_back(flux(x, y, radius * f.radiusScale, f.radianceScale, true));
                    }
                const double before = variation(original), after = variation(filtered);
                check(after < .012 && after < before * .05,
                      "Star light still flashes between subpixel phases");
                const double oldMean =
                    std::accumulate(original.begin(), original.end(), 0.) / original.size();
                const double newMean =
                    std::accumulate(filtered.begin(), filtered.end(), 0.) / filtered.size();
                check(std::abs(newMean / oldMean - 1) < .001, "Compact sprite changes mean stellar light");
                worst = std::max(worst, after);
                std::cout << "SUBPIXEL radius=" << radius << " before_cv=" << before << " after_cv=" << after
                          << '\n';
            }
        }
        for (double invalid :
             {0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            const auto f = pointLightFootprint(invalid);
            check(f.radiusScale == 1 && f.radianceScale == 1, "Invalid radius corrupts point light");
        }
        // A stationary ship still has its requested engine vibration. Exercise
        // that real motion rather than a user steering the test camera.
        CockpitMotion motion;
        CockpitMotionInput input;
        input.hullMetres = 12;
        input.hull = 100;
        input.engine = .64;
        input.acceleration = {0, 3 * 9.80665, 0};
        const auto f = pointLightFootprint(1.05);
        std::vector<double> before, after;
        for (unsigned i = 0; i < 240; ++i) {
            motion.update(input, 1. / 60);
            const auto pose = motion.pose();
            const double x = .23 + pose.angles.y * 1100, y = .34 + pose.angles.x * 1100;
            before.push_back(flux(x, y, 1.05, 1));
            after.push_back(flux(x, y, 1.05 * f.radiusScale, f.radianceScale, true));
        }
        check(variation(after) < .012 && variation(after) < variation(before) * .05,
              "Idle cockpit vibration still makes stars twinkle");
        std::cout << "IDLE_ENGINE before_cv=" << variation(before) << " after_cv=" << variation(after)
                  << '\n';
        std::cout << "PASS: flux preservation, bounded optical size, invalid inputs, subpixel sweeps and "
                     "stationary-engine stability; worst_cv="
                  << worst << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
