#include "MainWindow.h"

#include "ManipulatorView.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QVBoxLayout>
#include <QWidget>

namespace {
QFrame *makeFuturePanel(const QString &title, const QString &description)
{
    auto *panel = new QFrame;
    panel->setObjectName("futurePanel");
    panel->setMinimumWidth(300);

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(8);

    auto *heading = new QLabel(title);
    heading->setObjectName("panelHeading");
    auto *body = new QLabel(description);
    body->setObjectName("panelBody");
    body->setWordWrap(true);

    layout->addWidget(heading);
    layout->addWidget(body);
    layout->addStretch();
    return panel;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Six-Magnet Manipulator Control");
    resize(1280, 820);
    setMinimumSize(980, 680);

    auto *central = new QWidget;
    central->setObjectName("central");
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(24, 18, 24, 24);
    root->setSpacing(14);

    auto *title = new QLabel("MAGNETIC MANIPULATOR");
    title->setObjectName("applicationTitle");
    auto *subtitle = new QLabel("Workspace and magnet orientation");
    subtitle->setObjectName("applicationSubtitle");

    root->addWidget(title);
    root->addWidget(subtitle);

    auto *content = new QHBoxLayout;
    content->setSpacing(18);

    m_manipulatorView = new ManipulatorView;
    m_manipulatorView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    content->addWidget(m_manipulatorView, 1);

    auto *futureColumn = new QVBoxLayout;
    futureColumn->setSpacing(14);
    futureColumn->addWidget(makeFuturePanel(
        "SYSTEM STATUS",
        "Camera, servo, controller, and safety status will be added here."));
    futureColumn->addWidget(makeFuturePanel(
        "CONTROL PANEL",
        "Experiment controls and live parameters will be added in the next GUI stage."));
    content->addLayout(futureColumn, 1);

    root->addLayout(content, 1);

    setStyleSheet(R"(
        QWidget#central {
            background: #10151c;
            color: #e8edf3;
        }
        QLabel#applicationTitle {
            color: #f4f7fa;
            font-size: 20px;
            font-weight: 700;
            letter-spacing: 2px;
        }
        QLabel#applicationSubtitle {
            color: #8795a6;
            font-size: 11px;
            margin-bottom: 2px;
        }
        QFrame#futurePanel {
            background: #171e27;
            border: 1px solid #293442;
            border-radius: 10px;
        }
        QLabel#panelHeading {
            color: #b8c5d3;
            font-size: 11px;
            font-weight: 700;
            letter-spacing: 1px;
        }
        QLabel#panelBody {
            color: #748294;
            font-size: 11px;
        }
    )");
}

