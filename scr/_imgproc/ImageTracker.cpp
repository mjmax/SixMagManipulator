#include "ImageTracker.h"

#include <QElapsedTimer>
#include <QMutexLocker>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;

struct Candidate
{
    QPointF normalizedPosition;
    double normalizedRadius = 0.0;
    int area = 0;
    double circularity = 0.0;
};
}

ImageTracker::ImageTracker(QObject *parent)
    : QThread(parent)
{
    qRegisterMetaType<TrackingResult>("TrackingResult");
}

ImageTracker::~ImageTracker()
{
    stop();
}

void ImageTracker::submitFrame(const QImage &frame, qint64 timestampNanoseconds)
{
    if (frame.isNull())
        return;

    QMutexLocker locker(&m_mutex);
    // QImage is implicitly shared, so this assignment does not duplicate the
    // camera pixels. Only the newest unprocessed frame is retained.
    m_pendingFrame = frame;
    m_pendingTimestampNanoseconds = timestampNanoseconds;
    m_pendingSubmittedAtSteadySeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_hasPendingFrame = true;
    m_frameAvailable.wakeOne();
}

void ImageTracker::setSettings(const ImageProcessingSettings &settings)
{
    ImageProcessingSettings validated = settings;
    validated.threshold = std::clamp(validated.threshold, 0, 255);
    validated.minimumAreaPixels = std::max(1, validated.minimumAreaPixels);
    validated.maximumAreaPixels = std::max(
        validated.minimumAreaPixels + 1,
        validated.maximumAreaPixels);
    validated.minimumCircularity = std::clamp(
        validated.minimumCircularity, 0.0, 1.0);
    validated.maximumObjects = std::clamp(validated.maximumObjects, 1, 2);
    validated.visualizationRateHz = std::clamp(
        validated.visualizationRateHz, 1, 60);

    QMutexLocker locker(&m_mutex);
    m_settings = validated;
}

void ImageTracker::setMillimeterTransform(
    const MillimeterTransform &displayTransform,
    const MillimeterTransform &modelTransform)
{
    QMutexLocker locker(&m_mutex);
    m_millimeterTransform = displayTransform;
    m_modelMillimeterTransform = modelTransform;
}

ImageProcessingSettings ImageTracker::settings() const
{
    QMutexLocker locker(&m_mutex);
    return m_settings;
}

TrackingResult ImageTracker::latestResult() const
{
    QMutexLocker locker(&m_mutex);
    return m_latestResult;
}

void ImageTracker::stop()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_stopping)
            return;
        m_stopping = true;
        m_frameAvailable.wakeOne();
    }
    if (isRunning())
        wait();
}

void ImageTracker::run()
{
    QElapsedTimer visualizationTimer;
    QElapsedTimer rateTimer;
    visualizationTimer.start();
    rateTimer.start();
    quint64 frameId = 0;
    int framesInRateWindow = 0;
    double processingRate = 0.0;

    while (true) {
        QImage frame;
        qint64 timestampNanoseconds = 0;
        double submittedAtSteadySeconds = 0.0;
        ImageProcessingSettings currentSettings;
        MillimeterTransform millimeterTransform;
        MillimeterTransform modelMillimeterTransform;
        {
            QMutexLocker locker(&m_mutex);
            while (!m_hasPendingFrame && !m_stopping)
                m_frameAvailable.wait(&m_mutex);
            if (m_stopping)
                break;

            frame = std::move(m_pendingFrame);
            timestampNanoseconds = m_pendingTimestampNanoseconds;
            submittedAtSteadySeconds = m_pendingSubmittedAtSteadySeconds;
            m_hasPendingFrame = false;
            currentSettings = m_settings;
            millimeterTransform = m_millimeterTransform;
            modelMillimeterTransform = m_modelMillimeterTransform;
        }

        TrackingResult result = detectObjects(
            frame, timestampNanoseconds, ++frameId, currentSettings);
        result.submittedAtSteadySeconds = submittedAtSteadySeconds;
        for (TrackedObject &object : result.objects) {
            const QPointF position = object.normalizedPosition;
            object.positionMillimeters = millimeterTransform.origin
                + millimeterTransform.xStep * position.x()
                + millimeterTransform.yStep * position.y();
            object.modelPositionMillimeters = modelMillimeterTransform.origin
                + modelMillimeterTransform.xStep * position.x()
                + modelMillimeterTransform.yStep * position.y();
        }
        result.completedAtSteadySeconds =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();

        ++framesInRateWindow;
        const qint64 rateElapsed = rateTimer.elapsed();
        if (rateElapsed >= 500) {
            processingRate = 1000.0 * static_cast<double>(framesInRateWindow)
                / static_cast<double>(rateElapsed);
            framesInRateWindow = 0;
            rateTimer.restart();
        }
        result.processingFramesPerSecond = processingRate;

        {
            QMutexLocker locker(&m_mutex);
            m_latestResult = result;
        }

        emit fastResultReady(result);

        const int visualizationInterval = std::max(
            1, 1000 / currentSettings.visualizationRateHz);
        if (visualizationTimer.elapsed() >= visualizationInterval) {
            visualizationTimer.restart();
            emit visualizationResultReady(result);
        }
    }
}

