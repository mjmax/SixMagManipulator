#include "FastControlRuntime.h"
#include "ImageTracker.h"
#include "MotorController.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPainter>
#include <QProcess>
#include <QThread>

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool valid, const char *message)
{
    if (!valid) throw std::runtime_error(message);
}

template <typename Predicate>
static bool waitUntil(Predicate predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (predicate()) return true;
        QThread::msleep(3);
    }
    return predicate();
}

static double steadySeconds()
{
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SixMagTest");
    QCoreApplication::setApplicationName("SimulatorControlIntegration");
    QProcess simulator;
    simulator.setProgram(QCoreApplication::applicationDirPath()
                         + "/SixMagMotorEmulator.exe");
    simulator.start();
    try {
        require(simulator.waitForStarted(3000), "Simulator did not start");
        MotorController motors;
        int connectionState = MotorController::Disconnected;
        QObject::connect(&motors, &MotorController::connectionStateChanged,
                         &app, [&connectionState](int state, const QString &) {
            connectionState = state;
        });
        motors.connectEndpoint("simulator://127.0.0.1:45454", 1000000,
                               {1, 2, 3, 4, 5, 6});
        require(waitUntil([&] { return connectionState == MotorController::Connected; },
                          5000), "Six simulator motors did not connect");

        const auto biases = motors.biases();
        const std::uint64_t session = motors.beginGoalControl();
        require(session != 0, "Simulator control session did not start");
        sixmag::control::FastControlRuntime runtime;
        require(runtime.start(3000.0, biases, session), "Native control did not start");
        const auto command = runtime.evaluate(0.0005, -0.0004, 1, steadySeconds());
        require(command.ready && command.sessionId == session,
                "Fresh SI position did not produce a six-motor command");
        motors.submitGoalAngles(session, command.unbiasedRadians);
        require(waitUntil([&] {
            const auto feedback = motors.latestAngles();
            for (int i = 0; i < 6; ++i) {
                const double expected = command.unbiasedRadians[i]
                    * 180.0 / 3.14159265358979323846;
                if (std::abs(feedback[i] - expected) > 2.0) return false;
            }
            return true;
        }, 5000), "Simulated motors did not follow the computed goal positions");

        runtime.stop();
        motors.stopGoalControl();
        std::array<double, 6> staleAngles{};
        staleAngles.fill(1.0);
        motors.submitGoalAngles(session, staleAngles);
        QThread::msleep(100);
        QCoreApplication::processEvents();
        {
            const auto feedback = motors.latestAngles();
            for (int i = 0; i < 6; ++i) {
                const double expected = command.unbiasedRadians[i]
                    * 180.0 / 3.14159265358979323846;
                require(std::abs(feedback[i] - expected) <= 2.0,
                        "Stopped control accepted an old command");
            }
        }

        require(motors.moveToBias(), "Reset-to-bias packet was not transmitted");
        require(waitUntil([&] {
            const auto feedback = motors.latestAngles();
            for (const double angle : feedback)
                if (std::abs(angle) > 2.0) return false;
            return true;
        }, 5000), "Reset did not bring all simulated motors back to bias");

        const std::uint64_t secondSession = motors.beginGoalControl();
        require(runtime.start(3000.0, biases, secondSession),
                "Second control session did not start");
        QThread::msleep(100);
        {
            const auto feedback = motors.latestAngles();
            for (const double angle : feedback)
                require(std::abs(angle) <= 2.0,
                        "Restart replayed a goal from the previous control session");
        }
        ImageTracker tracker;
        const MillimeterTransform modelTransform{
            QPointF(-30.0, 30.0), QPointF(60.0, 0.0), QPointF(0.0, -60.0)};
        tracker.setMillimeterTransform(modelTransform, modelTransform);
        std::atomic_bool frameCommandReady{false};
        sixmag::control::FastControlCommand frameCommand;
        QObject::connect(&tracker, &ImageTracker::fastResultReady,
                         &app, [&](const TrackingResult &result) {
            if (result.objects.size() > 1) return;
            const QPointF millimeters = result.objects.isEmpty()
                ? QPointF(0.0, 0.0)
                : result.objects.first().modelPositionMillimeters;
            frameCommand = runtime.evaluate(
                millimeters.x() * 0.001, millimeters.y() * 0.001,
                result.frameId, result.completedAtSteadySeconds);
            if (frameCommand.ready) {
                motors.submitGoalAngles(frameCommand.sessionId,
                                        frameCommand.unbiasedRadians,
                                        result.submittedAtSteadySeconds,
                                        frameCommand.evaluationMilliseconds);
                frameCommandReady.store(true, std::memory_order_release);
            }
        }, Qt::DirectConnection);
        QImage image(512, 512, QImage::Format_Grayscale8);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::black);
            painter.drawEllipse(QPointF(265, 253), 8, 8);
        }
        tracker.start();
        tracker.submitFrame(image, 1);
        require(waitUntil([&] {
            return frameCommandReady.load(std::memory_order_acquire);
        }, 3000), "Full-rate image result did not produce a control command");
        require(waitUntil([&] {
            const auto feedback = motors.latestAngles();
            for (int i = 0; i < 6; ++i) {
                const double expected = frameCommand.unbiasedRadians[i]
                    * 180.0 / 3.14159265358979323846;
                if (std::abs(feedback[i] - expected) > 2.0) return false;
            }
            return true;
        }, 5000), "Image-driven command did not reach all six simulated motors");
        frameCommandReady.store(false, std::memory_order_release);
        image.fill(Qt::white);
        tracker.submitFrame(image, 2);
        require(waitUntil([&] {
            return frameCommandReady.load(std::memory_order_acquire);
        }, 3000), "Missing object did not produce an origin-feedback command");
        for (const double angle : frameCommand.unbiasedRadians)
            require(std::abs(angle) < 1e-12,
                    "Missing object did not command zero unbiased angles");
        require(waitUntil([&] {
            for (const double angle : motors.latestAngles())
                if (std::abs(angle) > 2.0) return false;
            return true;
        }, 5000), "Missing-object feedback did not return simulator motors to bias");
        const auto timing = motors.controlTiming();
        require(timing.loopMilliseconds > 0.0,
                "Completed command sampling period was not recorded");
        require(timing.endToEndMilliseconds > 0.0
                    && timing.evaluationMilliseconds >= 0.0
                    && timing.endToEndMilliseconds >= timing.evaluationMilliseconds,
                "End-to-end and control evaluation timing are inconsistent");
        tracker.stop();
        runtime.stop();
        motors.stopGoalControl();
        motors.shutdown();
        simulator.terminate();
        simulator.waitForFinished(3000);
        std::cout << "Simulator control integration passed: image feedback, six goals, stop, bias reset.\n";
        return 0;
    } catch (const std::exception &error) {
        simulator.terminate();
        if (!simulator.waitForFinished(3000)) simulator.kill();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
