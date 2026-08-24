#include "ManipulatorView.h"
#include "ChassisGeometry.h"

#include <QCamera>
#include <QCameraDevice>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMediaDevices>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QRadialGradient>
#include <QVideoFrame>

#include <algorithm>
#include <cmath>

namespace {
constexpr double pi = 3.14159265358979323846;

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
    connect(&m_videoSink, &QVideoSink::videoFrameChanged,
            this, &ManipulatorView::receiveVideoFrame);
    startDefaultCamera();
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

void ManipulatorView::startDefaultCamera()
{
    const QCameraDevice device = QMediaDevices::defaultVideoInput();
    if (device.isNull())
        return;
    m_camera = new QCamera(device, this);
    m_captureSession.setCamera(m_camera);
    m_captureSession.setVideoSink(&m_videoSink);
    m_camera->start();
}

void ManipulatorView::receiveVideoFrame(const QVideoFrame &frame)
{
    const QImage image = frame.toImage();
    if (image.isNull())
        return;
    m_latestFrame = image;
    update();
}

void ManipulatorView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#141b24"));
    drawManipulator(painter, rect().adjusted(18, 18, -18, -18));
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
    painter.drawText(QRectF(area.left(), area.bottom() - 22, designSize, 20),
                     Qt::AlignCenter,
                     m_latestFrame.isNull()
                         ? "CAMERA OFFLINE  •  MAGNET ANGLES 0°"
                         : "LIVE WORKSPACE  •  MAGNET ANGLES 0°");
}

void ManipulatorView::drawWorkspace(QPainter &painter, const QPointF &center, double radius)
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
    if (!m_latestFrame.isNull()) {
        const QSize targetSize(
            std::max(1, static_cast<int>(std::ceil(circle.width()))),
            std::max(1, static_cast<int>(std::ceil(circle.height()))));
        const QImage scaled = m_latestFrame.scaled(
            targetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const QRect source(
            std::max(0, (scaled.width() - targetSize.width()) / 2),
            std::max(0, (scaled.height() - targetSize.height()) / 2),
            targetSize.width(), targetSize.height());
        painter.drawImage(circle, scaled, source);
    } else {
        QRadialGradient glass(center - QPointF(radius * 0.25, radius * 0.3),
                              radius * 1.25);
        glass.setColorAt(0.0, QColor("#263542"));
        glass.setColorAt(0.55, QColor("#111a23"));
        glass.setColorAt(1.0, QColor("#05090e"));
        painter.fillRect(circle, glass);
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

