#include "FastControlRuntime.h"

#include <chrono>
#include <cmath>

namespace sixmag::control {
namespace {
constexpr double pi = 3.14159265358979323846;
double steadySeconds() noexcept
{
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

bool FastControlRuntime::start(double gain, const MagnetAngles& biases,
                               std::uint64_t sessionId)
{
    if (!std::isfinite(gain) || gain <= 0 || sessionId == 0)
        return false;
    ControlLoopOptions options;
    options.gain = gain;
    for (std::size_t i = 0; i < MagnetCount; ++i) {
        if (!std::isfinite(biases[i]) || biases[i] < 0 || biases[i] > 300)
            return false;
        options.lowerAngles[i] = -biases[i] * pi / 180.0;
        options.upperAngles[i] = (300.0 - biases[i]) * pi / 180.0;
    }
    try {
        auto session = std::make_shared<Session>(model_, options, sessionId);
        if (!session->loop.selectMode(ControlMode::LinearizedOrigin))
            return false;
        std::atomic_store_explicit(&activeSession_, std::move(session),
                                   std::memory_order_release);
        return true;
    } catch (...) {
        return false;
    }
}

void FastControlRuntime::stop() noexcept
{
    std::atomic_store_explicit(&activeSession_, std::shared_ptr<Session>{},
                               std::memory_order_release);
}

FastControlCommand FastControlRuntime::evaluate(
    double xMetres, double yMetres,
    std::uint64_t frameId, double completedAtSteadySeconds) noexcept
{
    FastControlCommand command;
    auto session = std::atomic_load_explicit(&activeSession_,
                                              std::memory_order_acquire);
    if (!session)
        return command;
    const double now = steadySeconds();
    ControlFeedback feedback;
    feedback.position = {xMetres, yMetres};
    feedback.positionValid = true;
    feedback.positionSequence = frameId;
    feedback.positionTimeSeconds = completedAtSteadySeconds;
    const auto evaluationStart = std::chrono::steady_clock::now();
    const ControlStep step = session->loop.step(feedback, now);
    command.evaluationMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - evaluationStart).count();
    if (step.commandReady) {
        command.ready = true;
        command.sessionId = session->sessionId;
        command.unbiasedRadians = step.commandedAngles;
    }
    return command;
}
} // namespace sixmag::control
