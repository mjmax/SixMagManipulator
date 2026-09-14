#pragma once
#include "LinearizedPositionControl.h"
#include <cstdint>

namespace sixmag::control {
enum class ControlMode { Disabled = 0, LinearizedOrigin = 1 };
enum class ControlStepStatus {
    Disabled, Ready, InvalidMeasurement, InvalidTime, StaleMeasurement,
    NoNewMeasurement, OutOfOrderMeasurement, CalculationFailed
};
struct ControlFeedback {
    Vector2 position{};              // model-frame metres, NOT GUI-flipped axes
    MagnetAngles measuredAngles{};   // unbiased radians; reserved for future laws
    bool positionValid = false;
    bool anglesValid = false;        // current law intentionally does not need angles
    std::uint64_t positionSequence = 0;
    double positionTimeSeconds = 0;  // monotonic clock, same epoch as step(now)
};
struct ControlLoopOptions {
    double gain = 4000.0;            // 1/s^2, from control.m
    double maximumPositionAge = 0.1; // seconds; configure for actual acquisition
    // Default +/-150 degrees expressed in SI radians. Update from each motor's
    // raw limits minus its configured bias BEFORE using this with actuators.
    MagnetAngles lowerAngles{-2.6179938779914944,-2.6179938779914944,-2.6179938779914944,
                             -2.6179938779914944,-2.6179938779914944,-2.6179938779914944};
    MagnetAngles upperAngles{2.6179938779914944,2.6179938779914944,2.6179938779914944,
                             2.6179938779914944,2.6179938779914944,2.6179938779914944};
};
struct ControlStep {
    ControlStepStatus status = ControlStepStatus::Disabled;
    MagnetAngles requestedAngles{};  // exact control.m law before limits
    MagnetAngles commandedAngles{};  // clipped unbiased radians, NOT raw servo units
    std::array<bool, MagnetCount> limited{};
    bool commandReady = false;       // no hardware write is performed by this class
};

// Caller-driven computational loop. Invoke from a fast feedback worker, not
// the GUI refresh timer. No background thread, Qt dependency or device I/O.
// Stateful: one owner thread must serialize step(), reset() and selectMode().
class ControlLoop final {
public:
    explicit ControlLoop(const MagneticModel& model, const ControlLoopOptions& options = {});
    [[nodiscard]] bool selectMode(ControlMode mode) noexcept;
    ControlMode mode() const noexcept { return mode_; }
    void reset() noexcept; // reset sequence history on acquisition/source restart
    [[nodiscard]] ControlStep step(const ControlFeedback& feedback, double nowSeconds) noexcept;
private:
    LinearizedPositionControl linear_;
    ControlLoopOptions options_;
    ControlMode mode_ = ControlMode::Disabled;
    bool haveSequence_ = false;
    std::uint64_t lastSequence_ = 0;
    double lastMeasurementTime_ = 0;
};
} // namespace sixmag::control
