#pragma once

#include "core/Types.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QShowEvent;
class QSlider;
class QSpinBox;

namespace lightrec {
class FFmpegInstaller;
}

namespace lightrec {

class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);

    void loadFromConfig();
    void startFfmpegInstall();

protected:
    void showEvent(QShowEvent *event) override;

signals:    void settingsSaved();
    void virtualCameraToggled(bool enabled);
    void toolbarScaleChanged(int percent);
    void toolbarStyleChanged(int style);
    void toolbarOpacityChanged(int opacity);
    void showFloatingToolbarChanged(bool visible);
    void captureFloatingToolbarChanged(bool include);
    void floatingDotChanged(bool enabled);
    void floatingDotSizeChanged(int size);

private:
    void buildUi();
    void save();
    void browseOutput();
    void choosePenColor();
    void chooseLeftColor();
    void chooseRightColor();

    QComboBox *m_themeCombo = nullptr;
    QComboBox *m_toolbarScaleCombo = nullptr;
    QComboBox *m_toolbarStyleCombo = nullptr;
    QSlider *m_toolbarOpacity = nullptr;
    QLabel *m_toolbarOpacityLabel = nullptr;
    QCheckBox *m_showFloatingToolbar = nullptr;
    QCheckBox *m_captureFloatingToolbar = nullptr;
    QCheckBox *m_floatingDot = nullptr;
    QSpinBox *m_floatingDotSize = nullptr;
    QLineEdit *m_outputDir = nullptr;
    QCheckBox *m_minimizeOnStart = nullptr;
    QCheckBox *m_autoRecover = nullptr;
    QSpinBox *m_countdownSpin = nullptr;

    QComboBox *m_codecCombo = nullptr;
    QComboBox *m_fpsCombo = nullptr;
    QSlider *m_bitrateSlider = nullptr;
    QLabel *m_bitrateLabel = nullptr;
    QCheckBox *m_cursorCheck = nullptr;
    QCheckBox *m_denoiseCheck = nullptr;
    QSlider *m_micGainSlider = nullptr;
    QLabel *m_micGainLabel = nullptr;
    QSlider *m_systemGainSlider = nullptr;
    QLabel *m_systemGainLabel = nullptr;

    QPushButton *m_penColorButton = nullptr;
    QSpinBox *m_penWidth = nullptr;
    QSpinBox *m_mouseSize = nullptr;
    QPushButton *m_leftColorButton = nullptr;
    QPushButton *m_rightColorButton = nullptr;
    QSpinBox *m_clickDuration = nullptr;
    QSpinBox *m_keyFontSize = nullptr;
    QSlider *m_keyOpacity = nullptr;
    QLabel *m_keyOpacityLabel = nullptr;
    QPushButton *m_resetKeyPosition = nullptr;

    QCheckBox *m_cameraMirror = nullptr;
    QSlider *m_cameraOpacity = nullptr;
    QLabel *m_cameraOpacityLabel = nullptr;
    QComboBox *m_layoutCombo = nullptr;

    QKeySequenceEdit *m_hotkeyStartStop = nullptr;
    QKeySequenceEdit *m_hotkeyPause = nullptr;
    QKeySequenceEdit *m_hotkeyDraw = nullptr;
    QKeySequenceEdit *m_hotkeyHide = nullptr;
    QKeySequenceEdit *m_hotkeyCamera = nullptr;
    QKeySequenceEdit *m_hotkeySpotlight = nullptr;
    QKeySequenceEdit *m_hotkeyMagnifier = nullptr;
    QKeySequenceEdit *m_hotkeyRegion = nullptr;

    QSpinBox *m_livePort = nullptr;
    QCheckBox *m_virtualCamera = nullptr;
    QLabel *m_virtualCameraStatus = nullptr;

    QLabel *m_ffmpegStatus = nullptr;
    QPushButton *m_ffmpegInstallButton = nullptr;
    QProgressBar *m_ffmpegProgress = nullptr;
    FFmpegInstaller *m_ffmpegInstaller = nullptr;

    QColor m_penColor = Qt::red;
    QColor m_leftColor = QColor(0x2D, 0x8C, 0xF0);
    QColor m_rightColor = QColor(0xFF, 0x95, 0x00);
    OverlayConfig m_overlay;
};

} // namespace lightrec
