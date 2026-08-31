#include "MainWindow.h"

#include "ManipulatorView.h"
#include "MotorController.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QEvent>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <utility>

namespace {
class SpinArrowOverlay final : public QObject
{
public:
    explicit SpinArrowOverlay(QWidget *spinBox)
        : QObject(spinBox), m_spinBox(spinBox)
    {
        m_upArrow = makeArrow(QStringLiteral("▲"));
        m_downArrow = makeArrow(QStringLiteral("▼"));
        m_spinBox->installEventFilter(this);
        positionArrows();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_spinBox
            && (event->type() == QEvent::Resize
                || event->type() == QEvent::Show)) {
            positionArrows();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QLabel *makeArrow(const QString &symbol)
    {
        auto *label = new QLabel(symbol, m_spinBox);
        label->setAlignment(Qt::AlignCenter);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setStyleSheet(
            "background: transparent; border: none; color: #d9e4ef;"
            "font-family: 'Segoe UI Symbol'; font-size: 8px; font-weight: 700;"
            "padding: 0px; margin: 0px;");
        label->raise();
        return label;
    }

    void positionArrows()
    {
        constexpr int buttonWidth = 22;
        constexpr int buttonHeight = 13;
        const int left = m_spinBox->width() - buttonWidth;
        m_upArrow->setGeometry(left, 0, buttonWidth, buttonHeight);
        m_downArrow->setGeometry(
            left,
            std::max(0, m_spinBox->height() - buttonHeight),
            buttonWidth,
            buttonHeight);
        m_upArrow->raise();
        m_downArrow->raise();
    }

    QWidget *m_spinBox = nullptr;
    QLabel *m_upArrow = nullptr;
    QLabel *m_downArrow = nullptr;
};

class ComboArrowOverlay final : public QObject
{
public:
    explicit ComboArrowOverlay(QComboBox *comboBox)
        : QObject(comboBox), m_comboBox(comboBox)
    {
        m_arrow = new QLabel(QStringLiteral("▼"), m_comboBox);
        m_arrow->setAlignment(Qt::AlignCenter);
        m_arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_arrow->setStyleSheet(
            "background: transparent; border: none; color: #e5edf5;"
            "font-family: 'Segoe UI Symbol'; font-size: 9px; font-weight: 700;"
            "padding: 0px; margin: 0px;");
        m_comboBox->installEventFilter(this);
        positionArrow();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_comboBox
            && (event->type() == QEvent::Resize
                || event->type() == QEvent::Show)) {
            positionArrow();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void positionArrow()
    {
        constexpr int buttonWidth = 24;
        m_arrow->setGeometry(
            m_comboBox->width() - buttonWidth,
            0,
            buttonWidth,
            m_comboBox->height());
        m_arrow->raise();
    }

    QComboBox *m_comboBox = nullptr;
    QLabel *m_arrow = nullptr;
};

class RightAnchorOverlay final : public QObject
{
public:
    RightAnchorOverlay(QWidget *anchor, QWidget *overlay, QWidget *parent)
        : QObject(overlay),
          m_anchor(anchor),
          m_overlay(overlay),
          m_parent(parent)
    {
        m_anchor->installEventFilter(this);
        m_parent->installEventFilter(this);
        schedulePosition();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if ((watched == m_anchor || watched == m_parent)
            && (event->type() == QEvent::Show
                || event->type() == QEvent::Move
                || event->type() == QEvent::Resize
                || event->type() == QEvent::LayoutRequest)) {
            schedulePosition();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void schedulePosition()
    {
        QTimer::singleShot(0, this, [this] {
            if (!m_anchor || !m_overlay || !m_parent)
                return;
            const QPoint position = m_anchor->mapTo(
                m_parent, QPoint(m_anchor->width() + 10, 0));
            m_overlay->move(position);
            m_overlay->raise();
        });
    }

    QPointer<QWidget> m_anchor;
    QPointer<QWidget> m_overlay;
    QPointer<QWidget> m_parent;
};

class PortComboBox final : public QComboBox
{
public:
    using QComboBox::QComboBox;

    void setBeforePopup(std::function<void()> callback)
    {
        m_beforePopup = std::move(callback);
    }

protected:
    void showPopup() override
    {
        if (m_beforePopup)
            m_beforePopup();
        QComboBox::showPopup();
    }

private:
    std::function<void()> m_beforePopup;
};

class CompactTabWidget final : public QTabWidget
{
public:
    using QTabWidget::QTabWidget;

    QSize minimumSizeHint() const override { return {0, 0}; }
    QSize sizeHint() const override { return {300, 220}; }
};

class EqualPanel final : public QFrame
{
public:
    using QFrame::QFrame;

    QSize minimumSizeHint() const override { return {300, 0}; }
    QSize sizeHint() const override { return {300, 300}; }
};

class EqualPanelSplitter final : public QSplitter
{
public:
    explicit EqualPanelSplitter(QWidget *parent = nullptr)
        : QSplitter(Qt::Vertical, parent)
    {
    }

    QSize minimumSizeHint() const override { return {300, 0}; }
    QSize sizeHint() const override { return {300, 600}; }
};

class EditorFocusReleaseFilter final : public QObject
{
public:
    explicit EditorFocusReleaseFilter(QWidget *window)
        : QObject(window), m_window(window)
    {
        m_window->setFocusPolicy(Qt::StrongFocus);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *eventWidget = qobject_cast<QWidget *>(watched);

        if (event->type() == QEvent::FocusIn) {
            if (QWidget *editor = editorFor(eventWidget))
                m_activeEditor = editor;
            else
                m_activeEditor.clear();
        } else if (event->type() == QEvent::MouseButtonPress) {
            QWidget *editor = editorFor(QApplication::focusWidget());
            if (!editor)
                editor = m_activeEditor.data();
            if (editor) {
                const bool clickedOutside = !eventWidget
                    || (eventWidget != editor
                        && !editor->isAncestorOf(eventWidget));
                if (clickedOutside) {
                    m_activeEditor = editor;
                    releaseEditorFocus();
                }
            }
        } else if (event->type() == QEvent::WindowDeactivate
                   && eventWidget && eventWidget->window() == m_window) {
            releaseEditorFocus();
        }

        return QObject::eventFilter(watched, event);
    }

private:
    QWidget *editorFor(QWidget *widget) const
    {
        while (widget) {
            if (qobject_cast<QAbstractSpinBox *>(widget)
                || qobject_cast<QComboBox *>(widget)) {
                return widget;
            }
            if (widget == m_window)
                break;
            widget = widget->parentWidget();
        }
        return nullptr;
    }

    void releaseEditorFocus()
    {
        QWidget *editor = m_activeEditor.data();
        m_activeEditor.clear();
        if (editor) {
            QWidget *focusedWidget = QApplication::focusWidget();
            if (focusedWidget
                && (focusedWidget == editor
                    || editor->isAncestorOf(focusedWidget))) {
                focusedWidget->clearFocus();
            }
            editor->clearFocus();
            if (m_window->isActiveWindow())
                m_window->setFocus(Qt::MouseFocusReason);
        }
    }

    QWidget *m_window = nullptr;
    QPointer<QWidget> m_activeEditor;
};

QIcon colorIcon(const QColor &color)
{
    QPixmap swatch(42, 22);
    swatch.fill(Qt::transparent);
    QPainter painter(&swatch);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(255, 255, 255, 90), 1.0));
    painter.setBrush(color);
    painter.drawRoundedRect(swatch.rect().adjusted(2, 2, -2, -2), 4, 4);
    return QIcon(swatch);
}

QLabel *makeFieldLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName("fieldLabel");
    return label;
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
    qApp->installEventFilter(new EditorFocusReleaseFilter(this));

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(24, 18, 24, 24);
    root->setSpacing(14);

