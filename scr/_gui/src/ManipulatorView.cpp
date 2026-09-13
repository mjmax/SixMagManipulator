#include "ManipulatorView.h"
#include "ChassisGeometry.h"

#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QMediaDevices>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QRadialGradient>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QUrl>
#include <QVideoFrame>

#include <algorithm>
#include <cmath>

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr int traceOverlaySize = 512;

QPointF unitVector(double angleRadians)
{
    return {std::cos(angleRadians), -std::sin(angleRadians)};
}

double normalizedAngle(double degrees)
{
    double result = std::fmod(degrees, 360.0);
    if (result > 180.0)
        result -= 360.0;
    if (result <= -180.0)
        result += 360.0;
    return result;
}
}

ManipulatorView::ManipulatorView(QWidget *parent)
    : QWidget(parent), m_videoSink(this)
{
    setObjectName("manipulatorView");
    setMinimumSize(620, 580);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setContextMenuPolicy(Qt::PreventContextMenu);

    m_traceOverlay = QImage(traceOverlaySize, traceOverlaySize,
                            QImage::Format_ARGB32_Premultiplied);
    m_traceOverlay.fill(Qt::transparent);

    m_clearTraceButton = new QPushButton(QStringLiteral("Clear\nTrace"), this);
    m_clearTraceButton->setObjectName("workspaceClearTraceButton");
    m_clearTraceButton->setFixedSize(62, 62);
    m_clearTraceButton->setFocusPolicy(Qt::NoFocus);
    m_clearTraceButton->setToolTip("Remove all recorded object-path points");
    m_clearTraceButton->setStyleSheet(R"(
        QPushButton#workspaceClearTraceButton {
            background: rgba(19, 28, 38, 232);
            color: #eef3f8;
            border: 1px solid #53677d;
            border-radius: 7px;
            font-size: 10px;
            font-weight: 700;
            padding: 3px;
        }
        QPushButton#workspaceClearTraceButton:hover {
            background: rgba(45, 65, 85, 242);
            border-color: #7d9bbc;
        }
        QPushButton#workspaceClearTraceButton:pressed {
            background: rgba(14, 22, 30, 245);
        }
    )");
    connect(m_clearTraceButton, &QPushButton::clicked,
            this, &ManipulatorView::clearTrace);

    m_viewControls = new QWidget(this);
    m_viewControls->setObjectName("workspaceViewControls");
    auto *viewControlsLayout = new QHBoxLayout(m_viewControls);
    viewControlsLayout->setContentsMargins(0, 0, 0, 0);
    viewControlsLayout->setSpacing(3);

    m_panLockButton = new QPushButton(QStringLiteral("🔒"), m_viewControls);
    auto *zoomInButton = new QPushButton(QStringLiteral("+"), m_viewControls);
    m_zoomEditor = new QSpinBox(m_viewControls);
    auto *zoomOutButton = new QPushButton(QStringLiteral("−"), m_viewControls);
    const QList<QPushButton *> viewButtons = {
        m_panLockButton, zoomInButton, zoomOutButton
    };
    for (QPushButton *button : viewButtons) {
        button->setFixedSize(28, 28);
        button->setFocusPolicy(Qt::NoFocus);
    }
    m_panLockButton->setCheckable(true);
    m_panLockButton->setToolTip(
        "Unlock to pan the camera image with the left mouse button");
    zoomInButton->setToolTip("Zoom in by 10%");
    zoomOutButton->setToolTip("Zoom out by 10%");

    m_zoomEditor->setRange(25, 400);
    m_zoomEditor->setValue(100);
    m_zoomEditor->setSingleStep(10);
    m_zoomEditor->setSuffix("%");
    m_zoomEditor->setAlignment(Qt::AlignCenter);
    m_zoomEditor->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_zoomEditor->setKeyboardTracking(false);
    m_zoomEditor->setFixedSize(62, 28);
    m_zoomEditor->setToolTip("Visible camera-image zoom");
    zoomInButton->setEnabled(false);
    m_zoomEditor->setEnabled(false);
    zoomOutButton->setEnabled(false);

    viewControlsLayout->addWidget(m_panLockButton);
    viewControlsLayout->addWidget(zoomInButton);
    viewControlsLayout->addWidget(m_zoomEditor);
    viewControlsLayout->addWidget(zoomOutButton);
    m_viewControls->adjustSize();
    m_viewControls->setStyleSheet(R"(
        QWidget#workspaceViewControls QPushButton,
        QWidget#workspaceViewControls QSpinBox {
            background: rgba(19, 28, 38, 238);
            color: #eef3f8;
            border: 1px solid #53677d;
            border-radius: 5px;
            font-size: 12px;
            font-weight: 700;
        }
        QWidget#workspaceViewControls QPushButton:hover {
            background: rgba(45, 65, 85, 245);
            border-color: #7d9bbc;
        }
        QWidget#workspaceViewControls QPushButton:pressed,
        QWidget#workspaceViewControls QPushButton:checked {
            background: #237a50;
            border-color: #58d99a;
        }
        QWidget#workspaceViewControls QSpinBox {
            padding: 0 4px;
            selection-background-color: #315a78;
        }
    )");

    connect(m_panLockButton, &QPushButton::toggled,
            this, [this, zoomInButton, zoomOutButton](bool unlocked) {
        m_panUnlocked = unlocked;
        m_panningImage = false;
        zoomInButton->setEnabled(unlocked);
        m_zoomEditor->setEnabled(unlocked);
        zoomOutButton->setEnabled(unlocked);
        m_panLockButton->setText(
            unlocked ? QStringLiteral("🔓") : QStringLiteral("🔒"));
        unsetCursor();
    });
    connect(zoomInButton, &QPushButton::clicked,
            this, [this] {
        m_zoomEditor->setValue(m_zoomEditor->value() + 10);
    });
    connect(zoomOutButton, &QPushButton::clicked,
            this, [this] {
        m_zoomEditor->setValue(m_zoomEditor->value() - 10);
    });
    connect(m_zoomEditor, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int value) {
        m_imageZoomPercent = value;
        clampImagePan();
        saveCameraViewSettings();
        update();
    });

    loadCameraViewSettings(QStringLiteral("webcam:default"));
    m_cameraClock.start();
    m_imageTracker = new ImageTracker(this);
    connect(m_imageTracker, &ImageTracker::visualizationResultReady,
            this, &ManipulatorView::receiveTrackingVisualization,
            Qt::QueuedConnection);
    m_imageTracker->start(QThread::HighPriority);

    connect(&m_videoSink, &QVideoSink::videoFrameChanged,
            this, &ManipulatorView::receiveVideoFrame);
    startDefaultCamera();
}

