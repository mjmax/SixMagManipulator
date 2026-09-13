#include "MotorController.h"

#include "DynamixelProtocol.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSet>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace {
constexpr int motorCount = 6;
constexpr int simulatorPort = 45454;
constexpr int transactionTimeoutMs = 30;
constexpr quint8 presentPositionAddress = 36;
constexpr quint8 movingSpeedAddress = 32;
constexpr double speedRpmPerStep = 0.111;
constexpr double maximumSpeedRpm = 97.0;

int rpmToSpeedRegister(double rpm)
{
    // Joint-mode value zero removes the limit; never use it for a positive RPM.
    return std::clamp(static_cast<int>(std::lround(rpm / speedRpmPerStep)),
                      1, 1023);
}

double rawPositionToDegrees(quint16 rawPosition)
{
    return static_cast<double>(rawPosition) * 300.0 / 1023.0;
}
}

struct MotorStateStore
{
    mutable QMutex mutex;
    std::array<double, motorCount> rawAngles{};
    std::array<double, motorCount> biases{};
    std::array<double, motorCount> angles{};
    double speedLimitRpm = 35.0;
    int speedRegister = rpmToSpeedRegister(35.0);
    std::atomic_int guiRefreshRateHz{15};
};

class MotorWorker final : public QObject
{
    Q_OBJECT

public:
    explicit MotorWorker(std::shared_ptr<MotorStateStore> stateStore)
        : m_stateStore(std::move(stateStore))
    {
    }

public slots:
    void initialize()
    {
        m_pollTimer = new QTimer(this);
        m_pollTimer->setTimerType(Qt::PreciseTimer);
        // The interval is selected when an endpoint connects: real hardware
        // polls again immediately, while the simulator retains a short yield.
        m_pollTimer->setInterval(1);
        connect(m_pollTimer, &QTimer::timeout,
                this, &MotorWorker::pollPositions);
    }

    void connectEndpoint(const QString &endpoint, int baudRate,
                         const QVector<int> &motorIds)
    {
        closeDevice(false);
        if (motorIds.size() != motorCount) {
            failConnection(QStringLiteral("Exactly six motor IDs are required"));
            return;
        }

        QSet<int> uniqueIds;
        for (int index = 0; index < motorCount; ++index) {
            const int id = motorIds[index];
            if (id < 0 || id > 253) {
                failConnection(
                    QStringLiteral("Motor IDs must be between 0 and 253"));
                return;
            }
            if (uniqueIds.contains(id)) {
                failConnection(QStringLiteral("Motor IDs must be unique"));
                return;
            }
            uniqueIds.insert(id);
            m_motorIds[static_cast<std::size_t>(index)] = id;
        }

        emit connectionStateChanged(
            MotorController::Connecting,
            QStringLiteral("Scanning configured motor IDs..."));
        emit motorStatesChanged(QVector<int>(motorCount, 0));

        const bool usingSimulator =
            endpoint.startsWith(QStringLiteral("simulator://"));
        m_pollTimer->setInterval(usingSimulator ? 1 : 0);
        if (usingSimulator) {
            auto *socket = new QTcpSocket(this);
            socket->connectToHost(QHostAddress::LocalHost, simulatorPort);
            if (!socket->waitForConnected(600)) {
                const QString error = socket->errorString();
                delete socket;
                failConnection(QStringLiteral("Simulator unavailable: %1")
                                   .arg(error));
                return;
            }
            m_device = socket;
        } else {
            auto *serial = new QSerialPort(this);
            serial->setPortName(endpoint);
            serial->setBaudRate(baudRate);
            serial->setDataBits(QSerialPort::Data8);
            serial->setParity(QSerialPort::NoParity);
            serial->setStopBits(QSerialPort::OneStop);
            serial->setFlowControl(QSerialPort::NoFlowControl);
            if (!serial->open(QIODevice::ReadWrite)) {
                const QString error = serial->errorString();
                delete serial;
                failConnection(QStringLiteral("Cannot open %1: %2")
                                   .arg(endpoint, error));
                return;
            }
            m_device = serial;
        }

        QVector<int> states(motorCount, 2);
        bool allFound = true;
        for (int index = 0; index < motorCount; ++index) {
            const int id = m_motorIds[static_cast<std::size_t>(index)];
            DynamixelProtocol::Packet response;
            const bool found = transact(
                static_cast<quint8>(id),
                DynamixelProtocol::pingInstruction, {}, response);
            states[index] = found ? 1 : 2;
            allFound = allFound && found;
        }
        emit motorStatesChanged(states);

        if (!allFound) {
            closeDevice(false);
            emit connectionStateChanged(
                MotorController::CommunicationError,
                QStringLiteral("Not all configured motors responded"));
            return;
        }

        int speed = 0;
        {
            QMutexLocker locker(&m_stateStore->mutex);
            speed = m_stateStore->speedRegister;
        }
        if (!writeMovingSpeed(speed)) {
            closeDevice(false);
            emit connectionStateChanged(
                MotorController::CommunicationError,
                QStringLiteral("Could not set speed on all six motors"));
            return;
        }

        m_motorStates.fill(1);
        m_guiClock.restart();
        m_connected = true;
        m_lastReportedState = MotorController::Connected;
        emit connectionStateChanged(MotorController::Connected,
                                    QStringLiteral("All six motors connected"));
        m_pollTimer->start();
    }

