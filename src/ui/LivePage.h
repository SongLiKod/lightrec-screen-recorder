#pragma once

#include "core/Types.h"

#include <QWidget>

#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace lightrec {

class LiveServer;
class LanPreview;
class RecordingController;
class OverlayController;

class LivePage : public QWidget
{
    Q_OBJECT
public:
    explicit LivePage(RecordingController *controller, OverlayController *overlay,
                      QWidget *parent = nullptr);
    ~LivePage() override;

    void setOptionsProvider(std::function<RecordOptions()> provider);
    void setRecordingState(RecordState state);
    void prepareForRecording();
    void stopAll();

signals:
    void startStreamRequested(const lightrec::StreamConfig &config);
    void stopStreamRequested();

private:
    void buildUi();
    void startRtmp();
    void stopRtmp();
    void startLan();
    void stopLan();
    void updateLanUi();
    void updateQr();
    void connectRecordingFrames(bool connectFrames);
    void startPreview();
    void stopPreview();
    void handleRecordingState(RecordState state);

    RecordingController *m_controller = nullptr;
    OverlayController *m_overlay = nullptr;
    LiveServer *m_server = nullptr;
    LanPreview *m_preview = nullptr;
    std::function<RecordOptions()> m_optionsProvider;

    QLineEdit *m_rtmpUrl = nullptr;
    QLineEdit *m_rtmpKey = nullptr;
    QCheckBox *m_saveLocal = nullptr;
    QPushButton *m_rtmpButton = nullptr;
    QLabel *m_rtmpStatus = nullptr;

    QSpinBox *m_portSpin = nullptr;
    QPushButton *m_lanButton = nullptr;
    QLabel *m_lanUrl = nullptr;
    QLabel *m_qrLabel = nullptr;
    QLabel *m_viewerLabel = nullptr;
    QLabel *m_lanStatus = nullptr;

    bool m_lanActive = false;
    bool m_rtmpActive = false;
    bool m_previewRunning = false;
    QMetaObject::Connection m_frameConnection;
};

} // namespace lightrec
