#pragma once

#include "ImageTracker.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QMediaCaptureSession>
#include <QPointF>
#include <QVector>
#include <QVideoSink>
#include <QWidget>

#include <array>

class QCamera;
class QPainter;
class QPushButton;
class QResizeEvent;

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

public slots:
    void setMagnetAngle(int magnetIndex, double angleDegrees);
    void resetMagnetAngles();
    void setServoAngleLimits(double lowerDegrees, double upperDegrees);

    void setDetectionThreshold(int threshold);
    void setDetectionMinimumArea(int pixels);
    void setDetectionMaximumArea(int pixels);
    void setDetectionMinimumCircularity(double circularity);
    void setVisualizationRate(int framesPerSecond);
    void setTraceColor(const QColor &color);
    void setTraceWidth(double pixels);
    void setTraceEnabled(bool enabled);
    void clearTrace();

signals:
    void servoAngleLimitsChanged(double lowerDegrees, double upperDegrees);
    void trackingStatusChanged(
        bool detected,
        const QPointF &normalizedPosition,
        double detectionMilliseconds,
        double processingFramesPerSecond);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void receiveVideoFrame(const QVideoFrame &frame);
    void receiveTrackingVisualization(const TrackingResult &result);

private:
    void startDefaultCamera();
    void applyImageProcessingSettings(const ImageProcessingSettings &settings);
    void appendTracePoint(const QPointF &normalizedPosition);
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
    QMediaCaptureSession m_captureSession;
    QVideoSink m_videoSink;
    ImageTracker *m_imageTracker = nullptr;
    QPushButton *m_clearTraceButton = nullptr;
    QElapsedTimer m_cameraClock;
    QElapsedTimer m_displayFrameClock;
    int m_visualizationRateHz = 15;

    QVector<QVector<QPointF>> m_traceSegments;
    bool m_startNewTraceSegment = true;
    QColor m_traceColor = QColor("#ff4b55");
    double m_traceWidth = 2.5;
    bool m_traceEnabled = false;
    bool m_objectDetected = false;
    QPointF m_objectPosition;
    double m_objectRadius = 0.0;
};

