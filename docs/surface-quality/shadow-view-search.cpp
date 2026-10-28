#include "infinite/Navigation.hpp"
#include <iomanip>
#include <iostream>
using namespace infinite;
int main() {
    Universe u;
    auto planet = u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, 1).child(Kind::Planet, 10));
    double radius = std::pow(10., planet.frame.logRadius), best = 0;
    auto light =
        glm::normalize(u.relative(planet.address.parent(), planet.address, planet.frame.logRadius).position);
    auto east = glm::normalize(
             glm::cross(light, std::abs(light.y) < .9 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0))),
         north = glm::cross(light, east);
    glm::dvec3 chosen{}, hit{};
    for (int i = 0; i < 128; i++) {
        double angle = i * 6.283185307179586 / 128;
        auto d = glm::normalize(east * std::cos(angle) + north * std::sin(angle) + light * .065);
        auto ground = d * (radius + u.terrainHeight(planet, d));
        for (double t = 32; t < 2500; t += 24) {
            auto p = ground + light * t;
            double margin = u.terrainHeight(planet, glm::normalize(p)) - (glm::length(p) - radius);
            if (margin > best) {
                best = margin;
                chosen = d;
                hit = p;
            }
        }
    }
    if (best < 50)
        return 1;
    auto ground = chosen * (radius + u.terrainHeight(planet, chosen));
    auto forward = -glm::normalize(light - chosen * glm::dot(light, chosen));
    auto eye = ground + chosen * 300. - forward * 500.;
    auto orientation = glm::quatLookAtRH(glm::normalize(ground - eye), chosen);
    auto offset = eye / 1e4 - orientation * glm::dvec3(0, 0, 1);
    std::cout << std::setprecision(17) << planet.address.str() << " 4 " << orientation.w << ' '
              << orientation.x << ' ' << orientation.y << ' ' << orientation.z << ' ' << offset.x << ' '
              << offset.y << ' ' << offset.z << '\n';
    std::cerr << "physical_occlusion_margin_m=" << best << " ray_distance_m=" << glm::length(hit - ground)
              << " sun_dot=" << glm::dot(chosen, light) << '\n';
}