    auto *title = new QLabel("MAGNETIC MANIPULATOR");
    title->setObjectName("applicationTitle");
    auto *subtitle = new QLabel("Workspace, object tracking, and magnet orientation");
    subtitle->setObjectName("applicationSubtitle");
    root->addWidget(title);
    root->addWidget(subtitle);

    auto *content = new QHBoxLayout;
    content->setSpacing(18);

    m_manipulatorView = new ManipulatorView;
    m_motorController = new MotorController(this);
    m_manipulatorView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    content->addWidget(m_manipulatorView, 1);

    auto *rightColumn = new EqualPanelSplitter;
    rightColumn->setChildrenCollapsible(true);
    rightColumn->setHandleWidth(14);

    auto *statusPanel = new EqualPanel;
    statusPanel->setObjectName("panel");
    statusPanel->setMinimumWidth(300);
    statusPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    auto *statusLayout = new QVBoxLayout(statusPanel);
    statusLayout->setContentsMargins(22, 18, 22, 18);
    statusLayout->setSpacing(7);

    auto *statusHeading = new QLabel("SYSTEM STATUS");
    statusHeading->setObjectName("panelHeading");
    m_trackingState = new QLabel("SEARCHING FOR OBJECT");
    m_trackingState->setObjectName("trackingState");
    m_trackingPosition = new QLabel("Visible circular workspace only");
    m_trackingPosition->setObjectName("statusDetail");
    m_trackingPerformance = new QLabel("Detector starting...");
    m_trackingPerformance->setObjectName("statusDetail");
    statusLayout->addWidget(statusHeading);
    statusLayout->addSpacing(3);
    statusLayout->addWidget(m_trackingState);
    statusLayout->addWidget(m_trackingPosition);
    statusLayout->addWidget(m_trackingPerformance);
    statusLayout->addStretch();
    rightColumn->addWidget(statusPanel);

