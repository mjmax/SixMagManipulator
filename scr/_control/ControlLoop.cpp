#include "ControlLoop.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sixmag::control {
ControlLoop::ControlLoop(const MagneticModel& model, const ControlLoopOptions& options)
    : linear_(model, options.gain), options_(options)
{
    if (!std::isfinite(options_.maximumPositionAge) || options_.maximumPositionAge <= 0)
        throw std::invalid_argument("Feedback age limit must be positive and finite.");
    for (std::size_t j = 0; j < MagnetCount; ++j)
        if (!std::isfinite(options_.lowerAngles[j]) || !std::isfinite(options_.upperAngles[j]) ||
            options_.lowerAngles[j] > options_.upperAngles[j])
            throw std::invalid_argument("Invalid unbiased motor angle limits.");
}
void ControlLoop::reset() noexcept
{
    haveSequence_ = false;
    lastSequence_ = 0;
    lastMeasurementTime_ = 0;
}
bool ControlLoop::selectMode(ControlMode mode) noexcept
{
    if (mode != ControlMode::Disabled && mode != ControlMode::LinearizedOrigin) {
        mode_ = ControlMode::Disabled;
        reset();
        return false;
    }
    if (mode_ != mode) { mode_ = mode; reset(); }
    return true;
}
ControlStep ControlLoop::step(const ControlFeedback& f, double now) noexcept
{
    ControlStep out;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    out.requestedAngles.fill(nan);
    out.commandedAngles.fill(nan);
    if (mode_ == ControlMode::Disabled) return out;
    if (!f.positionValid || !std::isfinite(f.position[0]) || !std::isfinite(f.position[1])) {
        out.status = ControlStepStatus::InvalidMeasurement;
        return out;
    }
    if (!std::isfinite(now) || !std::isfinite(f.positionTimeSeconds) ||
        now < 0 || f.positionTimeSeconds < 0 || f.positionTimeSeconds > now) {
        out.status = ControlStepStatus::InvalidTime;
        return out;
    }
    if (now-f.positionTimeSeconds > options_.maximumPositionAge) {
        out.status = ControlStepStatus::StaleMeasurement;
        return out;
    }
    if (haveSequence_ && (f.positionSequence < lastSequence_ ||
                         f.positionTimeSeconds < lastMeasurementTime_)) {
        out.status = ControlStepStatus::OutOfOrderMeasurement;
        return out;
    }
    if (haveSequence_ && f.positionSequence == lastSequence_) {
        out.status = ControlStepStatus::NoNewMeasurement;
        return out;
    }
    // Future feedback-angle laws branch here; do not add model evaluation or
    // matrix inversion to the fixed-origin law's hot path.
    if (linear_.evaluate(f.position, out.requestedAngles) != ControlLawStatus::Ok) {
        out.status = ControlStepStatus::CalculationFailed;
        return out;
    }
    for (std::size_t j = 0; j < MagnetCount; ++j) {
        out.commandedAngles[j] = std::clamp(out.requestedAngles[j],
                                          options_.lowerAngles[j], options_.upperAngles[j]);
        out.limited[j] = out.commandedAngles[j] != out.requestedAngles[j];
    }
    haveSequence_ = true;
    lastSequence_ = f.positionSequence;
    lastMeasurementTime_ = f.positionTimeSeconds;
    out.status = ControlStepStatus::Ready;
    out.commandReady = true;
    return out;
}
} // namespace sixmag::control
