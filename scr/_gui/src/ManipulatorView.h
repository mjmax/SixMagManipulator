#pragma once

#include "ImageTracker.h"
#include "VimbaCameraSource.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QMediaCaptureSession>
#include <QPointF>
#include <QVideoSink>
#include <QWidget>

#include <array>
#include <atomic>

class QCamera;
class QEvent;
class QMouseEvent;
class QPainter;
class QPushButton;
class QResizeEvent;
class QSpinBox;

class ManipulatorView final : public QWidget
{
    Q_OBJECT

public:
    explicit ManipulatorView(QWidget *parent = nullptr);
    ~ManipulatorView() override;
    QSize sizeHint() const override;

    double lowerServoAngleLimit() const { return m_lowerServoAngleLimit; }
    double upperServoAngleLimit() const { return m_upperServoAngleLimit; }

    // The future control loop can either read latestResult() without waiting
    // for the GUI or connect directly to fastResultReady using DirectConnection.
    ImageTracker *imageTracker() const { return m_imageTracker; }
    QVector<VimbaCameraDescriptor> availableVimbaCameras() const;

public slots:
    void setMagnetAngle(int magnetIndex, double angleDegrees);
    void resetMagnetAngles();
    void setServoAngleLimits(double lowerDegrees, double upperDegrees);

    void setDetectionThreshold(int threshold);
    void setDetectionMinimumArea(int pixels);
    void setDetectionMaximumArea(int pixels);
    void setMaximumAreaPreview(bool visible, int areaPixels);
    void setDetectionMinimumCircularity(double circularity);
    void setMinimumCircularityPreview(bool visible, double threshold);
    void setVisualizationRate(int framesPerSecond);
    void setTraceColor(const QColor &color);
    void setTraceWidth(double pixels);
    void setMaximumTraceDots(int maximumDots);
    void setTraceEnabled(bool enabled);
    void clearTrace();
    void setCameraSource(const QString &sourceId);
    void setCameraExposure(double value);
    void setCameraGain(double value);
    void setCameraBlackLevel(double value);
    void setCameraGamma(double value);
    void setCameraRotation(int degrees);
    void shutdown();

signals:
    void servoAngleLimitsChanged(double lowerDegrees, double upperDegrees);
    void trackingStatusChanged(
        bool detected,
        const QPointF &normalizedPosition,
        double detectionMilliseconds,
        double processingFramesPerSecond);
    void cameraControlsReady(const VimbaFeatureState &exposure,
                             const VimbaFeatureState &gain,
                             const VimbaFeatureState &blackLevel,
                             const VimbaFeatureState &gamma);
    void cameraSourceError(const QString &message);

protected:
    void leaveEvent(QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void receiveVideoFrame(const QVideoFrame &frame);
    void receiveVimbaFrame(const QImage &image, qint64 timestampNanoseconds);
    void receiveTrackingVisualization(const TrackingResult &result);

private:
    void startDefaultCamera();
    void stopCameraSource();
    void applyImageProcessingSettings(const ImageProcessingSettings &settings);
    void appendTraceDot(const QPointF &normalizedPosition);
    void clampImagePan();
    void loadCameraViewSettings(const QString &sourceId);
    void saveCameraViewSettings() const;
    QRectF workspaceCircleRect() const;
    void drawManipulator(QPainter &painter, const QRectF &area);
    void drawWorkspace(QPainter &painter, const QPointF &center, double radius);
    void drawMagnet(
        QPainter &painter,
        const QPointF &position,
        const QPointF &workspaceCenter,
        double radius,
        double angleDegrees,
        int index);

    std::array<double, 6> m_magnetAngles{};
    double m_lowerServoAngleLimit = -150.0;
    double m_upperServoAngleLimit = 150.0;
    QImage m_latestDisplayFrame;
    QCamera *m_camera = nullptr;
    VimbaCameraSource *m_vimbaCamera = nullptr;
    QMediaCaptureSession m_captureSession;
    QVideoSink m_videoSink;
    ImageTracker *m_imageTracker = nullptr;
    QPushButton *m_clearTraceButton = nullptr;
    QWidget *m_viewControls = nullptr;
    QPushButton *m_panLockButton = nullptr;
    QSpinBox *m_zoomEditor = nullptr;
    QElapsedTimer m_cameraClock;
    QElapsedTimer m_displayFrameClock;
    std::atomic_int m_visualizationRateHz{15};
    std::atomic<qint64> m_lastVimbaDisplayNanoseconds{0};

    QImage m_traceOverlay;
    QPointF m_previousTracePosition;
    bool m_hasPreviousTracePosition = false;
    QColor m_traceColor = QColor("#ff4b55");
    double m_traceWidth = 2.5;
    int m_maximumTraceDots = 16;
    int m_maximumAreaPreviewPixels = 20000;
    double m_minimumCircularityPreviewThreshold = 0.45;
    double m_objectCircularity = 0.0;
    bool m_traceEnabled = false;
    bool m_maximumAreaPreviewVisible = false;
    bool m_minimumCircularityPreviewVisible = false;
    bool m_hasObjectMeasurement = false;
    bool m_panUnlocked = false;
    bool m_panningImage = false;
    bool m_shutdownComplete = false;
    bool m_objectDetected = false;
    QPointF m_objectPosition;
    QPointF m_imagePanNormalized;
    QPointF m_lastPanMousePosition;
    QString m_currentCameraSourceId;
    double m_objectRadius = 0.0;
    int m_imageZoomPercent = 100;
    int m_cameraRotationDegrees = 0;
};