    auto *controlPanel = new EqualPanel;
    controlPanel->setObjectName("panel");
    controlPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    auto *controlLayout = new QVBoxLayout(controlPanel);
    controlLayout->setContentsMargins(14, 14, 14, 14);
    controlLayout->setSpacing(10);
    controlLayout->setSizeConstraint(QLayout::SetNoConstraint);

    auto *controlHeading = new QLabel("CONTROL PANEL");
    controlHeading->setObjectName("panelHeading");
    controlLayout->addWidget(controlHeading);

    auto *tabs = new CompactTabWidget;
    tabs->setObjectName("controlTabs");
    tabs->setMinimumHeight(0);
    tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    auto *controlTab = new QWidget;
    auto *actuatorsTab = new QWidget;
    auto *imageProcessingTab = new QWidget;

    auto *actuatorLayout = new QVBoxLayout(actuatorsTab);
    actuatorLayout->setContentsMargins(10, 8, 10, 8);
    actuatorLayout->setSpacing(10);

    auto *actuatorForm = new QGridLayout;
    actuatorForm->setContentsMargins(0, 0, 0, 0);
    actuatorForm->setHorizontalSpacing(10);
    actuatorForm->setVerticalSpacing(3);

    auto *portSelector = new PortComboBox;
    portSelector->setObjectName("actuatorPortSelector");
    portSelector->setFixedWidth(170);
    portSelector->setToolTip("USB2Dynamixel COM port or the local test simulator");

    auto *baudSelector = new QComboBox;
    baudSelector->setObjectName("actuatorBaudSelector");
    baudSelector->setFixedWidth(112);
    const QList<int> supportedBaudRates = {
        9600, 19200, 57600, 115200, 200000,
        250000, 400000, 500000, 1000000
    };
    for (const int baudRate : supportedBaudRates) {
        baudSelector->addItem(
            QLocale(QLocale::English).toString(baudRate), baudRate);
    }
    baudSelector->setCurrentIndex(baudSelector->findData(1000000));

    new ComboArrowOverlay(portSelector);
    new ComboArrowOverlay(baudSelector);

    auto *motorConnectButton = new QPushButton("Connect");
    motorConnectButton->setObjectName("motorConnectButton");
    motorConnectButton->setFixedSize(118, 28);
    motorConnectButton->setProperty("connectionState",
                                    MotorController::Disconnected);

    auto *pollActuatorsPanel = new QWidget(actuatorsTab);
    pollActuatorsPanel->setFixedSize(84, 74);
    auto *pollActuatorsLayout = new QVBoxLayout(pollActuatorsPanel);
    pollActuatorsLayout->setContentsMargins(0, 0, 0, 0);
    pollActuatorsLayout->setSpacing(3);
    auto *pollActuatorsButton = new QPushButton("Poll\nActuators");
    pollActuatorsButton->setObjectName("pollActuatorsButton");
    pollActuatorsButton->setFixedSize(68, 50);
    pollActuatorsButton->setEnabled(false);
    pollActuatorsButton->setProperty("benchmarkRunning", false);
    pollActuatorsButton->setToolTip(
        "Measure the average time for 100 complete six-motor position reads");
    auto *pollBenchmarkLabel = new QLabel("—");
    pollBenchmarkLabel->setObjectName("pollBenchmarkLabel");
    pollBenchmarkLabel->setFixedSize(84, 18);
    pollBenchmarkLabel->setAlignment(Qt::AlignCenter);
    pollActuatorsLayout->addWidget(
        pollActuatorsButton, 0, Qt::AlignHCenter);
    pollActuatorsLayout->addWidget(
        pollBenchmarkLabel, 0, Qt::AlignHCenter);

    actuatorForm->addWidget(makeFieldLabel("COM port"), 0, 0);
    actuatorForm->addWidget(makeFieldLabel("Baud rate"), 0, 1);
    actuatorForm->addWidget(makeFieldLabel("Connection"), 0, 2);
    actuatorForm->addWidget(portSelector, 1, 0, Qt::AlignLeft);
    actuatorForm->addWidget(baudSelector, 1, 1, Qt::AlignLeft);
    actuatorForm->addWidget(motorConnectButton, 1, 2, Qt::AlignLeft);
    actuatorForm->setColumnStretch(3, 1);
    new RightAnchorOverlay(
        motorConnectButton, pollActuatorsPanel, actuatorsTab);
    actuatorLayout->addLayout(actuatorForm);