ManipulatorView::~ManipulatorView()
{
    shutdown();
}

QSize ManipulatorView::sizeHint() const
{
    return {760, 680};
}

void ManipulatorView::setMagnetAngle(int magnetIndex, double angleDegrees)
{
    if (magnetIndex < 0 || magnetIndex >= static_cast<int>(m_magnetAngles.size()))
        return;
    m_magnetAngles[static_cast<std::size_t>(magnetIndex)] = normalizedAngle(angleDegrees);
    update();
}

void ManipulatorView::resetMagnetAngles()
{
    m_magnetAngles.fill(0.0);
    update();
}

void ManipulatorView::setServoAngleLimits(double lowerDegrees, double upperDegrees)
{
    if (!std::isfinite(lowerDegrees) || !std::isfinite(upperDegrees)
        || lowerDegrees >= upperDegrees) {
        return;
    }
    if (m_lowerServoAngleLimit == lowerDegrees
        && m_upperServoAngleLimit == upperDegrees) {
        return;
    }
    m_lowerServoAngleLimit = lowerDegrees;
    m_upperServoAngleLimit = upperDegrees;
    emit servoAngleLimitsChanged(lowerDegrees, upperDegrees);
    update();
}

void ManipulatorView::setDetectionThreshold(int threshold)
{
    ImageProcessingSettings settings = m_imageTracker->settings();
    settings.threshold = threshold;
    applyImageProcessingSettings(settings);
}

void ManipulatorView::setDetectionMinimumArea(int pixels)
{
    ImageProcessingSettings settings = m_imageTracker->settings();
    settings.minimumAreaPixels = pixels;
    applyImageProcessingSettings(settings);
}

void ManipulatorView::setDetectionMaximumArea(int pixels)
{
    ImageProcessingSettings settings = m_imageTracker->settings();
    settings.maximumAreaPixels = pixels;
    applyImageProcessingSettings(settings);
}

void ManipulatorView::setMaximumAreaPreview(bool visible, int areaPixels)
{
    const int validatedArea = std::max(1, areaPixels);
    if (m_maximumAreaPreviewVisible == visible
        && m_maximumAreaPreviewPixels == validatedArea) {
        return;
    }
    m_maximumAreaPreviewVisible = visible;
    m_maximumAreaPreviewPixels = validatedArea;
    update();
}

void ManipulatorView::setDetectionMinimumCircularity(double circularity)
{
    ImageProcessingSettings settings = m_imageTracker->settings();
    settings.minimumCircularity = circularity;
    applyImageProcessingSettings(settings);
}

void ManipulatorView::setMinimumCircularityPreview(bool visible,
                                                   double threshold)
{
    const double validatedThreshold = std::clamp(threshold, 0.0, 1.0);
    if (m_minimumCircularityPreviewVisible == visible
        && qFuzzyCompare(m_minimumCircularityPreviewThreshold,
                         validatedThreshold)) {
        return;
    }
    m_minimumCircularityPreviewVisible = visible;
    m_minimumCircularityPreviewThreshold = validatedThreshold;
    update();
}