    void disconnectEndpoint()
    {
        closeDevice(true);
    }

    void startPollBenchmark(int cycleCount)
    {
        if (!m_connected || !m_device || cycleCount <= 0) {
            emit pollBenchmarkFailed(
                QStringLiteral("Actuators are not connected"));
            return;
        }
        m_benchmarkTargetCycles = cycleCount;
        m_benchmarkCompletedCycles = 0;
        m_benchmarkTotalMilliseconds = 0.0;
    }

    void applySpeedLimit(int rawSpeed)
    {
        if (!m_connected || !m_device)
            return;
        m_pollTimer->stop();
        const bool success = writeMovingSpeed(rawSpeed);
        m_speedWriteFailed = !success;
        const int state = success ? MotorController::Connected
                                  : MotorController::CommunicationError;
        m_lastReportedState = state;
        emit connectionStateChanged(
            state, success ? QStringLiteral("All six motors connected")
                           : QStringLiteral("Could not set motor speed"));
        m_pollTimer->start();
    }

private slots:
    void pollPositions()
    {
        if (!m_connected || !m_device)
            return;

        QElapsedTimer cycleClock;
        cycleClock.start();
        std::array<double, motorCount> rawAngles;
        {
            QMutexLocker locker(&m_stateStore->mutex);
            rawAngles = m_stateStore->rawAngles;
        }
        std::array<double, motorCount> angles{};
        bool allHealthy = true;
        bool statesChanged = false;
        for (int index = 0; index < motorCount; ++index) {
            const int id = m_motorIds[static_cast<std::size_t>(index)];
            QByteArray parameters;
            parameters.append(char(presentPositionAddress));
            parameters.append(char(2));
            DynamixelProtocol::Packet response;
            const bool received = transact(
                static_cast<quint8>(id),
                DynamixelProtocol::readInstruction,
                parameters, response)
                && response.code == 0
                && response.parameters.size() >= 2;

            int newState = 2;
            if (received) {
                const quint16 raw = static_cast<quint8>(response.parameters.at(0))
                    | (static_cast<quint16>(
                           static_cast<quint8>(response.parameters.at(1))) << 8);
                rawAngles[static_cast<std::size_t>(index)] =
                    rawPositionToDegrees(raw);
                newState = 1;
            } else {
                allHealthy = false;
            }
            if (m_motorStates[static_cast<std::size_t>(index)] != newState) {
                m_motorStates[static_cast<std::size_t>(index)] = newState;
                statesChanged = true;
            }
        }

        const double cycleMilliseconds =
            cycleClock.nsecsElapsed() / 1000000.0;
        updatePollBenchmark(cycleMilliseconds, allHealthy);

        {
            QMutexLocker locker(&m_stateStore->mutex);
            m_stateStore->rawAngles = rawAngles;
            for (int index = 0; index < motorCount; ++index) {
                const std::size_t position = static_cast<std::size_t>(index);
                angles[position] = rawAngles[position]
                    - m_stateStore->biases[position];
            }
            m_stateStore->angles = angles;
        }

        if (statesChanged) {
            QVector<int> states;
            states.reserve(motorCount);
            for (const int state : m_motorStates)
                states.append(state);
            emit motorStatesChanged(states);
        }

        const int newConnectionState = allHealthy && !m_speedWriteFailed
            ? MotorController::Connected
            : MotorController::CommunicationError;
        if (newConnectionState != m_lastReportedState) {
            m_lastReportedState = newConnectionState;
            emit connectionStateChanged(
                newConnectionState,
                allHealthy && !m_speedWriteFailed
                    ? QStringLiteral("All six motors connected")
                           : QStringLiteral("Motor communication error"));
        }

        const int guiInterval = std::max(
            1, 1000 / m_stateStore->guiRefreshRateHz.load(std::memory_order_relaxed));
        if (!m_guiClock.isValid() || m_guiClock.elapsed() >= guiInterval) {
            QVector<double> guiAngles;
            guiAngles.reserve(motorCount);
            for (const double angle : angles)
                guiAngles.append(angle);
            emit guiAnglesReady(guiAngles);
            m_guiClock.restart();
        }
    }

signals:
    void connectionStateChanged(int state, const QString &message);
    void motorStatesChanged(const QVector<int> &states);
    void guiAnglesReady(const QVector<double> &angles);
    void pollBenchmarkProgress(double averageMilliseconds,
                               int completedCycles, int totalCycles);
    void pollBenchmarkFinished(double averageMilliseconds);
    void pollBenchmarkFailed(const QString &message);

private:
    bool writeMovingSpeed(int rawSpeed)
    {
        QByteArray parameters;
        parameters.append(char(movingSpeedAddress));
        parameters.append(char(rawSpeed & 0xff));
        parameters.append(char((rawSpeed >> 8) & 0xff));
        for (const int id : m_motorIds) {
            DynamixelProtocol::Packet response;
            if (!transact(static_cast<quint8>(id),
                          DynamixelProtocol::writeInstruction,
                          parameters, response) || response.code != 0)
                return false;
        }
        return true;
    }