    auto *motorStatusRow = new QHBoxLayout;
    motorStatusRow->setContentsMargins(0, 2, 0, 0);
    motorStatusRow->setSpacing(10);
    auto *motorStatusLabel = new QLabel(
        "<div>MOTOR STATUS</div>"
        "<div style=\"margin-top: 18px;\">MOTOR IDs</div>");
    motorStatusLabel->setObjectName("fieldLabel");
    motorStatusLabel->setTextFormat(Qt::RichText);
    motorStatusRow->addWidget(motorStatusLabel, 0, Qt::AlignTop);
    QVector<QFrame *> motorStatusLights;
    QVector<QSpinBox *> motorIdEditors;
    motorStatusLights.reserve(6);
    motorIdEditors.reserve(6);
    for (int index = 0; index < 6; ++index) {
        auto *motorColumn = new QVBoxLayout;
        motorColumn->setContentsMargins(0, 0, 0, 0);
        motorColumn->setSpacing(3);
        motorColumn->setAlignment(Qt::AlignHCenter);

        auto *idLabel = new QLabel(QStringLiteral("M%1").arg(index + 1));
        idLabel->setObjectName("motorIdLabel");
        idLabel->setAlignment(Qt::AlignCenter);

        auto *light = new QFrame;
        light->setObjectName("motorStatusLight");
        light->setFixedSize(16, 16);
        light->setProperty("motorState", 0);
        light->setToolTip(QStringLiteral("M%1 / servo ID %2: unavailable")
                              .arg(index + 1).arg(index + 1));

        auto *idEditor = new QSpinBox;
        idEditor->setObjectName("motorIdEditor");
        idEditor->setRange(0, 253);
        idEditor->setValue(index + 1);
        idEditor->setFixedSize(44, 24);
        idEditor->setAlignment(Qt::AlignCenter);
        idEditor->setButtonSymbols(QAbstractSpinBox::NoButtons);
        idEditor->setToolTip(
            QStringLiteral("DYNAMIXEL bus ID assigned to M%1").arg(index + 1));

        motorStatusLights.append(light);
        motorIdEditors.append(idEditor);
        motorColumn->addWidget(idLabel, 0, Qt::AlignHCenter);
        motorColumn->addWidget(light, 0, Qt::AlignHCenter);
        motorColumn->addWidget(idEditor, 0, Qt::AlignHCenter);
        motorStatusRow->addLayout(motorColumn);
    }
    motorStatusRow->addStretch();
    actuatorLayout->addLayout(motorStatusRow);
    actuatorLayout->addStretch();

    auto refreshActuatorPorts = [portSelector] {
        if (!portSelector->isEnabled())
            return;
        const QString selectedEndpoint = portSelector->currentData().toString();
        QSignalBlocker blocker(portSelector);
        portSelector->clear();
        for (const auto &endpoint : MotorController::availableEndpoints())
            portSelector->addItem(endpoint.first, endpoint.second);
        const int previousIndex = portSelector->findData(selectedEndpoint);
        if (previousIndex >= 0)
            portSelector->setCurrentIndex(previousIndex);
    };
    portSelector->setBeforePopup(refreshActuatorPorts);
    refreshActuatorPorts();

    auto *portRefreshTimer = new QTimer(actuatorsTab);
    portRefreshTimer->setInterval(1000);
    connect(portRefreshTimer, &QTimer::timeout,
            this, refreshActuatorPorts);
    portRefreshTimer->start();

    auto *imageLayout = new QVBoxLayout(imageProcessingTab);
    imageLayout->setContentsMargins(10, 8, 10, 8);
    imageLayout->setSpacing(4);

    auto *form = new QGridLayout;
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(3);
    form->setContentsMargins(0, 0, 0, 0);

    auto *threshold = new QSpinBox;
    threshold->setRange(0, 255);
    threshold->setValue(90);
    threshold->setToolTip("Pixels at or below this brightness are treated as object pixels");

    auto *minimumArea = new QSpinBox;
    minimumArea->setRange(1, 19999);
    minimumArea->setValue(30);
    minimumArea->setSuffix(" px²");

    auto *maximumArea = new QSpinBox;
    maximumArea->setRange(31, 500000);
    maximumArea->setValue(20000);
    maximumArea->setSuffix(" px²");

    auto *minimumCircularity = new QDoubleSpinBox;
    minimumCircularity->setRange(0.05, 1.0);
    minimumCircularity->setDecimals(2);
    minimumCircularity->setSingleStep(0.05);
    minimumCircularity->setValue(0.45);

    auto *visualizationRate = new QSpinBox;
    visualizationRate->setRange(1, 60);
    visualizationRate->setValue(15);
    visualizationRate->setSuffix(" Hz");
    visualizationRate->setToolTip("Changes GUI drawing rate only; detector processing remains full speed");

