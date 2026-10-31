#include "infinite/ShipDynamics.hpp"
#include <algorithm>
#include <cmath>
namespace infinite {
ShipProperties shipProperties(double size, double boost, double payloadKg) {
    ShipProperties p;
    const double s = std::clamp(size, 1e-12, 1.), m = s * s * s, i = m * s * s;
    p.massKg *= m;
    p.inertia *= i;
    p.forceCaps *= m * s;
    p.reverseForce *= m * s;
    p.forceCaps.z *= 1 + 1.4 * std::clamp(boost, 0., 1.);
    p.torqueCaps *= i;
    p.faceAreas *= s * s;
    // Full-size cargo is secured in a central 4 x 3 x 6 m bay.
    const double cargo = std::clamp(payloadKg, 0., double(maxShipPayloadKg));
    p.massKg += cargo;
    p.inertia += cargo * glm::dvec3(3.75, 52. / 12., 25. / 12.);
    return p;
}
namespace {
glm::dvec3 forceDemand(const ShipProperties& p, glm::dvec3 f) {
    return {f.x / p.forceCaps.x, f.y / p.forceCaps.y, f.z / (f.z < 0 ? p.forceCaps.z : p.reverseForce)};
}
double manoeuvreLoad(glm::dvec3 f, glm::dvec3 t) {
    return f.x * f.x + f.y * f.y + std::pow(std::max(0., f.z), 2) + glm::dot(t, t);
}
} // namespace
ShipWrench allocateShipWrench(const ShipProperties& p, glm::dvec3 support, glm::dvec3 command,
                              glm::dvec3 torque) {
    auto s = forceDemand(p, support), c = forceDemand(p, command), t = torque / p.torqueCaps;
    s.z = std::clamp(s.z, -1., 1.);
    double load = manoeuvreLoad(s, {});
    if (load > 1)
        s /= std::sqrt(load);
    const double torqueLength = glm::length(t), room = std::sqrt(std::max(0., 1 - manoeuvreLoad(s, {})));
    if (torqueLength > room && torqueLength > 0)
        t *= room / torqueLength;
    double lo = 0, hi = 1;
    for (unsigned i = 0; i < 18; i++) {
        const double k = (lo + hi) * .5;
        if (manoeuvreLoad(s + c * k, t) <= 1)
            lo = k;
        else
            hi = k;
    }
    if (manoeuvreLoad(s + c, t) <= 1)
        lo = 1;
    auto f = s + c * lo;
    f.z = std::clamp(f.z, -1., 1.);
    return {f, t};
}
glm::dvec3 shipForce(const ShipProperties& p, glm::dvec3 n) {
    return {n.x * p.forceCaps.x, n.y * p.forceCaps.y, n.z * (n.z < 0 ? p.forceCaps.z : p.reverseForce)};
}
glm::dvec3 shipTorque(const ShipProperties& p, glm::dvec3 n) {
    return n * p.torqueCaps;
}
void integrateShipRotation(glm::dquat& q, glm::dvec3& w, glm::dvec3 inertia, glm::dvec3 torque, double dt) {
    if (!(dt > 0))
        return;
    const auto momentum = q * (inertia * w);
    auto mid = w;
    glm::dquat half = q;
    glm::dvec3 nextMomentum = momentum;
    for (unsigned iteration = 0; iteration < 4; iteration++) {
        const double angle = glm::length(mid) * dt * .5;
        half = angle > 1e-20 ? glm::normalize(q * glm::angleAxis(angle, glm::normalize(mid))) : q;
        nextMomentum = momentum + (half * torque) * dt;
        mid = (glm::inverse(half) * ((momentum + nextMomentum) * .5)) / inertia;
    }
    const double angle = glm::length(mid) * dt;
    if (angle > 1e-20)
        q = glm::normalize(q * glm::angleAxis(angle, glm::normalize(mid)));
    w = (glm::inverse(q) * nextMomentum) / inertia;
}
glm::dvec3 pointGravity(glm::dvec3 delta, double mu, double radius) {
    const double distance = glm::length(delta);
    if (distance < 1e-20 || !(mu > 0))
        return {};
    // A bounded uniform-density interior prevents a central singularity.
    const double r = std::max(distance, std::max(1e-15, radius));
    return delta * (mu / (r * r * r));
}
glm::dvec3 atmosphericDrag(const ShipProperties& p, glm::dquat q, glm::dvec3 v, double density, double dt) {
    const double speed = glm::length(v);
    if (!(density > 0) || speed < 1e-30 || !(dt > 0))
        return {};
    const auto d = glm::abs(glm::inverse(q) * (v / speed));
    const double area = glm::dot(d, p.faceAreas), k = .5 * density * p.dragCoefficient * area / p.massKg;
    return -v * (p.massKg / dt) * (k * speed * dt / (1 + k * speed * dt));
}
} // namespace infinite
