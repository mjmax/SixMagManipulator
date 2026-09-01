#include "VimbaCameraSource.h"

#include <QMutexLocker>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <malloc.h>
#include <thread>
#include <vector>

namespace {
constexpr int streamBufferCount = 32;
constexpr int startupFramesToDiscard = 12;
constexpr double fixedExposureTimeMicroseconds = 5000.0;

QString errorText(const QString &operation, VmbError_t error)
{
    return QStringLiteral("%1 failed (Vmb error %2)").arg(operation).arg(error);
}

void setFloatIfAvailable(VmbHandle_t handle, const char *name, double value)
{
    double minimum = 0.0;
    double maximum = 0.0;
    if (VmbFeatureFloatRangeQuery(handle, name, &minimum, &maximum) != VmbErrorSuccess)
        return;
    VmbFeatureFloatSet(handle, name, std::clamp(value, minimum, maximum));
}

void *allocateStreamBuffer(VmbUint32_t payloadSize,
                           VmbUint32_t requestedAlignment,
                           VmbUint32_t &allocatedSize)
{
    const size_t alignment = std::max<size_t>(
        requestedAlignment, alignof(void *));
    if ((alignment & (alignment - 1)) != 0)
        return nullptr;

    const size_t mask = alignment - 1;
    const size_t roundedSize = (static_cast<size_t>(payloadSize) + mask) & ~mask;
    if (roundedSize > std::numeric_limits<VmbUint32_t>::max())
        return nullptr;

    allocatedSize = static_cast<VmbUint32_t>(roundedSize);
    return _aligned_malloc(roundedSize, alignment);
}

void runCommandAndWait(VmbHandle_t handle, const char *name)
{
    if (VmbFeatureCommandRun(handle, name) != VmbErrorSuccess)
        return;

    for (int attempt = 0; attempt < 250; ++attempt) {
        VmbBool_t done = false;
        if (VmbFeatureCommandIsDone(handle, name, &done) != VmbErrorSuccess
            || done) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
}

VimbaCameraSource::VimbaCameraSource(const QString &cameraId, QObject *parent)
    : QThread(parent), m_cameraId(cameraId)
{
    qRegisterMetaType<VimbaFeatureState>("VimbaFeatureState");
}

VimbaCameraSource::~VimbaCameraSource()
{
    stop();
}

QVector<VimbaCameraDescriptor> VimbaCameraSource::availableCameras()
{
    QVector<VimbaCameraDescriptor> result;
    if (VmbStartup(nullptr) != VmbErrorSuccess)
        return result;

    VmbUint32_t count = 0;
    VmbCamerasList(nullptr, 0, &count, sizeof(VmbCameraInfo_t));
    std::vector<VmbCameraInfo_t> cameras(count);
    if (count > 0
        && VmbCamerasList(cameras.data(), count, &count,
                          sizeof(VmbCameraInfo_t)) == VmbErrorSuccess) {
        for (VmbUint32_t index = 0; index < count; ++index) {
            const VmbCameraInfo_t &camera = cameras[index];
            if (!camera.cameraIdString || !camera.modelName)
                continue;
            const QString model = QString::fromUtf8(camera.modelName);
            // Exclude the Vimba software cameras from the physical source list.
            if (model.contains(QStringLiteral("Simulator"), Qt::CaseInsensitive))
                continue;
            VimbaCameraDescriptor descriptor;
            descriptor.id = QString::fromUtf8(camera.cameraIdString);
            const QString serial = camera.serialString
                ? QString::fromUtf8(camera.serialString) : QString();
            descriptor.displayName = serial.isEmpty()
                ? model : QStringLiteral("%1 (%2)").arg(model, serial);
            result.append(descriptor);
        }
    }
    VmbShutdown();
    return result;
}

void VimbaCameraSource::stop()
{
    m_stopping.store(true, std::memory_order_release);
    if (isRunning())
        wait();
}

void VimbaCameraSource::setExposureTime(double value)
{
    Q_UNUSED(value)
    QMutexLocker locker(&m_controlMutex);
    m_pendingControls.exposure = fixedExposureTimeMicroseconds;
    m_pendingControls.exposurePending = true;
}

void VimbaCameraSource::setGain(double value)
{
    QMutexLocker locker(&m_controlMutex);
    m_pendingControls.gain = value;
    m_pendingControls.gainPending = true;
}

void VimbaCameraSource::setBlackLevel(double value)
{
    QMutexLocker locker(&m_controlMutex);
    m_pendingControls.blackLevel = value;
    m_pendingControls.blackLevelPending = true;
}

void VimbaCameraSource::setGamma(double value)
{
    QMutexLocker locker(&m_controlMutex);
    m_pendingControls.gamma = value;
    m_pendingControls.gammaPending = true;
}

VimbaFeatureState VimbaCameraSource::featureState(VmbHandle_t cameraHandle,
                                                   const char *name)
{
    VimbaFeatureState state;
    if (VmbFeatureFloatRangeQuery(cameraHandle, name,
                                  &state.minimum, &state.maximum) != VmbErrorSuccess
        || VmbFeatureFloatGet(cameraHandle, name, &state.value) != VmbErrorSuccess) {
        return state;
    }
    state.available = true;
    return state;
}

void VimbaCameraSource::applyPendingControls(VmbHandle_t cameraHandle)
{
    PendingControls pending;
    {
        QMutexLocker locker(&m_controlMutex);
        pending = m_pendingControls;
        m_pendingControls = {};
    }
    if (pending.exposurePending)
        setFloatIfAvailable(cameraHandle, "ExposureTime", pending.exposure);
    if (pending.gainPending)
        setFloatIfAvailable(cameraHandle, "Gain", pending.gain);
    if (pending.blackLevelPending)
        setFloatIfAvailable(cameraHandle, "BlackLevel", pending.blackLevel);
    if (pending.gammaPending)
        setFloatIfAvailable(cameraHandle, "Gamma", pending.gamma);
}

void VMB_CALL VimbaCameraSource::frameCallback(VmbHandle_t,
                                               VmbHandle_t streamHandle,
                                               VmbFrame_t *frame)
{
    auto *source = static_cast<VimbaCameraSource *>(frame->context[0]);
    if (source)
        source->handleFrame(streamHandle, frame);
}

void VimbaCameraSource::handleFrame(VmbHandle_t streamHandle, VmbFrame_t *frame)
{
    const bool validFrame =
        !m_stopping.load(std::memory_order_acquire)
        && frame->receiveStatus == VmbFrameStatusComplete
        && (frame->receiveFlags & VmbFrameFlagsDimension) != 0
        && (frame->receiveFlags & VmbFrameFlagsImageData) != 0
        && frame->pixelFormat == VmbPixelFormatMono8
        && frame->imageData && frame->width > 0 && frame->height > 0
        && static_cast<VmbUint64_t>(frame->width) * frame->height
            <= frame->bufferSize;

    bool discardWarmupFrame = false;
    int remaining = m_warmupFramesRemaining.load(std::memory_order_acquire);
    while (validFrame && remaining > 0) {
        if (m_warmupFramesRemaining.compare_exchange_weak(
                remaining, remaining - 1, std::memory_order_acq_rel)) {
            discardWarmupFrame = true;
            break;
        }
    }

    if (validFrame && !discardWarmupFrame) {
        const QImage view(frame->imageData,
                          static_cast<int>(frame->width),
                          static_cast<int>(frame->height),
                          static_cast<int>(frame->width),
                          QImage::Format_Grayscale8);
        const qint64 timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        emit frameReady(view.copy(), timestamp);
    }
    if (!m_stopping.load(std::memory_order_acquire))
        VmbCaptureFrameQueue(streamHandle, frame, &VimbaCameraSource::frameCallback);
}

void VimbaCameraSource::run()
{
    m_stopping.store(false, std::memory_order_release);
    VmbHandle_t cameraHandle = nullptr;
    VmbHandle_t streamHandle = nullptr;
    std::vector<VmbFrame_t> frames(streamBufferCount);
    std::vector<void *> allocations(streamBufferCount, nullptr);
    bool captureStarted = false;
    bool acquisitionStarted = false;

    VmbError_t error = VmbStartup(nullptr);
    if (error != VmbErrorSuccess) {
        emit sourceError(errorText(QStringLiteral("Vimba startup"), error));
        return;
    }

    const QByteArray cameraId = m_cameraId.toUtf8();
    error = VmbCameraOpen(cameraId.constData(), VmbAccessModeFull, &cameraHandle);
    if (error != VmbErrorSuccess) {
        emit sourceError(errorText(QStringLiteral("Opening camera"), error));
        VmbShutdown();
        return;
    }

    VmbCameraInfo_t cameraInfo{};
    error = VmbCameraInfoQueryByHandle(cameraHandle, &cameraInfo,
                                       sizeof(VmbCameraInfo_t));
    if (error == VmbErrorSuccess
        && cameraInfo.streamCount > 0 && cameraInfo.streamHandles) {
        streamHandle = cameraInfo.streamHandles[0];
    } else if (error == VmbErrorSuccess) {
        error = VmbErrorNotFound;
    }

    // Clear both the device and transport sides of an acquisition that may
    // have been interrupted by an earlier client or camera-source switch.
    if (error == VmbErrorSuccess) {
        runCommandAndWait(cameraHandle, "AcquisitionAbort");
        runCommandAndWait(cameraHandle, "AcquisitionStop");
        VmbCaptureEnd(streamHandle);
        VmbCaptureQueueFlush(streamHandle);
    }

    // A previous client may also have left the camera in a different pixel
    // format. Verify Mono8 before interpreting any frame bytes.
    if (error == VmbErrorSuccess) {
        error = VmbFeatureEnumSet(cameraHandle, "PixelFormat", "Mono8");
    }
    const char *activePixelFormat = nullptr;
    if (error == VmbErrorSuccess)
        error = VmbFeatureEnumGet(cameraHandle, "PixelFormat", &activePixelFormat);
    if (error == VmbErrorSuccess
        && (!activePixelFormat || std::strcmp(activePixelFormat, "Mono8") != 0)) {
        error = VmbErrorInvalidValue;
    }

    if (error == VmbErrorSuccess) {
        VmbFeatureEnumSet(cameraHandle, "ExposureAuto", "Off");
        setFloatIfAvailable(cameraHandle, "ExposureTime",
                            fixedExposureTimeMicroseconds);
        VmbFeatureEnumSet(cameraHandle, "AcquisitionMode", "Continuous");
        VmbFeatureEnumSet(cameraHandle, "TriggerSelector", "FrameStart");
        VmbFeatureEnumSet(cameraHandle, "TriggerMode", "Off");
        VmbFeatureEnumSet(cameraHandle, "AcquisitionFrameRateMode", "Basic");
    }
    double frameRateMinimum = 0.0;
    double frameRateMaximum = 0.0;
    if (error == VmbErrorSuccess
        && VmbFeatureFloatRangeQuery(cameraHandle, "AcquisitionFrameRate",
                                  &frameRateMinimum, &frameRateMaximum) == VmbErrorSuccess) {
        VmbFeatureFloatSet(cameraHandle, "AcquisitionFrameRate", frameRateMaximum);
    }

    if (error == VmbErrorSuccess) {
        emit controlsReady(featureState(cameraHandle, "ExposureTime"),
                           featureState(cameraHandle, "Gain"),
                           featureState(cameraHandle, "BlackLevel"),
                           featureState(cameraHandle, "Gamma"));
    }

    VmbUint32_t payloadSize = 0;
    if (error == VmbErrorSuccess)
        error = VmbPayloadSizeGet(cameraHandle, &payloadSize);

    VmbInt64_t alignmentValue = 1;
    if (error == VmbErrorSuccess
        && VmbFeatureIntGet(streamHandle, "StreamBufferAlignment",
                            &alignmentValue) != VmbErrorSuccess) {
        alignmentValue = 1;
    }
    if (alignmentValue < 1
        || static_cast<VmbUint64_t>(alignmentValue)
            > std::numeric_limits<VmbUint32_t>::max()) {
        error = VmbErrorInvalidValue;
    }

    if (error == VmbErrorSuccess) {
        for (int index = 0; index < streamBufferCount; ++index) {
            VmbUint32_t allocatedSize = 0;
            allocations[index] = allocateStreamBuffer(
                payloadSize, static_cast<VmbUint32_t>(alignmentValue),
                allocatedSize);
            if (!allocations[index]) {
                error = VmbErrorResources;
                break;
            }
            std::memset(&frames[index], 0, sizeof(VmbFrame_t));
            frames[index].buffer = allocations[index];
            frames[index].bufferSize = allocatedSize;
            frames[index].context[0] = this;
            error = VmbFrameAnnounce(streamHandle, &frames[index],
                                     sizeof(VmbFrame_t));
            if (error != VmbErrorSuccess)
                break;
        }
    }
    if (error == VmbErrorSuccess) {
        error = VmbCaptureStart(streamHandle);
        captureStarted = error == VmbErrorSuccess;
    }
    if (error == VmbErrorSuccess) {
        for (VmbFrame_t &frame : frames) {
            error = VmbCaptureFrameQueue(streamHandle, &frame,
                                          &VimbaCameraSource::frameCallback);
            if (error != VmbErrorSuccess)
                break;
        }
    }
    if (error == VmbErrorSuccess) {
        m_warmupFramesRemaining.store(startupFramesToDiscard,
                                      std::memory_order_release);
        error = VmbFeatureCommandRun(cameraHandle, "AcquisitionStart");
        acquisitionStarted = error == VmbErrorSuccess;
    }

    if (error != VmbErrorSuccess) {
        emit sourceError(errorText(QStringLiteral("Starting acquisition"), error));
    } else {
        while (!m_stopping.load(std::memory_order_acquire)) {
            applyPendingControls(cameraHandle);
            msleep(2);
        }
    }

    if (acquisitionStarted)
        VmbFeatureCommandRun(cameraHandle, "AcquisitionStop");
    if (captureStarted) {
        VmbCaptureEnd(streamHandle);
        VmbCaptureQueueFlush(streamHandle);
    }
    if (streamHandle)
        VmbFrameRevokeAll(streamHandle);
    for (void *allocation : allocations)
        _aligned_free(allocation);
    VmbCameraClose(cameraHandle);
    VmbShutdown();
}
