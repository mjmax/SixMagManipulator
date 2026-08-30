#pragma once

#include <QMainWindow>

class QLabel;
class ManipulatorView;
class QCloseEvent;
class MotorController;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    ManipulatorView *m_manipulatorView = nullptr;
    MotorController *m_motorController = nullptr;
    QLabel *m_trackingState = nullptr;
    QLabel *m_trackingPosition = nullptr;
    QLabel *m_trackingPerformance = nullptr;
};

