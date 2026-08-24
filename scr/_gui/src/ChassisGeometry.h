#pragma once

#include <cmath>

namespace ChassisGeometry {

constexpr double magnetClearancePx = 20.0;
constexpr double workspaceValleyScale = 1.0;
constexpr double valleyRoundness = 1.45;
constexpr double peakFlattening = 0.85;

inline double lobeShape(double theta)
{
    const double phase = 0.5 * (1.0 + std::cos(6.0 * theta));
    const double rounded = std::pow(phase, valleyRoundness);

    // Preserve exactly 0 and 1, so valley and peak clearances do not move.
    // Raising only the shoulders makes the same-height peak broader and less
    // pointed without reducing the minimum gap outside the magnet housing.
    return rounded
        + peakFlattening * rounded * rounded * (1.0 - rounded);
}

} // namespace ChassisGeometry

