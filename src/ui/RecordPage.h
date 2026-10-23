#pragma once

#include "core/Types.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;

namespace lightrec {

class RecordPage : public QWidget
{
    Q_OBJECT
public:
    explicit RecordPage(QWidget *parent = nullptr);

    RecordOptions options() const;
    void setOptions(const RecordOptions &options);
    void reloadDefaults();
    void setState(RecordState state);
    void setElapsed(qint64 elapsedMs);
    void setEncoderText(const QString &text);
    void setStatusText(const QString &text);
    void setOutputDir(const QString &dir);
    QString outputDir() const;
    void setRegion(const QRect &region);
    void setWindow(WId windowId, const QString &title);
    void refreshDevices();
    void refreshCameras();
    void updateFfmpegStatus();

signals:
    void startRequested();
    void pauseRequested();
    void stopRequested();
    void selectRegionRequested();
    void selectWindowRequested();
    void outputDirChanged(const QString &dir);
    void optionsChanged();
    void installFfmpegRequested();

private:
    void buildUi();
    void updateModeVisibility();
    void updateBitrateLabel();
    void updateGainLabels();
    void updateLayoutControls();
    void updateStateControls();
    void refreshMonitors();
    CaptureMode currentMode() const;

    VideoConfig m_video;
    AudioConfig m_audio;
    OverlayConfig m_overlay;
    StreamConfig m_stream;
    RecordState m_state = RecordState::Idle;

    QPushButton *m_fullScreenButton = nullptr;
    QPushButton *m_windowButton = nullptr;
    QPushButton *m_regionButton = nullptr;
    QComboBox *m_monitorCombo = nullptr;
    QWidget *m_monitorRow = nullptr;
    QWidget *m_windowRow = nullptr;
    QWidget *m_regionRow = nullptr;
    QLabel *m_windowLabel = nullptr;
    QLabel *m_regionLabel = nullptr;
    QComboBox *m_codecCombo = nullptr;
    QComboBox *m_fpsCombo = nullptr;
    QSlider *m_bitrateSlider = nullptr;
    QLabel *m_bitrateLabel = nullptr;
    QCheckBox *m_cursorCheck = nullptr;

    QCheckBox *m_systemAudioCheck = nullptr;
    QComboBox *m_systemDeviceCombo = nullptr;
    QCheckBox *m_micCheck = nullptr;
    QComboBox *m_micDeviceCombo = nullptr;
    QCheckBox *m_denoiseCheck = nullptr;
    QSlider *m_micGainSlider = nullptr;
    QLabel *m_micGainLabel = nullptr;
    QSlider *m_systemGainSlider = nullptr;
    QLabel *m_systemGainLabel = nullptr;

    QCheckBox *m_cameraCheck = nullptr;
    QComboBox *m_cameraCombo = nullptr;
    QComboBox *m_layoutCombo = nullptr;
    QCheckBox *m_mirrorCheck = nullptr;
    QSlider *m_cameraOpacitySlider = nullptr;
    QLabel *m_cameraOpacityLabel = nullptr;
    QPushButton *m_pptButton = nullptr;
    QCheckBox *m_mouseCheck = nullptr;
    QCheckBox *m_keysCheck = nullptr;

    QCheckBox *m_streamCheck = nullptr;
    QLineEdit *m_streamUrlEdit = nullptr;
    QLineEdit *m_streamKeyEdit = nullptr;
    QCheckBox *m_streamSaveCheck = nullptr;

    QLineEdit *m_outputDirEdit = nullptr;
    QPushButton *m_recordButton = nullptr;
    QPushButton *m_pauseButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QLabel *m_elapsedLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_encoderLabel = nullptr;
    QPushButton *m_ffmpegButton = nullptr;
};

} // namespace lightrec
