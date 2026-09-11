#include "HallSensorController.h"

#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>

#include <cmath>

HallSensorController::HallSensorController(QObject *parent)
    : QObject(parent),
      m_serialPort(new QSerialPort(this)),
      m_connectionTimer(new QTimer(this)),
      m_displayTimer(new QTimer(this))
{
    m_connectionTimer->setSingleShot(true);
    m_connectionTimer->setInterval(5000);
    m_displayTimer->setInterval(33);
    connect(m_serialPort, &QSerialPort::readyRead,
            this, &HallSensorController::readAvailableData);
    connect(m_serialPort, &QSerialPort::errorOccurred,
            this, [this](QSerialPort::SerialPortError error) {
        handleSerialError(static_cast<int>(error));
    });
    connect(m_connectionTimer, &QTimer::timeout, this, [this] {
        if (!m_serialPort->isOpen())
            return;
        reportInvalidStream(
            QStringLiteral("Sensor stream stopped or contains no valid records"));
    });
    connect(m_displayTimer, &QTimer::timeout,
            this, &HallSensorController::publishLatestValues);
}

HallSensorController::~HallSensorController() { shutdown(); }

QList<QPair<QString, QString>> HallSensorController::availablePorts()
{
    QList<QPair<QString, QString>> ports;
    for (const QSerialPortInfo &port : QSerialPortInfo::availablePorts()) {
        QString label = port.portName();
        if (!port.description().isEmpty())
            label += QStringLiteral(" — ") + port.description();
        ports.append({label, port.portName()});
    }
    return ports;
}

void HallSensorController::connectPort(const QString &portName, int baudRate)
{
    if (portName.isEmpty()) {
        emit connectionStateChanged(
            CommunicationError, QStringLiteral("Select an Arduino COM port"));
        return;
    }
    closePort();
    m_shutdownComplete = false;
    m_serialPort->setPortName(portName);
    m_serialPort->setBaudRate(baudRate);
    m_serialPort->setDataBits(QSerialPort::Data8);
    m_serialPort->setParity(QSerialPort::NoParity);
    m_serialPort->setStopBits(QSerialPort::OneStop);
    m_serialPort->setFlowControl(QSerialPort::NoFlowControl);
    if (!m_serialPort->open(QIODevice::ReadOnly)) {
        m_state = CommunicationError;
        emit connectionStateChanged(
            m_state, QStringLiteral("Could not open %1: %2")
                         .arg(portName, m_serialPort->errorString()));
        return;
    }
    m_receiveBuffer.clear();
    m_hasUnpublishedValues = false;
    m_validRecordStreak = 0;
    m_invalidRecordStreak = 0;
    m_state = Connecting;
    emit connectionStateChanged(
        m_state, QStringLiteral("Waiting for valid Arduino sensor data"));
    m_connectionTimer->setInterval(5000);
    m_connectionTimer->start();
    m_displayTimer->start();
}

void HallSensorController::disconnectPort()
{
    closePort();
    m_state = Disconnected;
    emit connectionStateChanged(m_state, QStringLiteral("Disconnected"));
}

void HallSensorController::shutdown()
{
    if (m_shutdownComplete)
        return;
    m_shutdownComplete = true;
    closePort();
}

void HallSensorController::readAvailableData()
{
    m_receiveBuffer += m_serialPort->readAll();
    qsizetype newlineIndex = -1;
    while ((newlineIndex = m_receiveBuffer.indexOf('\n')) >= 0) {
        const QByteArray record = m_receiveBuffer.left(newlineIndex).trimmed();
        m_receiveBuffer.remove(0, newlineIndex + 1);
        std::array<double, 6> values{};
        if (parseRecord(record, values))
            acceptRecord(values);
        else
            rejectRecord();
    }
    if (m_receiveBuffer.size() > 8192) {
        m_receiveBuffer = m_receiveBuffer.right(4096);
        rejectRecord();
    }
}

bool HallSensorController::parseRecord(
    const QByteArray &record, std::array<double, 6> &values) const
{
    const QList<QByteArray> fields = record.split(',');
    if (fields.size() != static_cast<int>(values.size()))
        return false;
    for (int index = 0; index < fields.size(); ++index) {
        bool valid = false;
        const double value = fields[index].trimmed().toDouble(&valid);
        // Arduino Mega analogRead() values are in the inclusive 0..1023
        // range. This rejects numeric-looking garbage at a wrong baud rate.
        if (!valid || !std::isfinite(value) || value < 0.0 || value > 1023.0)
            return false;
        values[static_cast<std::size_t>(index)] = value;
    }
    return true;
}

void HallSensorController::acceptRecord(const std::array<double, 6> &values)
{
    m_latestValues = values;
    m_hasUnpublishedValues = true;
    m_invalidRecordStreak = 0;
    ++m_validRecordStreak;

    // Do not trust a single record: wrong baud rates can occasionally create
    // one numeric-looking line by chance.
    if (m_validRecordStreak >= 3 && m_state != Connected) {
        m_state = Connected;
        emit connectionStateChanged(
            m_state, QStringLiteral("Arduino sensor stream connected"));
    }

    if (m_state == Connected) {
        m_connectionTimer->setInterval(2000);
        m_connectionTimer->start();
    }
}

void HallSensorController::rejectRecord()
{
    m_validRecordStreak = 0;
    ++m_invalidRecordStreak;
    if (m_invalidRecordStreak >= 3) {
        reportInvalidStream(
            QStringLiteral("Corrupted sensor stream; check the baud rate"));
    }
}

void HallSensorController::reportInvalidStream(const QString &message)
{
    if (m_state == CommunicationError)
        return;
    m_state = CommunicationError;
    emit connectionStateChanged(m_state, message);
}

void HallSensorController::publishLatestValues()
{
    if (!m_hasUnpublishedValues || m_state != Connected)
        return;
    m_hasUnpublishedValues = false;
    QVector<double> values;
    values.reserve(static_cast<int>(m_latestValues.size()));
    for (const double value : m_latestValues)
        values.append(value);
    emit sensorValuesReady(values);
}

void HallSensorController::handleSerialError(int errorCode)
{
    const auto error = static_cast<QSerialPort::SerialPortError>(errorCode);
    if (error == QSerialPort::NoError
        || error == QSerialPort::TimeoutError
        || !m_serialPort->isOpen())
        return;
    if (error != QSerialPort::ResourceError
        && error != QSerialPort::DeviceNotFoundError
        && error != QSerialPort::PermissionError)
        return;
    const QString message = m_serialPort->errorString();
    closePort();
    m_state = CommunicationError;
    emit connectionStateChanged(m_state, message);
}

void HallSensorController::closePort()
{
    m_connectionTimer->stop();
    m_displayTimer->stop();
    if (m_serialPort->isOpen())
        m_serialPort->close();
    m_receiveBuffer.clear();
    m_hasUnpublishedValues = false;
    m_validRecordStreak = 0;
    m_invalidRecordStreak = 0;
}