    void updatePollBenchmark(double cycleMilliseconds, bool successful)
    {
        if (m_benchmarkTargetCycles <= 0)
            return;
        if (!successful) {
            m_benchmarkTargetCycles = 0;
            m_benchmarkCompletedCycles = 0;
            m_benchmarkTotalMilliseconds = 0.0;
            emit pollBenchmarkFailed(
                QStringLiteral("A motor position read failed"));
            return;
        }

        m_benchmarkTotalMilliseconds += cycleMilliseconds;
        ++m_benchmarkCompletedCycles;
        const double average =
            m_benchmarkTotalMilliseconds / m_benchmarkCompletedCycles;
        if (m_benchmarkCompletedCycles == 1
            || m_benchmarkCompletedCycles % 5 == 0
            || m_benchmarkCompletedCycles == m_benchmarkTargetCycles) {
            emit pollBenchmarkProgress(
                average, m_benchmarkCompletedCycles, m_benchmarkTargetCycles);
        }
        if (m_benchmarkCompletedCycles >= m_benchmarkTargetCycles) {
            m_benchmarkTargetCycles = 0;
            m_benchmarkCompletedCycles = 0;
            m_benchmarkTotalMilliseconds = 0.0;
            emit pollBenchmarkFinished(average);
        }
    }

    bool transact(quint8 id, quint8 instruction,
                  const QByteArray &parameters,
                  DynamixelProtocol::Packet &response)
    {
        if (!m_device || !m_device->isOpen())
            return false;

        m_receiveBuffer.clear();
        m_device->readAll();
        const QByteArray request =
            DynamixelProtocol::makePacket(id, instruction, parameters);
        if (m_device->write(request) != request.size())
            return false;
        if (m_device->bytesToWrite() > 0
            && !m_device->waitForBytesWritten(transactionTimeoutMs)) {
            return false;
        }

        QElapsedTimer timeout;
        timeout.start();
        while (timeout.elapsed() < transactionTimeoutMs) {
            m_receiveBuffer.append(m_device->readAll());
            DynamixelProtocol::Packet packet;
            while (DynamixelProtocol::takePacket(m_receiveBuffer, packet)) {
                if (packet.id == id) {
                    response = packet;
                    return true;
                }
            }
            const int remaining = transactionTimeoutMs
                - static_cast<int>(timeout.elapsed());
            if (remaining <= 0 || !m_device->waitForReadyRead(remaining))
                break;
        }
        m_receiveBuffer.append(m_device->readAll());
        DynamixelProtocol::Packet packet;
        while (DynamixelProtocol::takePacket(m_receiveBuffer, packet)) {
            if (packet.id == id) {
                response = packet;
                return true;
            }
        }
        return false;
    }

