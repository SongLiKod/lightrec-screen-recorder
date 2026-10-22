#pragma once

#include "core/Types.h"

#include <QObject>
#include <QVariant>

namespace lightrec {

class ConfigStore : public QObject
{
    Q_OBJECT
public:
    static ConfigStore &instance();

    ThemeMode themeMode() const;
    void setThemeMode(ThemeMode mode);

    QString outputDir() const;
    void setOutputDir(const QString &dir);

    int countdownSeconds() const;
    void setCountdownSeconds(int seconds);

    bool minimizeOnStart() const;
    void setMinimizeOnStart(bool value);

    int toolbarScale() const;
    void setToolbarScale(int percent);
    bool showFloatingToolbar() const;
    void setShowFloatingToolbar(bool value);
    int toolbarStyle() const;
    void setToolbarStyle(int style);
    int toolbarOpacity() const;
    void setToolbarOpacity(int opacity);
    bool floatingDotEnabled() const;
    void setFloatingDotEnabled(bool value);
    int floatingDotSize() const;
    void setFloatingDotSize(int size);

    bool virtualCameraEnabled() const;
    void setVirtualCameraEnabled(bool value);

    bool autoRecoverEnabled() const;
    void setAutoRecoverEnabled(bool value);

    VideoConfig videoDefaults() const;
    void setVideoDefaults(const VideoConfig &config);

    AudioConfig audioDefaults() const;
    void setAudioDefaults(const AudioConfig &config);

    OverlayConfig overlayDefaults() const;
    void setOverlayDefaults(const OverlayConfig &config);

    WatermarkConfig watermarkDefaults() const;
    void setWatermarkDefaults(const WatermarkConfig &config);

    StreamConfig streamDefaults() const;
    void setStreamDefaults(const StreamConfig &config);

    QString hotkeyStartStop() const;
    QString hotkeyPause() const;
    QString hotkeyDraw() const;
    QString hotkeyHideOverlay() const;
    void setHotkey(const QString &action, const QString &sequence);
    QString hotkey(const QString &action) const;

    int liveServerPort() const;
    void setLiveServerPort(int port);

    QString lastPreset() const;
    void setLastPreset(const QString &name);

    QVariant value(const QString &key, const QVariant &fallback = QVariant()) const;
    void setValue(const QString &key, const QVariant &value);

    static QJsonObject videoToJson(const VideoConfig &config);
    static VideoConfig videoFromJson(const QJsonObject &object);
    static QJsonObject audioToJson(const AudioConfig &config);
    static AudioConfig audioFromJson(const QJsonObject &object);
    static QJsonObject overlayToJson(const OverlayConfig &config);
    static OverlayConfig overlayFromJson(const QJsonObject &object);
    static QJsonObject watermarkToJson(const WatermarkConfig &config);
    static WatermarkConfig watermarkFromJson(const QJsonObject &object);
    static QJsonObject streamToJson(const StreamConfig &config);
    static StreamConfig streamFromJson(const QJsonObject &object);

signals:
    void settingsChanged();

private:
    explicit ConfigStore(QObject *parent = nullptr);

    QJsonObject readJson(const QString &key) const;
    void writeJson(const QString &key, const QJsonObject &object);
};

} // namespace lightrec
