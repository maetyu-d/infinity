#pragma once
#include "Core.hpp"

namespace infinity {
// Read-only optics and estimates. These never move a body or change a route.
double warpExitEstimate(double range, double dropRange, double closingSpeed, double speedLimit,
                        double clearanceOffset = -1);
double warpDestinationReveal(double range, double dropRange, double bodyRadius);
double warpTunnelAperture(double reveal, double angularRadius, double verticalFov);
} // namespace infinity