    void failConnection(const QString &message)
    {
        emit motorStatesChanged(QVector<int>(motorCount, 2));
        emit connectionStateChanged(MotorController::CommunicationError,
                                    message);
    }

    void closeDevice(bool reportDisconnected)
    {
        if (m_benchmarkTargetCycles > 0) {
            m_benchmarkTargetCycles = 0;
            m_benchmarkCompletedCycles = 0;
            m_benchmarkTotalMilliseconds = 0.0;
            emit pollBenchmarkFailed(
                QStringLiteral("Disconnected during poll test"));
        }
        m_connected = false;
        m_speedWriteFailed = false;
        if (m_pollTimer)
            m_pollTimer->stop();
        if (m_device) {
            m_device->close();
            delete m_device;
            m_device = nullptr;
        }
        m_receiveBuffer.clear();
        m_motorStates.fill(0);
        m_lastReportedState = MotorController::Disconnected;
        if (reportDisconnected) {
            emit motorStatesChanged(QVector<int>(motorCount, 0));
            emit connectionStateChanged(MotorController::Disconnected,
                                        QStringLiteral("Disconnected"));
        }
    }

    std::shared_ptr<MotorStateStore> m_stateStore;
    QIODevice *m_device = nullptr;
    QTimer *m_pollTimer = nullptr;
    QByteArray m_receiveBuffer;
    QElapsedTimer m_guiClock;
    std::array<int, motorCount> m_motorStates{};
    std::array<int, motorCount> m_motorIds{1, 2, 3, 4, 5, 6};
    int m_lastReportedState = MotorController::Disconnected;
    int m_benchmarkTargetCycles = 0;
    int m_benchmarkCompletedCycles = 0;
    double m_benchmarkTotalMilliseconds = 0.0;
    bool m_connected = false;
    bool m_speedWriteFailed = false;
};

MotorController::MotorController(QObject *parent)
    : QObject(parent),
      m_workerThread(new QThread(this)),
      m_stateStore(std::make_shared<MotorStateStore>())
{
    m_stateStore->rawAngles.fill(150.0);
    m_stateStore->biases.fill(150.0);
    QSettings settings;
    settings.beginGroup(QStringLiteral("motorBiases"));
    for (int index = 0; index < motorCount; ++index) {
        const double saved = settings.value(
            QStringLiteral("M%1").arg(index + 1), 150.0).toDouble();
        if (std::isfinite(saved) && saved >= 0.0 && saved <= 300.0)
            m_stateStore->biases[static_cast<std::size_t>(index)] = saved;
    }
    settings.endGroup();

    const double savedSpeed = settings.value(
        QStringLiteral("motorSpeedLimitRpm"), 35.0).toDouble();
    if (std::isfinite(savedSpeed) && savedSpeed >= 0.11
        && savedSpeed <= maximumSpeedRpm) {
        m_stateStore->speedLimitRpm = savedSpeed;
        m_stateStore->speedRegister = rpmToSpeedRegister(savedSpeed);
    }

    m_worker = new MotorWorker(m_stateStore);
    m_worker->moveToThread(m_workerThread);
    connect(m_workerThread, &QThread::started,
            m_worker, &MotorWorker::initialize);
    connect(m_workerThread, &QThread::finished,
            m_worker, &QObject::deleteLater);
    connect(this, &MotorController::connectRequested,
            m_worker, &MotorWorker::connectEndpoint,
            Qt::QueuedConnection);
    connect(this, &MotorController::disconnectRequested,
            m_worker, &MotorWorker::disconnectEndpoint,
            Qt::QueuedConnection);
    connect(this, &MotorController::pollBenchmarkRequested,
            m_worker, &MotorWorker::startPollBenchmark,
            Qt::QueuedConnection);
    connect(this, &MotorController::speedLimitRequested,
            m_worker, &MotorWorker::applySpeedLimit,
            Qt::QueuedConnection);
    connect(m_worker, &MotorWorker::connectionStateChanged,
            this, &MotorController::connectionStateChanged);
    connect(m_worker, &MotorWorker::motorStatesChanged,
            this, &MotorController::motorStatesChanged);
    connect(m_worker, &MotorWorker::guiAnglesReady,
            this, &MotorController::guiAnglesReady);
    connect(m_worker, &MotorWorker::pollBenchmarkProgress,
            this, &MotorController::pollBenchmarkProgress);
    connect(m_worker, &MotorWorker::pollBenchmarkFinished,
            this, &MotorController::pollBenchmarkFinished);
    connect(m_worker, &MotorWorker::pollBenchmarkFailed,
            this, &MotorController::pollBenchmarkFailed);
    m_workerThread->start(QThread::HighPriority);
}