void ManipulatorView::setVisualizationRate(int framesPerSecond)
{
    ImageProcessingSettings settings = m_imageTracker->settings();
    settings.visualizationRateHz = framesPerSecond;
    applyImageProcessingSettings(settings);
}

void ManipulatorView::setTraceColor(const QColor &color)
{
    if (!color.isValid() || color == m_traceColor)
        return;
    m_traceColor = color;

    // Recolor the existing alpha mask without retaining historical points.
    // This operation occurs only when the user changes the trace color.
    if (!m_traceOverlay.isNull()) {
        QPainter overlayPainter(&m_traceOverlay);
        overlayPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        overlayPainter.fillRect(m_traceOverlay.rect(), m_traceColor);
    }
    update();
}

void ManipulatorView::setTraceWidth(double pixels)
{
    if (!std::isfinite(pixels))
        return;
    m_traceWidth = std::clamp(pixels, 0.5, 12.0);
    update();
}

void ManipulatorView::setMaximumTraceDots(int maximumDots)
{
    m_maximumTraceDots = std::clamp(maximumDots, 0, 64);
}

void ManipulatorView::setTraceEnabled(bool enabled)
{
    if (m_traceEnabled == enabled)
        return;
    m_traceEnabled = enabled;
    m_hasPreviousTracePosition = false;
}

void ManipulatorView::clearTrace()
{
    m_traceOverlay.fill(Qt::transparent);
    m_hasPreviousTracePosition = false;
    update();
}

void ManipulatorView::applyImageProcessingSettings(
    const ImageProcessingSettings &settings)
{
    m_imageTracker->setSettings(settings);
    m_visualizationRateHz.store(
        m_imageTracker->settings().visualizationRateHz,
        std::memory_order_release);
}

void ManipulatorView::startDefaultCamera()
{
    const QCameraDevice device = QMediaDevices::defaultVideoInput();
    if (device.isNull())
        return;
    m_camera = new QCamera(device, this);
    QCameraFormat fastestFormat;
    for (const QCameraFormat &format : device.videoFormats()) {
        const bool faster = format.maxFrameRate() > fastestFormat.maxFrameRate();
        const bool sameRateHigherResolution =
            qFuzzyCompare(format.maxFrameRate(), fastestFormat.maxFrameRate())
            && format.resolution().width() * format.resolution().height()
                > fastestFormat.resolution().width() * fastestFormat.resolution().height();
        if (fastestFormat.isNull() || faster || sameRateHigherResolution)
            fastestFormat = format;
    }
    if (!fastestFormat.isNull())
        m_camera->setCameraFormat(fastestFormat);
    m_captureSession.setCamera(m_camera);
    m_captureSession.setVideoSink(&m_videoSink);
    m_camera->start();
}

QVector<VimbaCameraDescriptor> ManipulatorView::availableVimbaCameras() const
{
    return VimbaCameraSource::availableCameras();
}

void ManipulatorView::stopCameraSource()
{
    if (m_camera) {
        m_camera->stop();
        m_captureSession.setCamera(nullptr);
        delete m_camera;
        m_camera = nullptr;
    }
    if (m_vimbaCamera) {
        m_vimbaCamera->stop();
        delete m_vimbaCamera;
        m_vimbaCamera = nullptr;
    }
}

void ManipulatorView::setCameraSource(const QString &sourceId)
{
    if (m_shutdownComplete)
        return;

    saveCameraViewSettings();
    stopCameraSource();
    m_latestDisplayFrame = {};
    loadCameraViewSettings(sourceId);
    m_objectDetected = false;
    m_hasObjectMeasurement = false;
    m_lastVimbaDisplayNanoseconds.store(0, std::memory_order_release);
    update();

    if (!sourceId.startsWith(QStringLiteral("vimba:"))) {
        startDefaultCamera();
        return;
    }

    m_vimbaCamera = new VimbaCameraSource(sourceId.mid(6), this);
    connect(m_vimbaCamera, &VimbaCameraSource::frameReady,
            this, &ManipulatorView::receiveVimbaFrame,
            Qt::DirectConnection);
    connect(m_vimbaCamera, &VimbaCameraSource::controlsReady,
            this, &ManipulatorView::cameraControlsReady);
    connect(m_vimbaCamera, &VimbaCameraSource::sourceError,
            this, &ManipulatorView::cameraSourceError);
    m_vimbaCamera->start(QThread::TimeCriticalPriority);
}

void ManipulatorView::shutdown()
{
    if (m_shutdownComplete)
        return;
    m_shutdownComplete = true;

    saveCameraViewSettings();
    disconnect(&m_videoSink, &QVideoSink::videoFrameChanged,
               this, &ManipulatorView::receiveVideoFrame);
    stopCameraSource();
    if (m_imageTracker)
        m_imageTracker->stop();
}

void ManipulatorView::setCameraExposure(double value)
{
    if (m_vimbaCamera)
        m_vimbaCamera->setExposureTime(value);
}

