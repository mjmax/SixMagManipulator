#include "MainWindow.h"

#include "ManipulatorView.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

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
    auto *imageProcessingTab = new QWidget;
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
        traceWidth
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
    form->setColumnStretch(2, 1);
    imageLayout->addLayout(form);

    imageLayout->addStretch();

    tabs->addTab(controlTab, "Control");
    tabs->addTab(imageProcessingTab, "Image Processing");
    controlLayout->addWidget(tabs, 1);
    rightColumn->addWidget(controlPanel);
    rightColumn->setStretchFactor(0, 1);
    rightColumn->setStretchFactor(1, 1);
    rightColumn->setSizes({1, 1});
    content->addWidget(rightColumn, 1);
    root->addLayout(content, 1);

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

