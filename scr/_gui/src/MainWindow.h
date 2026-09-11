#pragma once

#include <QMainWindow>

class QLabel;
class ManipulatorView;
class QCloseEvent;
class MotorController;
class HallSensorController;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void shutdownResources();

    ManipulatorView *m_manipulatorView = nullptr;
    MotorController *m_motorController = nullptr;
    HallSensorController *m_hallSensorController = nullptr;
    QLabel *m_trackingState = nullptr;
    QLabel *m_trackingPosition = nullptr;
    QLabel *m_trackingPerformance = nullptr;
    bool m_shutdownComplete = false;
};