void ManipulatorView::setCameraGain(double value)
{
    if (m_vimbaCamera)
        m_vimbaCamera->setGain(value);
}

void ManipulatorView::setCameraBlackLevel(double value)
{
    if (m_vimbaCamera)
        m_vimbaCamera->setBlackLevel(value);
}

void ManipulatorView::setCameraGamma(double value)
{
    if (m_vimbaCamera)
        m_vimbaCamera->setGamma(value);
}

void ManipulatorView::setCameraRotation(int degrees)
{
    const int validatedDegrees = std::clamp(degrees, -360, 360);
    if (m_cameraRotationDegrees == validatedDegrees)
        return;
    m_cameraRotationDegrees = validatedDegrees;
    update();
}

void ManipulatorView::receiveVideoFrame(const QVideoFrame &frame)
{
    const QImage image = frame.toImage();
    if (image.isNull())
        return;

    // Every frame reaches the latest-frame detector. Only a low-rate frame is
    // retained by the GUI, and QImage implicit sharing avoids a second pixel copy.
    m_imageTracker->submitFrame(image, m_cameraClock.nsecsElapsed());

    const int displayInterval = std::max(
        1, 1000 / m_visualizationRateHz.load(std::memory_order_acquire));
    if (!m_displayFrameClock.isValid()
        || m_displayFrameClock.elapsed() >= displayInterval) {
        m_latestDisplayFrame = image;
        clampImagePan();
        m_displayFrameClock.restart();
        update();
    }
}

void ManipulatorView::receiveVimbaFrame(const QImage &image,
                                        qint64 timestampNanoseconds)
{
    m_imageTracker->submitFrame(image, timestampNanoseconds);

    const qint64 interval = 1000000000LL / std::max(
        1, m_visualizationRateHz.load(std::memory_order_acquire));
    qint64 previous = m_lastVimbaDisplayNanoseconds.load(std::memory_order_acquire);
    if (timestampNanoseconds - previous < interval
        || !m_lastVimbaDisplayNanoseconds.compare_exchange_strong(
            previous, timestampNanoseconds, std::memory_order_acq_rel)) {
        return;
    }
    QMetaObject::invokeMethod(this, [this, image] {
        m_latestDisplayFrame = image;
        clampImagePan();
        update();
    }, Qt::QueuedConnection);
}

void ManipulatorView::receiveTrackingVisualization(const TrackingResult &result)
{
    if (result.objects.isEmpty()) {
        m_objectDetected = false;
        m_hasPreviousTracePosition = false;
        emit trackingStatusChanged(
            false, {}, result.detectionMilliseconds,
            result.processingFramesPerSecond);
        update();
        return;
    }

    const TrackedObject &object = result.objects.first();
    m_objectDetected = true;
    m_objectPosition = object.normalizedPosition;
    m_objectRadius = object.normalizedRadius;
    m_objectCircularity = object.circularity;
    m_hasObjectMeasurement = true;
    if (m_traceEnabled)
        appendTraceDot(m_objectPosition);
    emit trackingStatusChanged(
        true, m_objectPosition, result.detectionMilliseconds,
        result.processingFramesPerSecond);
    update();
}

void ManipulatorView::appendTraceDot(const QPointF &normalizedPosition)
{
    if (m_traceOverlay.isNull())
        return;

    const QPointF overlayPoint(
        std::clamp(normalizedPosition.x(), 0.0, 1.0)
            * (m_traceOverlay.width() - 1),
        std::clamp(normalizedPosition.y(), 0.0, 1.0)
            * (m_traceOverlay.height() - 1));

    // Trace width is specified in visible GUI pixels. Convert it to overlay
    // pixels so dots retain the selected size when the overlay is composited.
    const QRectF content = rect().adjusted(18, 18, -18, -18);
    const double designSize = std::min(
        std::min(content.width(), content.height()), 680.0);
    const double visibleWorkspaceDiameter = std::max(1.0, designSize * 0.57);
    const double overlayDiameter = std::max(
        1.0, m_traceWidth * m_traceOverlay.width()
            / visibleWorkspaceDiameter);

    int steps = 1;
    QPointF previousOverlayPoint;
    if (m_hasPreviousTracePosition) {
        previousOverlayPoint = QPointF(
            std::clamp(m_previousTracePosition.x(), 0.0, 1.0)
                * (m_traceOverlay.width() - 1),
            std::clamp(m_previousTracePosition.y(), 0.0, 1.0)
                * (m_traceOverlay.height() - 1));

        const double refreshScale = std::clamp(
            static_cast<double>(
                m_visualizationRateHz.load(std::memory_order_acquire)) / 30.0,
            0.5, 2.0);
        const double targetSpacing = std::max(
            1.0, overlayDiameter * refreshScale);
        const QPointF delta = overlayPoint - previousOverlayPoint;
        const double distance = std::hypot(delta.x(), delta.y());

        // The user-selected cap bounds the number of estimated dots. A value
        // of zero leaves only the newly measured position.
        steps = std::clamp(
            static_cast<int>(std::ceil(distance / targetSpacing)),
            1, m_maximumTraceDots + 1);
    }

    QPainter overlayPainter(&m_traceOverlay);
    overlayPainter.setRenderHint(QPainter::Antialiasing, true);
    overlayPainter.setPen(Qt::NoPen);
    overlayPainter.setBrush(m_traceColor);
    for (int step = 1; step <= steps; ++step) {
        const double fraction = static_cast<double>(step) / steps;
        const QPointF dotPosition = m_hasPreviousTracePosition
            ? previousOverlayPoint
                + (overlayPoint - previousOverlayPoint) * fraction
            : overlayPoint;
        overlayPainter.drawEllipse(dotPosition,
                                   overlayDiameter * 0.5,
                                   overlayDiameter * 0.5);
    }

    m_previousTracePosition = normalizedPosition;
    m_hasPreviousTracePosition = true;
}

