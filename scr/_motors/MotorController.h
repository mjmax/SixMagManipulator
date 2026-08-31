#pragma once

#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

#include <array>
#include <memory>

class QThread;
class MotorWorker;
struct MotorStateStore;

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
    std::array<double, 6> latestAngles() const;
    void shutdown();

public slots:
    void connectEndpoint(const QString &endpoint, int baudRate,
                         const QVector<int> &motorIds);
    void disconnectEndpoint();

signals:
    void connectionStateChanged(int state, const QString &message);
    void motorStatesChanged(const QVector<int> &states);
    void guiAnglesReady(const QVector<double> &angles);

    void connectRequested(const QString &endpoint, int baudRate,
                          const QVector<int> &motorIds);
    void disconnectRequested();

private:
    QThread *m_workerThread = nullptr;
    MotorWorker *m_worker = nullptr;
    std::shared_ptr<MotorStateStore> m_stateStore;
};