    auto *traceColor = new QComboBox;
    traceColor->setObjectName("colorSelector");
    traceColor->setIconSize(QSize(42, 22));
    const QVector<QColor> colors = {
        QColor("#ff4b55"), QColor("#2ecc71"), QColor("#3498db"),
        QColor("#f1c40f"), QColor("#ff8c32"), QColor("#111111"),
        QColor("#22d3ee"), QColor("#d946ef"), QColor("#f5f7fa")
    };
    for (const QColor &color : colors)
        traceColor->addItem(colorIcon(color), QString(), color);

    new ComboArrowOverlay(traceColor);

    auto *traceWidth = new QDoubleSpinBox;
    traceWidth->setRange(0.5, 12.0);
    traceWidth->setDecimals(1);
    traceWidth->setSingleStep(0.5);
    traceWidth->setValue(2.5);
    traceWidth->setSuffix(" px");

    auto *maximumTraceDots = new QSpinBox;
    maximumTraceDots->setRange(0, 64);
    maximumTraceDots->setValue(16);
    maximumTraceDots->setToolTip(
        "Maximum estimated dots inserted between measured positions; 0 disables interpolation");

    auto *traceToggle = new QPushButton("Trace On");
    traceToggle->setObjectName("traceToggleButton");
    traceToggle->setCheckable(true);
    traceToggle->setChecked(false);
    traceToggle->setToolTip("Enable or disable recording the object's trace path");

    const std::initializer_list<QWidget *> spinBoxes = {
        threshold,
        minimumArea,
        maximumArea,
        minimumCircularity,
        visualizationRate,
        traceWidth,
        maximumTraceDots
    };
    for (QWidget *spinBox : spinBoxes)
        new SpinArrowOverlay(spinBox);

    // Keep the configuration column compact and leave the rest of the tab open
    // for controls added in later stages.
    constexpr int processingFieldWidth = 96;
    threshold->setFixedWidth(processingFieldWidth);
    minimumArea->setFixedWidth(processingFieldWidth);
    maximumArea->setFixedWidth(processingFieldWidth);
    minimumCircularity->setFixedWidth(processingFieldWidth);
    visualizationRate->setFixedWidth(processingFieldWidth);
    traceColor->setFixedWidth(processingFieldWidth);
    traceWidth->setFixedWidth(processingFieldWidth);
    maximumTraceDots->setFixedWidth(processingFieldWidth);
    traceToggle->setFixedSize(processingFieldWidth, 28);

    const auto addProcessingControl =
        [form](int groupRow, int column, QLabel *label, QWidget *field) {
            const int labelRow = groupRow * 2;
            form->addWidget(label, labelRow, column, Qt::AlignLeft | Qt::AlignBottom);
            form->addWidget(field, labelRow + 1, column, Qt::AlignLeft | Qt::AlignTop);
        };

    addProcessingControl(0, 0, makeFieldLabel("Dark threshold"), threshold);
    addProcessingControl(0, 1, makeFieldLabel("Minimum object area"), minimumArea);
    addProcessingControl(1, 0, makeFieldLabel("Maximum object area"), maximumArea);
    addProcessingControl(1, 1, makeFieldLabel("Minimum circularity"), minimumCircularity);
    addProcessingControl(2, 0, makeFieldLabel("GUI refresh rate"), visualizationRate);
    addProcessingControl(2, 1, makeFieldLabel("Trace color"), traceColor);
    addProcessingControl(3, 0, makeFieldLabel("Trace line width"), traceWidth);
    form->addWidget(traceToggle, 7, 1, Qt::AlignLeft | Qt::AlignTop);
    addProcessingControl(4, 0, makeFieldLabel("Max Trace Dots"), maximumTraceDots);
    form->setColumnStretch(2, 1);
    imageLayout->addLayout(form);

    imageLayout->addStretch();

    tabs->addTab(controlTab, "Control");
    tabs->addTab(actuatorsTab, "Actuators");
    tabs->addTab(imageProcessingTab, "Image Processing");
    controlLayout->addWidget(tabs, 1);
    rightColumn->addWidget(controlPanel);
    rightColumn->setStretchFactor(0, 1);
    rightColumn->setStretchFactor(1, 1);
    rightColumn->setSizes({1, 1});
    content->addWidget(rightColumn, 1);
    root->addLayout(content, 1);

