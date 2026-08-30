#pragma once

#include <QImage>
#include <QMutex>
#include <QPointF>
#include <QThread>
#include <QVector>
#include <QWaitCondition>

struct ImageProcessingSettings
{
    int threshold = 90;
    int minimumAreaPixels = 30;
    int maximumAreaPixels = 20000;
    double minimumCircularity = 0.45;
    int maximumObjects = 1;
    int visualizationRateHz = 15;
};

struct TrackedObject
{
    int id = 0;
    QPointF normalizedPosition;
    double normalizedRadius = 0.0;
    int areaPixels = 0;
    double circularity = 0.0;
};

struct TrackingResult
{
    quint64 frameId = 0;
    qint64 timestampNanoseconds = 0;
    QVector<TrackedObject> objects;
    double detectionMilliseconds = 0.0;
    double processingFramesPerSecond = 0.0;
};

Q_DECLARE_METATYPE(TrackingResult)

// A latest-frame worker: camera submission never waits for image processing.
// If a new frame arrives while detection is busy, the older pending frame is
// replaced. fastResultReady is intended for the future control loop, while
// visualizationResultReady is deliberately rate-limited for the GUI.
class ImageTracker final : public QThread
{
    Q_OBJECT

public:
    explicit ImageTracker(QObject *parent = nullptr);
    ~ImageTracker() override;

    void submitFrame(const QImage &frame, qint64 timestampNanoseconds);
    void setSettings(const ImageProcessingSettings &settings);
    ImageProcessingSettings settings() const;
    TrackingResult latestResult() const;
    void stop();

signals:
    void fastResultReady(const TrackingResult &result);
    void visualizationResultReady(const TrackingResult &result);

protected:
    void run() override;

private:
    TrackingResult detectObjects(
        const QImage &frame,
        qint64 timestampNanoseconds,
        quint64 frameId,
        const ImageProcessingSettings &settings) const;

    mutable QMutex m_mutex;
    QWaitCondition m_frameAvailable;
    QImage m_pendingFrame;
    qint64 m_pendingTimestampNanoseconds = 0;
    bool m_hasPendingFrame = false;
    bool m_stopping = false;
    ImageProcessingSettings m_settings;
    TrackingResult m_latestResult;
};

