#pragma once
#include "Core.hpp"
namespace infinite {
// SI properties of the existing 12 m hull. Shrinking is an explicit field:
// m~s^3, I~s^5, F~s^4 and torque~s^5; render magnification is not an input.
struct ShipProperties {
    double massKg = 12000;
    glm::dvec3 inertia{153000, 160000, 25000};
    glm::dvec3 forceCaps{528000, 576000, 624000}; // +/-X, +/-Y, forward -Z
    double reverseForce = 384000;
    glm::dvec3 torqueCaps{994500, 1040000, 237500};
    glm::dvec3 faceAreas{36, 48, 12};
    double dragCoefficient = .65;
};
ShipProperties shipProperties(double sizeRatio = 1, double boost = 0);
struct ShipWrench {
    glm::dvec3 linear{}, angular{}; // normalized actuator demand
};
// Gravity support has priority. Translation and attitude share one bounded
// manoeuvring envelope; the forward main engine has its own force limit.
ShipWrench allocateShipWrench(const ShipProperties&, glm::dvec3 supportNewtons, glm::dvec3 commandedNewtons,
                              glm::dvec3 commandedTorque);
glm::dvec3 shipForce(const ShipProperties&, glm::dvec3 normalized);
glm::dvec3 shipTorque(const ShipProperties&, glm::dvec3 normalized);
void integrateShipRotation(glm::dquat& attitude, glm::dvec3& bodyRate, glm::dvec3 inertia,
                           glm::dvec3 bodyTorque, double seconds);
glm::dvec3 pointGravity(glm::dvec3 deltaMetres, double mu, double radiusMetres);
// Implicit quadratic drag, returned as force: cannot reverse air-relative
// velocity or add energy, even during a fast atmospheric entry.
glm::dvec3 atmosphericDrag(const ShipProperties&, glm::dquat attitude, glm::dvec3 airRelativeVelocity,
                           double density, double seconds);
} // namespace infinite