    connect(motorConnectButton, &QPushButton::clicked,
            this, [this, portSelector, baudSelector, motorConnectButton,
                   motorIdEditors] {
        const int state =
            motorConnectButton->property("connectionState").toInt();
        if (state == MotorController::Connected
            || state == MotorController::CommunicationError) {
            m_motorController->disconnectEndpoint();
            return;
        }
        const QString endpoint = portSelector->currentData().toString();
        if (!endpoint.isEmpty()) {
            QVector<int> motorIds;
            motorIds.reserve(motorIdEditors.size());
            for (const QSpinBox *editor : motorIdEditors)
                motorIds.append(editor->value());
            m_motorController->connectEndpoint(
                endpoint, baudSelector->currentData().toInt(), motorIds);
        }
    });
    connect(pollActuatorsButton, &QPushButton::clicked,
            this, [this, pollActuatorsButton, pollBenchmarkLabel] {
        pollActuatorsButton->setProperty("benchmarkRunning", true);
        pollActuatorsButton->setText("Polling...");
        pollActuatorsButton->setEnabled(false);
        pollBenchmarkLabel->setText("Starting...");
        pollActuatorsButton->style()->unpolish(pollActuatorsButton);
        pollActuatorsButton->style()->polish(pollActuatorsButton);
        m_motorController->startPollBenchmark(100);
    });
    connect(m_motorController, &MotorController::connectionStateChanged,
            this, [portSelector, baudSelector, motorConnectButton,
                   motorIdEditors, pollActuatorsButton](
                      int state, const QString &message) {
        motorConnectButton->setProperty("connectionState", state);
        switch (state) {
        case MotorController::Connecting:
            motorConnectButton->setText("Scanning...");
            motorConnectButton->setEnabled(false);
            break;
        case MotorController::Connected:
            motorConnectButton->setText("Connected");
            motorConnectButton->setEnabled(true);
            break;
        case MotorController::CommunicationError:
            motorConnectButton->setText("Disconnect");
            motorConnectButton->setEnabled(true);
            break;
        default:
            motorConnectButton->setText("Connect");
            motorConnectButton->setEnabled(true);
            break;
        }
        const bool settingsEnabled =
            state != MotorController::Connecting
            && state != MotorController::Connected;
        portSelector->setEnabled(settingsEnabled);
        baudSelector->setEnabled(settingsEnabled);
        for (QSpinBox *editor : motorIdEditors)
            editor->setEnabled(settingsEnabled);
        const bool benchmarkRunning =
            pollActuatorsButton->property("benchmarkRunning").toBool();
        pollActuatorsButton->setEnabled(
            state == MotorController::Connected && !benchmarkRunning);
        motorConnectButton->setToolTip(message);
        motorConnectButton->style()->unpolish(motorConnectButton);
        motorConnectButton->style()->polish(motorConnectButton);
    });
    connect(m_motorController, &MotorController::pollBenchmarkProgress,
            this, [pollBenchmarkLabel](double averageMilliseconds,
                                       int completedCycles,
                                       int totalCycles) {
        pollBenchmarkLabel->setText(
            QStringLiteral("%1 ms").arg(averageMilliseconds, 0, 'f', 3));
        pollBenchmarkLabel->setToolTip(
            QStringLiteral("%1 of %2 cycles")
                .arg(completedCycles).arg(totalCycles));
    });
    connect(m_motorController, &MotorController::pollBenchmarkFinished,
            this, [motorConnectButton, pollActuatorsButton,
                   pollBenchmarkLabel](double averageMilliseconds) {
        pollActuatorsButton->setProperty("benchmarkRunning", false);
        pollActuatorsButton->setText("Poll\nActuators");
        pollActuatorsButton->setEnabled(
            motorConnectButton->property("connectionState").toInt()
            == MotorController::Connected);
        pollBenchmarkLabel->setText(
            QStringLiteral("%1 ms").arg(averageMilliseconds, 0, 'f', 3));
        pollActuatorsButton->style()->unpolish(pollActuatorsButton);
        pollActuatorsButton->style()->polish(pollActuatorsButton);
    });
    connect(m_motorController, &MotorController::pollBenchmarkFailed,
            this, [motorConnectButton, pollActuatorsButton,
                   pollBenchmarkLabel](const QString &message) {
        pollActuatorsButton->setProperty("benchmarkRunning", false);
        pollActuatorsButton->setText("Poll\nActuators");
        pollActuatorsButton->setEnabled(
            motorConnectButton->property("connectionState").toInt()
            == MotorController::Connected);
        pollBenchmarkLabel->setText("Test Failed");
        pollBenchmarkLabel->setToolTip(message);
        pollActuatorsButton->style()->unpolish(pollActuatorsButton);
        pollActuatorsButton->style()->polish(pollActuatorsButton);
    });
    connect(m_motorController, &MotorController::motorStatesChanged,
            this, [motorStatusLights, motorIdEditors](
                      const QVector<int> &states) {
        const int count = std::min(motorStatusLights.size(), states.size());
        for (int index = 0; index < count; ++index) {
            QFrame *light = motorStatusLights[index];
            light->setProperty("motorState", states[index]);
            const QString condition = states[index] == 1
                ? QStringLiteral("available")
                : (states[index] == 2
                    ? QStringLiteral("communication error")
                    : QStringLiteral("unavailable"));
            light->setToolTip(QStringLiteral("M%1 / servo ID %2: %3")
                                  .arg(index + 1)
                                  .arg(motorIdEditors[index]->value())
                                  .arg(condition));
            light->style()->unpolish(light);
            light->style()->polish(light);
        }
    });
    connect(m_motorController, &MotorController::guiAnglesReady,
            this, [this](const QVector<double> &angles) {
        const int count = std::min(6, static_cast<int>(angles.size()));
        for (int index = 0; index < count; ++index)
            m_manipulatorView->setMagnetAngle(index, angles[index]);
    });

