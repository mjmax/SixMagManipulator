#pragma once

#include <QMainWindow>

class ManipulatorView;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    ManipulatorView *m_manipulatorView = nullptr;
};

