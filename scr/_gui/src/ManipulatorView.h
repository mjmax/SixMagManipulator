#pragma once

#include <QImage>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QWidget>

#include <array>

class QCamera;
class QPainter;

class ManipulatorView final : public QWidget
{
    Q_OBJECT

public:
    explicit ManipulatorView(QWidget *parent = nullptr);
    QSize sizeHint() const override;

    double lowerServoAngleLimit() const { return m_lowerServoAngleLimit; }
    double upperServoAngleLimit() const { return m_upperServoAngleLimit; }

public slots:
    // Zero degrees means the red (north) half points toward the workspace.
    // Positive angle values rotate counterclockwise in the top view.
    void setMagnetAngle(int magnetIndex, double angleDegrees);
    void resetMagnetAngles();

    // Intended for the future calibration interface. Invalid ranges are
    // ignored, preserving the last valid pair of limits.
    void setServoAngleLimits(double lowerDegrees, double upperDegrees);

signals:
    void servoAngleLimitsChanged(double lowerDegrees, double upperDegrees);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void receiveVideoFrame(const QVideoFrame &frame);

private:
    void startDefaultCamera();
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
    QImage m_latestFrame;
    QCamera *m_camera = nullptr;
    QMediaCaptureSession m_captureSession;
    QVideoSink m_videoSink;
};