TrackingResult ImageTracker::detectObjects(
    const QImage &frame,
    qint64 timestampNanoseconds,
    quint64 frameId,
    const ImageProcessingSettings &settings) const
{
    QElapsedTimer detectionTimer;
    detectionTimer.start();

    TrackingResult result;
    result.frameId = frameId;
    result.timestampNanoseconds = timestampNanoseconds;

    const QImage gray = frame.convertToFormat(QImage::Format_Grayscale8);
    if (gray.isNull())
        return result;

    // The GUI displays the centered square crop inside a circular clip. Search
    // exactly that circle; objects outside the visible workspace are ignored.
    const int side = std::min(gray.width(), gray.height());
    if (side < 3)
        return result;
    const int cropX = (gray.width() - side) / 2;
    const int cropY = (gray.height() - side) / 2;
    const double center = (static_cast<double>(side) - 1.0) * 0.5;
    const double radius = center;
    const double radiusSquared = radius * radius;

    std::vector<int> labels(static_cast<std::size_t>(side) * side, -1);
    std::vector<int> queue;
    queue.reserve(4096);
    std::vector<Candidate> candidates;
    int componentLabel = 0;

    const auto isInsideWorkspace = [center, radiusSquared](int x, int y) {
        const double dx = static_cast<double>(x) - center;
        const double dy = static_cast<double>(y) - center;
        return dx * dx + dy * dy <= radiusSquared;
    };
    const auto isDark = [&gray, cropX, cropY, &settings](int x, int y) {
        return gray.constScanLine(cropY + y)[cropX + x] <= settings.threshold;
    };

    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const int seedIndex = y * side + x;
            if (labels[static_cast<std::size_t>(seedIndex)] >= 0
                || !isInsideWorkspace(x, y)
                || !isDark(x, y)) {
                continue;
            }

            queue.clear();
            queue.push_back(seedIndex);
            labels[static_cast<std::size_t>(seedIndex)] = componentLabel;
            std::size_t queuePosition = 0;
            int area = 0;
            int perimeter = 0;
            double sumX = 0.0;
            double sumY = 0.0;

            while (queuePosition < queue.size()) {
                const int index = queue[queuePosition++];
                const int pixelY = index / side;
                const int pixelX = index - pixelY * side;
                ++area;
                sumX += pixelX;
                sumY += pixelY;

                constexpr int dx[4] = {-1, 1, 0, 0};
                constexpr int dy[4] = {0, 0, -1, 1};
                for (int direction = 0; direction < 4; ++direction) {
                    const int neighborX = pixelX + dx[direction];
                    const int neighborY = pixelY + dy[direction];
                    if (neighborX < 0 || neighborX >= side
                        || neighborY < 0 || neighborY >= side
                        || !isInsideWorkspace(neighborX, neighborY)
                        || !isDark(neighborX, neighborY)) {
                        ++perimeter;
                        continue;
                    }

                    const int neighborIndex = neighborY * side + neighborX;
                    int &neighborLabel = labels[static_cast<std::size_t>(neighborIndex)];
                    if (neighborLabel < 0) {
                        neighborLabel = componentLabel;
                        queue.push_back(neighborIndex);
                    }
                }
            }
            ++componentLabel;

            if (area < settings.minimumAreaPixels
                || area > settings.maximumAreaPixels
                || perimeter <= 0) {
                continue;
            }

            const double circularity = 4.0 * pi * static_cast<double>(area)
                / static_cast<double>(perimeter * perimeter);
            if (circularity < settings.minimumCircularity)
                continue;

            Candidate candidate;
            candidate.normalizedPosition = {
                (sumX / area) / static_cast<double>(side - 1),
                (sumY / area) / static_cast<double>(side - 1)
            };
            candidate.normalizedRadius = std::sqrt(area / pi)
                / static_cast<double>(side);
            candidate.area = area;
            candidate.circularity = circularity;
            candidates.push_back(candidate);
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &left, const Candidate &right) {
        if (left.circularity != right.circularity)
            return left.circularity > right.circularity;
        return left.area > right.area;
    });

    const int resultCount = std::min(
        settings.maximumObjects,
        static_cast<int>(candidates.size()));
    result.objects.reserve(resultCount);
    for (int index = 0; index < resultCount; ++index) {
        const Candidate &candidate = candidates[static_cast<std::size_t>(index)];
        TrackedObject object;
        object.id = index;
        object.normalizedPosition = candidate.normalizedPosition;
        object.normalizedRadius = candidate.normalizedRadius;
        object.areaPixels = candidate.area;
        object.circularity = candidate.circularity;
        result.objects.push_back(object);
    }

    result.detectionMilliseconds = detectionTimer.nsecsElapsed() / 1.0e6;
    return result;
}