MotorController::~MotorController()
{
    shutdown();
}

void MotorController::shutdown()
{
    if (!m_workerThread || !m_workerThread->isRunning())
        return;

    // Blocking invocation guarantees QSerialPort::close() completes in the
    // worker's own thread before the GUI window is allowed to disappear.
    QMetaObject::invokeMethod(m_worker, "disconnectEndpoint",
                              Qt::BlockingQueuedConnection);
    m_workerThread->quit();
    m_workerThread->wait();
}

QList<QPair<QString, QString>> MotorController::availableEndpoints()
{
    QList<QPair<QString, QString>> endpoints;
    for (const QSerialPortInfo &port : QSerialPortInfo::availablePorts()) {
        QString label = port.portName();
        if (!port.description().isEmpty())
            label += QStringLiteral(" — ") + port.description();
        endpoints.append({label, port.portName()});
    }
    endpoints.append({QStringLiteral("Simulator (localhost)"),
                      QStringLiteral("simulator://127.0.0.1:45454")});
    return endpoints;
}

std::array<double, 6> MotorController::latestAngles() const
{
    QMutexLocker locker(&m_stateStore->mutex);
    return m_stateStore->angles;
}

std::array<double, 6> MotorController::biases() const
{
    QMutexLocker locker(&m_stateStore->mutex);
    return m_stateStore->biases;
}

double MotorController::speedLimitRpm() const
{
    QMutexLocker locker(&m_stateStore->mutex);
    return m_stateStore->speedLimitRpm;
}

void MotorController::setSpeedLimitRpm(double rpm)
{
    if (!std::isfinite(rpm) || rpm < 0.11
        || rpm > maximumSpeedRpm)
        return;
    const int rawSpeed = rpmToSpeedRegister(rpm);
    {
        QMutexLocker locker(&m_stateStore->mutex);
        m_stateStore->speedLimitRpm = rpm;
        m_stateStore->speedRegister = rawSpeed;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("motorSpeedLimitRpm"), rpm);
    emit speedLimitRequested(rawSpeed);
}

void MotorController::setBias(int motorIndex, double degrees)
{
    if (motorIndex < 0 || motorIndex >= motorCount
        || !std::isfinite(degrees) || degrees < 0.0 || degrees > 300.0)
        return;
    {
        QMutexLocker locker(&m_stateStore->mutex);
        const std::size_t position = static_cast<std::size_t>(motorIndex);
        m_stateStore->biases[position] = degrees;
        m_stateStore->angles[position] =
            m_stateStore->rawAngles[position] - degrees;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("motorBiases/M%1").arg(motorIndex + 1),
                      degrees);
}

void MotorController::setGuiRefreshRate(int framesPerSecond)
{
    m_stateStore->guiRefreshRateHz.store(
        std::clamp(framesPerSecond, 1, 60), std::memory_order_relaxed);
}

void MotorController::connectEndpoint(const QString &endpoint, int baudRate,
                                      const QVector<int> &motorIds)
{
    emit connectRequested(endpoint, baudRate, motorIds);
}

void MotorController::disconnectEndpoint()
{
    emit disconnectRequested();
}

void MotorController::startPollBenchmark(int cycleCount)
{
    emit pollBenchmarkRequested(cycleCount);
}

#include "MotorController.moc"
