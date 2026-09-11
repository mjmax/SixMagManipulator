#pragma once

#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

#include <array>

class QSerialPort;
class QTimer;

class HallSensorController final : public QObject
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

    explicit HallSensorController(QObject *parent = nullptr);
    ~HallSensorController() override;

    static QList<QPair<QString, QString>> availablePorts();
    void shutdown();

public slots:
    void connectPort(const QString &portName, int baudRate);
    void disconnectPort();

signals:
    void connectionStateChanged(int state, const QString &message);
    void sensorValuesReady(const QVector<double> &values);

private slots:
    void readAvailableData();
    void publishLatestValues();
    void handleSerialError(int errorCode);

private:
    bool parseRecord(const QByteArray &record,
                     std::array<double, 6> &values) const;
    void acceptRecord(const std::array<double, 6> &values);
    void rejectRecord();
    void reportInvalidStream(const QString &message);
    void closePort();

    QSerialPort *m_serialPort = nullptr;
    QTimer *m_connectionTimer = nullptr;
    QTimer *m_displayTimer = nullptr;
    QByteArray m_receiveBuffer;
    std::array<double, 6> m_latestValues{};
    ConnectionState m_state = Disconnected;
    int m_validRecordStreak = 0;
    int m_invalidRecordStreak = 0;
    bool m_hasUnpublishedValues = false;
    bool m_shutdownComplete = false;
};