void ManipulatorView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#141b24"));
    drawManipulator(painter, rect().adjusted(18, 18, -18, -18));
}

QRectF ManipulatorView::workspaceCircleRect() const
{
    const QRectF area = rect().adjusted(18, 18, -18, -18);
    const double designSize = std::min(
        std::min(area.width(), area.height()), 680.0);
    const QPointF center(area.left() + designSize * 0.50,
                         area.top() + designSize * 0.50);
    const double radius = designSize * 0.285;
    return QRectF(center.x() - radius, center.y() - radius,
                  radius * 2.0, radius * 2.0);
}

void ManipulatorView::loadCameraViewSettings(const QString &sourceId)
{
    m_currentCameraSourceId = sourceId;
    const QString sourceKey = QString::fromLatin1(
        QUrl::toPercentEncoding(sourceId));
    QSettings settings;
    settings.beginGroup(QStringLiteral("cameraView/%1").arg(sourceKey));
    const int zoom = std::clamp(
        settings.value(QStringLiteral("zoomPercent"), 100).toInt(),
        25, 400);
    const double panX =
        settings.value(QStringLiteral("panX"), 0.0).toDouble();
    const double panY =
        settings.value(QStringLiteral("panY"), 0.0).toDouble();
    settings.endGroup();

    m_imageZoomPercent = zoom;
    m_imagePanNormalized = {
        std::isfinite(panX) ? panX : 0.0,
        std::isfinite(panY) ? panY : 0.0
    };
    if (m_zoomEditor) {
        QSignalBlocker blocker(m_zoomEditor);
        m_zoomEditor->setValue(zoom);
    }
}

void ManipulatorView::saveCameraViewSettings() const
{
    if (m_currentCameraSourceId.isEmpty())
        return;

    const QString sourceKey = QString::fromLatin1(
        QUrl::toPercentEncoding(m_currentCameraSourceId));
    QSettings settings;
    settings.beginGroup(QStringLiteral("cameraView/%1").arg(sourceKey));
    settings.setValue(QStringLiteral("zoomPercent"), m_imageZoomPercent);
    settings.setValue(QStringLiteral("panX"), m_imagePanNormalized.x());
    settings.setValue(QStringLiteral("panY"), m_imagePanNormalized.y());
    settings.endGroup();
}

void ManipulatorView::clampImagePan()
{
    if (m_latestDisplayFrame.isNull())
        return;

    const QRectF circle = workspaceCircleRect();
    const double diameter = circle.width();
    if (diameter <= 0.0)
        return;

    const double zoom = m_imageZoomPercent / 100.0;
    const double sourceMinimum = std::min(
        m_latestDisplayFrame.width(), m_latestDisplayFrame.height());
    const double scale = diameter / sourceMinimum * zoom;
    const double imageWidth = m_latestDisplayFrame.width() * scale;
    const double imageHeight = m_latestDisplayFrame.height() * scale;
    const double normalization = diameter * zoom;
    const double maximumX = std::max(0.0, (imageWidth - diameter) * 0.5)
        / normalization;
    const double maximumY = std::max(0.0, (imageHeight - diameter) * 0.5)
        / normalization;
    m_imagePanNormalized.setX(std::clamp(
        m_imagePanNormalized.x(), -maximumX, maximumX));
    m_imagePanNormalized.setY(std::clamp(
        m_imagePanNormalized.y(), -maximumY, maximumY));
}

