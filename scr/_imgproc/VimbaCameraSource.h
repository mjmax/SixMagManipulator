#pragma once

#include <QImage>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QVector>

#include <atomic>

#include <VmbC/VmbC.h>

struct VimbaCameraDescriptor
{
    QString id;
    QString displayName;
};

struct VimbaFeatureState
{
    bool available = false;
    double minimum = 0.0;
    double maximum = 0.0;
    double value = 0.0;
};

class VimbaCameraSource final : public QThread
{
    Q_OBJECT

public:
    explicit VimbaCameraSource(const QString &cameraId,
                               QObject *parent = nullptr);
    ~VimbaCameraSource() override;

    static QVector<VimbaCameraDescriptor> availableCameras();
    void stop();
    void setExposureTime(double value);
    void setGain(double value);
    void setBlackLevel(double value);
    void setGamma(double value);

signals:
    void frameReady(const QImage &image, qint64 timestampNanoseconds);
    void controlsReady(const VimbaFeatureState &exposure,
                       const VimbaFeatureState &gain,
                       const VimbaFeatureState &blackLevel,
                       const VimbaFeatureState &gamma);
    void sourceError(const QString &message);

protected:
    void run() override;

private:
    struct PendingControls
    {
        bool exposurePending = false;
        bool gainPending = false;
        bool blackLevelPending = false;
        bool gammaPending = false;
        double exposure = 0.0;
        double gain = 0.0;
        double blackLevel = 0.0;
        double gamma = 0.0;
    };

    static void VMB_CALL frameCallback(VmbHandle_t cameraHandle,
                                       VmbHandle_t streamHandle,
                                       VmbFrame_t *frame);
    void handleFrame(VmbHandle_t streamHandle, VmbFrame_t *frame);
    void applyPendingControls(VmbHandle_t cameraHandle);
    static VimbaFeatureState featureState(VmbHandle_t cameraHandle,
                                          const char *name);

    QString m_cameraId;
    std::atomic_bool m_stopping{false};
    std::atomic_int m_warmupFramesRemaining{0};
    QMutex m_controlMutex;
    PendingControls m_pendingControls;
};

Q_DECLARE_METATYPE(VimbaFeatureState)