    connect(traceToggle, &QPushButton::toggled,
            this, [this, traceToggle](bool enabled) {
        traceToggle->setText(enabled ? "Trace Off" : "Trace On");
        m_manipulatorView->setTraceEnabled(enabled);
    });
    connect(threshold, qOverload<int>(&QSpinBox::valueChanged),
            m_manipulatorView, &ManipulatorView::setDetectionThreshold);
    connect(minimumArea, qOverload<int>(&QSpinBox::valueChanged),
            this, [this, maximumArea](int value) {
        maximumArea->setMinimum(value + 1);
        m_manipulatorView->setDetectionMinimumArea(value);
    });
    connect(maximumArea, qOverload<int>(&QSpinBox::valueChanged),
            this, [this, minimumArea](int value) {
        minimumArea->setMaximum(value - 1);
        m_manipulatorView->setDetectionMaximumArea(value);
    });
    connect(minimumCircularity,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            m_manipulatorView,
            &ManipulatorView::setDetectionMinimumCircularity);
    connect(visualizationRate, qOverload<int>(&QSpinBox::valueChanged),
            m_manipulatorView, &ManipulatorView::setVisualizationRate);
    connect(traceColor, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this, traceColor](int index) {
        m_manipulatorView->setTraceColor(traceColor->itemData(index).value<QColor>());
    });
    connect(traceWidth, qOverload<double>(&QDoubleSpinBox::valueChanged),
            m_manipulatorView, &ManipulatorView::setTraceWidth);
    connect(maximumTraceDots, qOverload<int>(&QSpinBox::valueChanged),
            m_manipulatorView, &ManipulatorView::setMaximumTraceDots);

