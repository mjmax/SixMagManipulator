#include "MotorController.h"

#include "DynamixelProtocol.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSet>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {
constexpr int motorCount = 6;
constexpr int simulatorPort = 45454;
constexpr int transactionTimeoutMs = 30;
constexpr quint8 presentPositionAddress = 36;

double rawPositionToDegrees(quint16 rawPosition)
{
    return (static_cast<double>(rawPosition) - 511.5) * 300.0 / 1023.0;
}
}

struct MotorStateStore
{
    mutable QMutex mutex;
    std::array<double, motorCount> angles{};
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
        // Yield briefly between complete six-servo cycles. Real serial I/O
        // normally dominates this interval; the yield prevents a localhost
        // simulator from busy-spinning while retaining high control bandwidth.
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

        if (endpoint.startsWith(QStringLiteral("simulator://"))) {
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

private slots:
    void pollPositions()
    {
        if (!m_connected || !m_device)
            return;

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
                angles[static_cast<std::size_t>(index)] =
                    rawPositionToDegrees(raw);
                newState = 1;
            } else {
                QMutexLocker locker(&m_stateStore->mutex);
                angles[static_cast<std::size_t>(index)] =
                    m_stateStore->angles[static_cast<std::size_t>(index)];
                allHealthy = false;
            }
            if (m_motorStates[static_cast<std::size_t>(index)] != newState) {
                m_motorStates[static_cast<std::size_t>(index)] = newState;
                statesChanged = true;
            }
        }

        {
            QMutexLocker locker(&m_stateStore->mutex);
            m_stateStore->angles = angles;
        }

        if (statesChanged) {
            QVector<int> states;
            states.reserve(motorCount);
            for (const int state : m_motorStates)
                states.append(state);
            emit motorStatesChanged(states);
        }

        const int newConnectionState = allHealthy
            ? MotorController::Connected
            : MotorController::CommunicationError;
        if (newConnectionState != m_lastReportedState) {
            m_lastReportedState = newConnectionState;
            emit connectionStateChanged(
                newConnectionState,
                allHealthy ? QStringLiteral("All six motors connected")
                           : QStringLiteral("Motor communication error"));
        }

        if (!m_guiClock.isValid() || m_guiClock.elapsed() >= 66) {
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

private:
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
        m_connected = false;
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
    bool m_connected = false;
};

MotorController::MotorController(QObject *parent)
    : QObject(parent),
      m_workerThread(new QThread(this)),
      m_stateStore(std::make_shared<MotorStateStore>())
{
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
    connect(m_worker, &MotorWorker::connectionStateChanged,
            this, &MotorController::connectionStateChanged);
    connect(m_worker, &MotorWorker::motorStatesChanged,
            this, &MotorController::motorStatesChanged);
    connect(m_worker, &MotorWorker::guiAnglesReady,
            this, &MotorController::guiAnglesReady);
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

void MotorController::connectEndpoint(const QString &endpoint, int baudRate,
                                      const QVector<int> &motorIds)
{
    emit connectRequested(endpoint, baudRate, motorIds);
}

void MotorController::disconnectEndpoint()
{
    emit disconnectRequested();
}

#include "MotorController.moc"