void ManipulatorView::mousePressEvent(QMouseEvent *event)
{
    if (m_panUnlocked && event->button() == Qt::LeftButton
        && workspaceCircleRect().contains(event->position())) {
        m_panningImage = true;
        m_lastPanMousePosition = event->position();
        grabMouse(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ManipulatorView::mouseMoveEvent(QMouseEvent *event)
{
    const QRectF circle = workspaceCircleRect();
    if (m_panUnlocked && !m_panningImage
        && (event->buttons() & Qt::LeftButton)
        && circle.contains(event->position())) {
        m_panningImage = true;
        m_lastPanMousePosition = event->position();
        grabMouse(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (m_panningImage) {
        if (!(event->buttons() & Qt::LeftButton)) {
            m_panningImage = false;
            releaseMouse();
            unsetCursor();
            event->accept();
            return;
        }
        const QPointF delta = event->position() - m_lastPanMousePosition;
        m_lastPanMousePosition = event->position();
        const double angle = m_cameraRotationDegrees * pi / 180.0;
        const QPointF unrotatedDelta(
            std::cos(angle) * delta.x() - std::sin(angle) * delta.y(),
            std::sin(angle) * delta.x() + std::cos(angle) * delta.y());
        const double normalization = std::max(
            1.0, circle.width() * m_imageZoomPercent / 100.0);
        m_imagePanNormalized += unrotatedDelta / normalization;
        clampImagePan();
        update();
        event->accept();
        return;
    }

    unsetCursor();
    QWidget::mouseMoveEvent(event);
}

void ManipulatorView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panningImage && event->button() == Qt::LeftButton) {
        m_panningImage = false;
        releaseMouse();
        unsetCursor();
        saveCameraViewSettings();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ManipulatorView::leaveEvent(QEvent *event)
{
    if (!m_panningImage)
        unsetCursor();
    QWidget::leaveEvent(event);
}

void ManipulatorView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    constexpr int margin = 12;
    m_clearTraceButton->move(
        std::max(margin, width() - m_clearTraceButton->width() - margin),
        margin);
    m_clearTraceButton->raise();
    m_viewControls->move(
        std::max(margin, width() - m_viewControls->width() - margin),
        std::max(margin, height() - m_viewControls->height() - margin));
    m_viewControls->raise();
    clampImagePan();
}

void ManipulatorView::drawManipulator(QPainter &painter, const QRectF &area)
{
    const double available = std::min(area.width(), area.height());
    const double designSize = std::min(available, 680.0);
    const QPointF center(area.left() + designSize * 0.50,
                         area.top() + designSize * 0.50);
    const double workspaceRadius = designSize * 0.285;
    const double magnetRadius = designSize * 0.072;
    const double orbitRadius = workspaceRadius + magnetRadius * 1.65;

    const double contourPeak = orbitRadius + magnetRadius
        + ChassisGeometry::magnetClearancePx;
    const double contourValley = workspaceRadius
        + magnetRadius * ChassisGeometry::workspaceValleyScale;
    constexpr int contourSamples = 360;
    QPainterPath chassis;
    for (int sample = 0; sample < contourSamples; ++sample) {
        const double theta = 2.0 * pi * static_cast<double>(sample)
            / static_cast<double>(contourSamples);
        const double contourRadius = contourValley
            + (contourPeak - contourValley)
                * ChassisGeometry::lobeShape(theta);
        const QPointF point = center + unitVector(theta) * contourRadius;
        if (sample == 0)
            chassis.moveTo(point);
        else
            chassis.lineTo(point);
    }
    chassis.closeSubpath();

    painter.save();
    QRadialGradient chassisGradient(
        center - QPointF(workspaceRadius * 0.35, workspaceRadius * 0.42),
        orbitRadius + magnetRadius * 1.3);
    chassisGradient.setColorAt(0.0, QColor("#4a5866"));
    chassisGradient.setColorAt(0.48, QColor("#2c3743"));
    chassisGradient.setColorAt(1.0, QColor("#171f28"));
    painter.setBrush(chassisGradient);
    painter.setPen(QPen(QColor("#080c11"), 6.0, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(chassis);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(132, 148, 164, 115), 1.35,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(chassis);
    painter.restore();

    drawWorkspace(painter, center, workspaceRadius);
    for (int index = 0; index < 6; ++index) {
        const double positionAngle = index * pi / 3.0;
        const QPointF position = center + unitVector(positionAngle) * orbitRadius;
        drawMagnet(painter, position, center, magnetRadius,
                   m_magnetAngles[static_cast<std::size_t>(index)], index);
    }

    painter.setPen(QColor("#8997a8"));
    painter.setFont(QFont("Segoe UI", 9, QFont::DemiBold));
    QString cameraStatus = m_latestDisplayFrame.isNull()
        ? QStringLiteral("CAMERA OFFLINE")
        : (m_objectDetected
            ? QStringLiteral("OBJECT TRACKING")
            : QStringLiteral("SEARCHING WORKSPACE"));
    painter.drawText(QRectF(area.left(), area.bottom() - 22, designSize, 20),
                     Qt::AlignCenter,
                     cameraStatus + QStringLiteral("  •  MAGNET ANGLES 0°"));
}

void ManipulatorView::drawWorkspace(QPainter &painter,
                                    const QPointF &center,
                                    double radius)
{
    const QRectF circle(center.x() - radius, center.y() - radius,
                        radius * 2.0, radius * 2.0);
    painter.save();
    painter.setPen(QPen(QColor("#0a0e13"), 13));
    painter.setBrush(QColor("#05080c"));
    painter.drawEllipse(circle.adjusted(-6, -6, 6, 6));

    QPainterPath circularClip;
    circularClip.addEllipse(circle);
    painter.setClipPath(circularClip);
    painter.translate(center);
    painter.rotate(-m_cameraRotationDegrees);
    painter.translate(-center);
    QRectF cameraSquareTarget = circle;
    if (!m_latestDisplayFrame.isNull()) {
        const int sourceSide = std::min(
            m_latestDisplayFrame.width(), m_latestDisplayFrame.height());
        const double zoom = m_imageZoomPercent / 100.0;
        const double scale = circle.width() / sourceSide * zoom;
        const QSizeF imageSize(
            m_latestDisplayFrame.width() * scale,
            m_latestDisplayFrame.height() * scale);
        const QPointF panOffset = m_imagePanNormalized
            * (circle.width() * zoom);
        const QRectF imageTarget(
            center.x() - imageSize.width() * 0.5 + panOffset.x(),
            center.y() - imageSize.height() * 0.5 + panOffset.y(),
            imageSize.width(), imageSize.height());
        const double cropX =
            (m_latestDisplayFrame.width() - sourceSide) * 0.5;
        const double cropY =
            (m_latestDisplayFrame.height() - sourceSide) * 0.5;
        cameraSquareTarget = QRectF(
            imageTarget.left() + cropX * scale,
            imageTarget.top() + cropY * scale,
            sourceSide * scale,
            sourceSide * scale);

        painter.fillRect(circle, QColor("#05080c"));
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(imageTarget, m_latestDisplayFrame);
    } else {
        QRadialGradient glass(center - QPointF(radius * 0.25, radius * 0.3),
                              radius * 1.25);
        glass.setColorAt(0.0, QColor("#263542"));
        glass.setColorAt(0.55, QColor("#111a23"));
        glass.setColorAt(1.0, QColor("#05090e"));
        painter.fillRect(circle, glass);
    }

    if (!m_traceOverlay.isNull())
        painter.drawImage(cameraSquareTarget, m_traceOverlay);

    const bool showCircularity = m_minimumCircularityPreviewVisible
        && m_hasObjectMeasurement;
    if (m_objectDetected || showCircularity) {
        const QPointF marker(
            cameraSquareTarget.left()
                + m_objectPosition.x() * cameraSquareTarget.width(),
            cameraSquareTarget.top()
                + m_objectPosition.y() * cameraSquareTarget.height());
        if (m_maximumAreaPreviewVisible
            && !m_latestDisplayFrame.isNull()) {
            const int sourceSide = std::min(
                m_latestDisplayFrame.width(),
                m_latestDisplayFrame.height());
            const double previewRadius = std::sqrt(
                static_cast<double>(m_maximumAreaPreviewPixels) / pi)
                * cameraSquareTarget.width()
                / std::max(1, sourceSide);
            painter.setPen(QPen(QColor(255, 76, 84, 205), 1.5));
            painter.setBrush(QColor(236, 45, 58, 82));
            painter.drawEllipse(marker, previewRadius, previewRadius);
        }

        if (m_objectDetected) {
            const double markerRadius = std::max(
                4.0, m_objectRadius * cameraSquareTarget.width());
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor("#58f0a4"), 2.0));
            painter.drawEllipse(marker, markerRadius + 3.0, markerRadius + 3.0);
            painter.setBrush(QColor(88, 240, 164, 190));
            painter.setPen(QPen(QColor("#f4fff9"), 1.0));
            painter.drawEllipse(marker, 2.5, 2.5);
        }

        if (showCircularity) {
            const bool passes = m_objectCircularity
                >= m_minimumCircularityPreviewThreshold;
            const QColor statusColor = passes
                ? QColor("#58f0a4") : QColor("#ff4c54");
            const double markerRadius = std::max(
                4.0, m_objectRadius * cameraSquareTarget.width());

            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(statusColor, 3.0));
            painter.drawEllipse(marker, markerRadius + 7.0,
                                markerRadius + 7.0);

            const QString text = QStringLiteral("Circularity %1")
                .arg(m_objectCircularity, 0, 'f', 2);
            painter.setFont(QFont("Segoe UI", 9, QFont::DemiBold));
            const QFontMetricsF metrics(painter.font());
            QRectF labelRect = metrics.boundingRect(text)
                .adjusted(-7.0, -4.0, 7.0, 4.0);
            labelRect.moveCenter(QPointF(
                marker.x(), marker.y() - markerRadius - 20.0));
            painter.setPen(QPen(statusColor, 1.0));
            painter.setBrush(QColor(7, 12, 18, 215));
            painter.drawRoundedRect(labelRect, 5.0, 5.0);
            painter.setPen(statusColor);
            painter.drawText(labelRect, Qt::AlignCenter, text);
        }
    }
    painter.restore();

    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor("#6e7d8c"), 2));
    painter.drawEllipse(circle);
    painter.setPen(QPen(QColor(255, 255, 255, 35), 1));
    painter.drawArc(circle.adjusted(5, 5, -5, -5), 30 * 16, 120 * 16);
    painter.restore();
}

void ManipulatorView::drawMagnet(QPainter &painter,
                                 const QPointF &position,
                                 const QPointF &workspaceCenter,
                                 double radius,
                                 double angleDegrees,
                                 int index)
{
    painter.save();
    painter.translate(position);
    QRadialGradient mountGradient(QPointF(-radius * 0.35, -radius * 0.4),
                                  radius * 1.5);
    mountGradient.setColorAt(0.0, QColor("#667382"));
    mountGradient.setColorAt(0.42, QColor("#34404d"));
    mountGradient.setColorAt(1.0, QColor("#111820"));
    painter.setBrush(mountGradient);
    painter.setPen(QPen(QColor("#0a0e13"), 3));
    painter.drawEllipse(QPointF(0, 0), radius + 8, radius + 8);

    painter.setBrush(QColor("#0c1117"));
    painter.setPen(QPen(QColor("#7e8a96"), 1.5));
    painter.drawEllipse(QPointF(0, 0), radius + 2, radius + 2);

    const QPointF inward = workspaceCenter - position;
    const double baseScreenDegrees = std::atan2(inward.y(), inward.x()) * 180.0 / pi;
    painter.rotate(baseScreenDegrees - angleDegrees);
    const double faceRadius = radius - 3.0;
    QPainterPath faceClip;
    faceClip.addEllipse(QPointF(0, 0), faceRadius, faceRadius);
    painter.setClipPath(faceClip);

    QLinearGradient southGradient(0, -faceRadius, 0, faceRadius);
    southGradient.setColorAt(0.0, QColor("#748fae"));
    southGradient.setColorAt(0.48, QColor("#31577e"));
    southGradient.setColorAt(1.0, QColor("#173654"));
    painter.fillRect(QRectF(-faceRadius, -faceRadius,
                            faceRadius * 2.0, faceRadius * 2.0), southGradient);

    QLinearGradient northGradient(0, -faceRadius, 0, faceRadius);
    northGradient.setColorAt(0.0, QColor("#d66d68"));
    northGradient.setColorAt(0.48, QColor("#a83e3b"));
    northGradient.setColorAt(1.0, QColor("#6f2425"));
    painter.fillRect(QRectF(0, -faceRadius,
                            faceRadius, faceRadius * 2.0), northGradient);

    QLinearGradient sheen(-faceRadius, -faceRadius, faceRadius, faceRadius);
    sheen.setColorAt(0.0, QColor(255, 255, 255, 90));
    sheen.setColorAt(0.32, QColor(255, 255, 255, 12));
    sheen.setColorAt(0.68, QColor(0, 0, 0, 18));
    sheen.setColorAt(1.0, QColor(0, 0, 0, 65));
    painter.fillRect(QRectF(-faceRadius, -faceRadius,
                            faceRadius * 2.0, faceRadius * 2.0), sheen);

    painter.setClipping(false);
    painter.setPen(QPen(QColor("#111820"), 2.2));
    painter.drawLine(QPointF(0, -faceRadius), QPointF(0, faceRadius));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor("#b8c1ca"), 1.2));
    painter.drawEllipse(QPointF(0, 0), faceRadius, faceRadius);
    painter.restore();

    const bool saturated = angleDegrees <= m_lowerServoAngleLimit
        || angleDegrees >= m_upperServoAngleLimit;
    const QString angleText = QStringLiteral("M%1  %2°")
        .arg(index + 1)
        .arg(angleDegrees, 0, 'f', 1);

    QPointF labelCenter;
    if (index == 1 || index == 2) {
        labelCenter = position + QPointF(0.0, -(radius + 36.0));
    } else if (index == 0) {
        labelCenter = position + QPointF(radius * 0.65, radius + 30.0);
    } else if (index == 3) {
        labelCenter = position + QPointF(-radius * 0.65, radius + 30.0);
    } else {
        labelCenter = position + QPointF(0.0, radius + 36.0);
    }

    painter.save();
    painter.setFont(QFont("Segoe UI", 8, QFont::DemiBold));
    const QRectF labelRect(labelCenter.x() - radius * 1.05,
                           labelCenter.y() - 10.0,
                           radius * 2.10,
                           20.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(7, 11, 16, 210));
    painter.drawRoundedRect(labelRect, 5.0, 5.0);
    painter.setPen(saturated ? QColor("#ff5964") : QColor("#eef3f8"));
    painter.drawText(labelRect, Qt::AlignCenter, angleText);
    painter.restore();
}