    connect(m_manipulatorView, &ManipulatorView::trackingStatusChanged,
            this,
            [this](bool detected,
                   const QPointF &position,
                   double detectionMilliseconds,
                   double processingFramesPerSecond) {
        if (detected) {
            m_trackingState->setText("OBJECT DETECTED");
            m_trackingState->setProperty("detected", true);
            m_trackingPosition->setText(QStringLiteral("Workspace x %1   y %2")
                .arg(position.x(), 0, 'f', 3)
                .arg(position.y(), 0, 'f', 3));
        } else {
            m_trackingState->setText("SEARCHING FOR OBJECT");
            m_trackingState->setProperty("detected", false);
            m_trackingPosition->setText("Visible circular workspace only");
        }
        m_trackingState->style()->unpolish(m_trackingState);
        m_trackingState->style()->polish(m_trackingState);
        m_trackingPerformance->setText(
            QStringLiteral("%1 FPS detector  •  %2 ms")
                .arg(processingFramesPerSecond, 0, 'f', 1)
                .arg(detectionMilliseconds, 0, 'f', 2));
    });

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
        QFrame#panel {
            background: #171e27;
            border: 1px solid #293442;
            border-radius: 10px;
        }
        QSplitter::handle {
            background: transparent;
        }
        QLabel#panelHeading {
            color: #b8c5d3;
            font-size: 11px;
            font-weight: 700;
            letter-spacing: 1px;
        }
        QLabel#panelBody, QLabel#scopeNote, QLabel#statusDetail {
            color: #8492a3;
            font-size: 11px;
        }
        QLabel#scopeNote {
            background: #111821;
            border: 1px solid #263341;
            border-radius: 6px;
            padding: 8px;
        }
        QLabel#trackingState {
            color: #f2b84b;
            font-size: 12px;
            font-weight: 700;
        }
        QLabel#trackingState[detected="true"] {
            color: #58f0a4;
        }
        QLabel#fieldLabel {
            color: #aab6c4;
            font-size: 11px;
        }
        QTabWidget#controlTabs::pane {
            border: 1px solid #2b3745;
            border-radius: 7px;
            background: #141b24;
            top: -1px;
        }
        QTabBar::tab {
            background: #111821;
            color: #8391a2;
            border: 1px solid #2b3745;
            padding: 9px 18px;
            margin-right: 3px;
        }
        QTabBar::tab:selected {
            background: #263445;
            color: #f1f5f9;
            border-bottom-color: #263445;
        }
        QSpinBox, QDoubleSpinBox, QComboBox {
            background: #0f151d;
            color: #eef3f8;
            border: 1px solid #344253;
            border-radius: 5px;
            padding: 2px 7px;
            min-height: 18px;
        }
        QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
            border-color: #4d8bc9;
        }
        QSpinBox::up-button, QDoubleSpinBox::up-button {
            subcontrol-origin: border;
            subcontrol-position: top right;
            width: 22px;
            height: 13px;
            border-left: 1px solid #344253;
            border-bottom: 1px solid #273544;
        }
        QSpinBox::down-button, QDoubleSpinBox::down-button {
            subcontrol-origin: border;
            subcontrol-position: bottom right;
            width: 22px;
            height: 13px;
            border-left: 1px solid #344253;
        }
        QSpinBox::up-arrow, QDoubleSpinBox::up-arrow,
        QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
            image: none;
            width: 0;
            height: 0;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 24px;
            border-left: 1px solid #344253;
        }
        QComboBox::down-arrow {
            image: none;
            width: 0;
            height: 0;
        }
        QComboBox#colorSelector {
            padding-left: 8px;
        }
        QComboBox QAbstractItemView {
            background: #111821;
            color: #eef3f8;
            border: 1px solid #344253;
            selection-background-color: #26384b;
            padding: 4px;
        }
        QLabel#motorIdLabel {
            color: #aebac7;
            font-size: 9px;
            font-weight: 700;
        }
        QSpinBox#motorIdEditor {
            padding: 1px 3px;
            font-size: 9px;
        }
        QFrame#motorStatusLight {
            background: #303a46;
            border: 1px solid #647181;
            border-radius: 8px;
        }
        QFrame#motorStatusLight[motorState="1"] {
            background: #32d583;
            border: 1px solid #9affca;
        }
        QFrame#motorStatusLight[motorState="2"] {
            background: #ef4f5f;
            border: 1px solid #ff9ca6;
        }
        QLabel#pollBenchmarkLabel {
            color: #aebac7;
            font-size: 9px;
            font-weight: 600;
        }
        QPushButton#pollActuatorsButton {
            background: #0f151d;
            color: #d5dee8;
            border: 1px solid #3b4b5d;
            border-radius: 6px;
            padding: 2px;
            font-size: 9px;
            font-weight: 700;
        }
        QPushButton#pollActuatorsButton:hover:enabled {
            background: #1a2633;
            border-color: #60758c;
        }
        QPushButton#pollActuatorsButton[benchmarkRunning="true"] {
            background: #8b2f39;
            color: #fff1f2;
            border-color: #f07480;
        }
        QPushButton#motorConnectButton {
            background: #0f151d;
            color: #d5dee8;
            border: 1px solid #3b4b5d;
            border-radius: 5px;
            padding: 2px 7px;
            font-size: 10px;
            font-weight: 700;
        }
        QPushButton#motorConnectButton:hover {
            background: #1a2633;
            border-color: #60758c;
        }
        QPushButton#motorConnectButton[connectionState="1"] {
            background: #8a651e;
            color: #fff3d2;
            border-color: #dbad4b;
        }
        QPushButton#motorConnectButton[connectionState="2"] {
            background: #237a50;
            color: #f4fff9;
            border-color: #58d99a;
        }
        QPushButton#motorConnectButton[connectionState="3"] {
            background: #8b2f39;
            color: #fff1f2;
            border-color: #f07480;
        }
        QPushButton#traceToggleButton {
            background: #0f151d;
            color: #aab6c4;
            border: 1px solid #344253;
            border-radius: 5px;
            padding: 2px 7px;
            font-size: 10px;
            font-weight: 700;
        }
        QPushButton#traceToggleButton:hover {
            background: #182330;
            border-color: #4d6177;
        }
        QPushButton#traceToggleButton:checked {
            background: #237a50;
            color: #f4fff9;
            border-color: #58d99a;
        }
        QPushButton#traceToggleButton:checked:hover {
            background: #2a8c5d;
        }
        QPushButton#clearTraceButton {
            background: #263445;
            color: #eef3f8;
            border: 1px solid #3b4d61;
            border-radius: 6px;
            padding: 8px 12px;
            font-weight: 600;
        }
        QPushButton#clearTraceButton:hover {
            background: #30445a;
            border-color: #547293;
        }
        QPushButton#clearTraceButton:pressed {
            background: #1d2b3a;
        }
    )");
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_motorController)
        m_motorController->shutdown();

#ifdef Q_OS_WIN
    QProcess::execute(QStringLiteral("taskkill"),
                      {QStringLiteral("/IM"),
                       QStringLiteral("SixMagMotorEmulator.exe"),
                       QStringLiteral("/T"),
                       QStringLiteral("/F")});
#else
    QProcess::execute(QStringLiteral("pkill"),
                      {QStringLiteral("-x"),
                       QStringLiteral("SixMagMotorEmulator")});
#endif

    QMainWindow::closeEvent(event);
}
