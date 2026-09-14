#pragma once

#include "ControlLoop.h"

#include <cstdint>
#include <memory>

namespace sixmag::control {

struct FastControlCommand {
    bool ready = false;
    std::uint64_t sessionId = 0;
    MagnetAngles unbiasedRadians{};
    double evaluationMilliseconds = 0.0;
};

// Called directly by the image-processing thread on every completed frame.
// A new session is prepared off that thread, then published atomically.
// Hardware I/O and GUI refreshes stay outside this computation path.
class FastControlRuntime final {
public:
    FastControlRuntime() = default;
    bool start(double proportionalGain, const MagnetAngles& biasDegrees,
               std::uint64_t sessionId);
    void stop() noexcept;
    [[nodiscard]] FastControlCommand evaluate(
        double modelXMetres, double modelYMetres,
        std::uint64_t frameId, double completedAtSteadySeconds) noexcept;

private:
    struct Session {
        Session(const MagneticModel& model, const ControlLoopOptions& options,
                std::uint64_t id)
            : loop(model, options), sessionId(id) {}
        ControlLoop loop;
        std::uint64_t sessionId;
    };
    MagneticModel model_;
    std::shared_ptr<Session> activeSession_;
};

} // namespace sixmag::control
