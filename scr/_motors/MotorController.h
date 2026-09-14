#pragma once

#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

#include <array>
#include <cstdint>
#include <memory>

class QThread;
class MotorWorker;
struct MotorStateStore;

struct ControlTiming {
    double loopMilliseconds = -1.0;
    double endToEndMilliseconds = -1.0;
    double evaluationMilliseconds = -1.0;
};

class MotorController final : public QObject
{
    Q_OBJECT

public:
    enum ConnectionState {
        Disconnected = 0,
        Connecting = 1,
        Connected = 2,
        CommunicationError = 3
    };
    Q_ENUM(ConnectionState)

    explicit MotorController(QObject *parent = nullptr);
    ~MotorController() override;

    static QList<QPair<QString, QString>> availableEndpoints();
    std::array<double, 6> biases() const;
    double speedLimitRpm() const;
    // Full-speed feedback: magnet angle = servo angle - bias, in degrees;
    // index 0 corresponds to M1. Goal writes add each motor's bias exactly
    // once before converting to the AX-18A 0..1023 position value.
    std::array<double, 6> latestAngles() const;
    ControlTiming controlTiming() const;
    std::uint64_t beginGoalControl();
    void stopGoalControl();
    void submitGoalAngles(std::uint64_t sessionId,
                          const std::array<double, 6> &unbiasedRadians,
                          double frameSubmittedAtSeconds = 0.0,
                          double evaluationMilliseconds = 0.0);
    bool moveToBias();
    void shutdown();

public slots:
    void connectEndpoint(const QString &endpoint, int baudRate,
                         const QVector<int> &motorIds);
    void disconnectEndpoint();
    void startPollBenchmark(int cycleCount = 100);
    void setBias(int motorIndex, double degrees);
    void setSpeedLimitRpm(double rpm);
    void setGuiRefreshRate(int framesPerSecond);

signals:
    void connectionStateChanged(int state, const QString &message);
    void motorStatesChanged(const QVector<int> &states);
    void guiAnglesReady(const QVector<double> &angles);
    void pollBenchmarkProgress(double averageMilliseconds,
                               int completedCycles, int totalCycles);
    void pollBenchmarkFinished(double averageMilliseconds);
    void pollBenchmarkFailed(const QString &message);

    void connectRequested(const QString &endpoint, int baudRate,
                          const QVector<int> &motorIds);
    void disconnectRequested();
    void pollBenchmarkRequested(int cycleCount);
    void speedLimitRequested(int rawSpeed);

private:
    QThread *m_workerThread = nullptr;
    MotorWorker *m_worker = nullptr;
    std::shared_ptr<MotorStateStore> m_stateStore;
};
