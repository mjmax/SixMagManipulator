#pragma once

#include <QMainWindow>

class QLabel;
class ManipulatorView;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    ManipulatorView *m_manipulatorView = nullptr;
    QLabel *m_trackingState = nullptr;
    QLabel *m_trackingPosition = nullptr;
    QLabel *m_trackingPerformance = nullptr;
};

