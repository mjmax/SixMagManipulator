#pragma once
#include "MagneticModel.h"

namespace sixmag::control {
enum class ControlLawStatus { Ok, InvalidPosition, NonFiniteCommand };

// Implements the ACTIVE law in __refmod/control.m:
// theta_goal = -k * pinv(Gth(r=0, theta=0)) * r.
// These are absolute, unbiased model angles, not increments or velocities.
class LinearizedPositionControl final {
public:
    explicit LinearizedPositionControl(const MagneticModel& model, double gain = 4000.0);
    [[nodiscard]] ControlLawStatus evaluate(
        const Vector2& position, MagnetAngles& requestedAngles) const noexcept;
    const std::array<Vector2, MagnetCount>& matrix() const noexcept { return matrix_; }
private:
    std::array<Vector2, MagnetCount> matrix_{}; // rad/m, includes -gain
};
} // namespace sixmag::control
