#include "core/ConfigStore.h"
#include "core/AppPaths.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonDocument>
#include <QSettings>

namespace lightrec {

namespace {
QString colorToString(const QColor &color)
{
    return color.name(QColor::HexArgb);
}

QColor colorFromString(const QString &value, const QColor &fallback)
{
    const QColor color(value);
    return color.isValid() ? color : fallback;
}
} // namespace

ConfigStore &ConfigStore::instance()
{
    static ConfigStore store;
    return store;
}

ConfigStore::ConfigStore(QObject *parent)
    : QObject(parent)
{
    QCoreApplication::setOrganizationName(QStringLiteral("LightRec"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("lightrec.local"));
    QCoreApplication::setApplicationName(QStringLiteral("LightRec"));
}

ThemeMode ConfigStore::themeMode() const
{
    const int value = QSettings().value(QStringLiteral("ui/theme"), 0).toInt();
    if (value < 0 || value > 2)
        return ThemeMode::System;
    return static_cast<ThemeMode>(value);
}

void ConfigStore::setThemeMode(ThemeMode mode)
{
    QSettings().setValue(QStringLiteral("ui/theme"), static_cast<int>(mode));
    emit settingsChanged();
}

QString ConfigStore::outputDir() const
{
    return AppPaths::outputDir();
}

void ConfigStore::setOutputDir(const QString &dir)
{
    AppPaths::setOutputDir(dir);
    emit settingsChanged();
}

int ConfigStore::countdownSeconds() const
{
    return qBound(0, QSettings().value(QStringLiteral("record/countdown"), 5).toInt(), 30);
}

void ConfigStore::setCountdownSeconds(int seconds)
{
    QSettings().setValue(QStringLiteral("record/countdown"), qBound(0, seconds, 30));
    emit settingsChanged();
}

bool ConfigStore::minimizeOnStart() const
{
    return QSettings().value(QStringLiteral("record/minimizeOnStart"), true).toBool();
}

void ConfigStore::setMinimizeOnStart(bool value)
{
    QSettings().setValue(QStringLiteral("record/minimizeOnStart"), value);
    emit settingsChanged();
}

int ConfigStore::toolbarScale() const
{
    return qBound(100, QSettings().value(QStringLiteral("ui/toolbarScale"), 150).toInt(), 250);
}

void ConfigStore::setToolbarScale(int percent)
{
    QSettings().setValue(QStringLiteral("ui/toolbarScale"), qBound(100, percent, 250));
    emit settingsChanged();
}

bool ConfigStore::showFloatingToolbar() const
{
    return QSettings().value(QStringLiteral("ui/showFloatingToolbar"), true).toBool();
}

void ConfigStore::setShowFloatingToolbar(bool value)
{
    QSettings().setValue(QStringLiteral("ui/showFloatingToolbar"), value);
    emit settingsChanged();
}

bool ConfigStore::captureFloatingToolbar() const
{
    return QSettings().value(QStringLiteral("ui/captureFloatingToolbar"), false).toBool();
}

void ConfigStore::setCaptureFloatingToolbar(bool value)
{
    QSettings().setValue(QStringLiteral("ui/captureFloatingToolbar"), value);
    emit settingsChanged();
}

int ConfigStore::toolbarStyle() const
{
    return qBound(0, QSettings().value(QStringLiteral("ui/toolbarStyle"), 2).toInt(), 2);
}

void ConfigStore::setToolbarStyle(int style)
{
    QSettings().setValue(QStringLiteral("ui/toolbarStyle"), qBound(0, style, 2));
    emit settingsChanged();
}

int ConfigStore::toolbarOpacity() const
{
    return qBound(40, QSettings().value(QStringLiteral("ui/toolbarOpacity"), 92).toInt(), 100);
}

void ConfigStore::setToolbarOpacity(int opacity)
{
    QSettings().setValue(QStringLiteral("ui/toolbarOpacity"), qBound(40, opacity, 100));
    emit settingsChanged();
}

bool ConfigStore::floatingDotEnabled() const
{
    return QSettings().value(QStringLiteral("ui/floatingDot"), true).toBool();
}

void ConfigStore::setFloatingDotEnabled(bool value)
{
    QSettings().setValue(QStringLiteral("ui/floatingDot"), value);
    emit settingsChanged();
}

int ConfigStore::floatingDotSize() const
{
    return qBound(44, QSettings().value(QStringLiteral("ui/floatingDotSize"), 64).toInt(), 120);
}

void ConfigStore::setFloatingDotSize(int size)
{
    QSettings().setValue(QStringLiteral("ui/floatingDotSize"), qBound(44, size, 120));
    emit settingsChanged();
}

bool ConfigStore::virtualCameraEnabled() const
{
    return QSettings().value(QStringLiteral("virtualcam/enabled"), false).toBool();
}

void ConfigStore::setVirtualCameraEnabled(bool value)
{
    QSettings().setValue(QStringLiteral("virtualcam/enabled"), value);
    emit settingsChanged();
}

bool ConfigStore::autoRecoverEnabled() const
{
    return QSettings().value(QStringLiteral("record/autoRecover"), true).toBool();
}

void ConfigStore::setAutoRecoverEnabled(bool value)
{
    QSettings().setValue(QStringLiteral("record/autoRecover"), value);
    emit settingsChanged();
}

VideoConfig ConfigStore::videoDefaults() const
{
    const QJsonObject object = readJson(QStringLiteral("record/video"));
    return object.isEmpty() ? VideoConfig() : videoFromJson(object);
}

void ConfigStore::setVideoDefaults(const VideoConfig &config)
{
    writeJson(QStringLiteral("record/video"), videoToJson(config));
    emit settingsChanged();
}

AudioConfig ConfigStore::audioDefaults() const
{
    const QJsonObject object = readJson(QStringLiteral("record/audio"));
    return object.isEmpty() ? AudioConfig() : audioFromJson(object);
}

void ConfigStore::setAudioDefaults(const AudioConfig &config)
{
    writeJson(QStringLiteral("record/audio"), audioToJson(config));
    emit settingsChanged();
}

OverlayConfig ConfigStore::overlayDefaults() const
{
    const QJsonObject object = readJson(QStringLiteral("overlay/config"));
    return object.isEmpty() ? OverlayConfig() : overlayFromJson(object);
}

void ConfigStore::setOverlayDefaults(const OverlayConfig &config)
{
    writeJson(QStringLiteral("overlay/config"), overlayToJson(config));
    emit settingsChanged();
}

WatermarkConfig ConfigStore::watermarkDefaults() const
{
    const QJsonObject object = readJson(QStringLiteral("watermark/config"));
    return object.isEmpty() ? WatermarkConfig() : watermarkFromJson(object);
}

void ConfigStore::setWatermarkDefaults(const WatermarkConfig &config)
{
    writeJson(QStringLiteral("watermark/config"), watermarkToJson(config));
    emit settingsChanged();
}

StreamConfig ConfigStore::streamDefaults() const
{
    const QJsonObject object = readJson(QStringLiteral("stream/config"));
    return object.isEmpty() ? StreamConfig() : streamFromJson(object);
}

void ConfigStore::setStreamDefaults(const StreamConfig &config)
{
    writeJson(QStringLiteral("stream/config"), streamToJson(config));
    emit settingsChanged();
}

QString ConfigStore::hotkeyStartStop() const
{
    return hotkey(QStringLiteral("startStop"));
}

QString ConfigStore::hotkeyPause() const
{
    return hotkey(QStringLiteral("pause"));
}

QString ConfigStore::hotkeyDraw() const
{
    return hotkey(QStringLiteral("draw"));
}

QString ConfigStore::hotkeyHideOverlay() const
{
    return hotkey(QStringLiteral("hideOverlay"));
}

QString ConfigStore::hotkeyCamera() const
{
    return hotkey(QStringLiteral("camera"));
}

QString ConfigStore::hotkeySpotlight() const
{
    return hotkey(QStringLiteral("spotlight"));
}

QString ConfigStore::hotkeyMagnifier() const
{
    return hotkey(QStringLiteral("magnifier"));
}

QString ConfigStore::hotkeyRegion() const
{
    return hotkey(QStringLiteral("region"));
}

void ConfigStore::setHotkey(const QString &action, const QString &sequence)
{
    QSettings().setValue(QStringLiteral("hotkeys/") + action, sequence);
    emit settingsChanged();
}

QString ConfigStore::hotkey(const QString &action) const
{
    static const QHash<QString, QString> defaults = {
        { QStringLiteral("startStop"), QStringLiteral("Ctrl+Alt+R") },
        { QStringLiteral("pause"), QStringLiteral("Ctrl+Alt+P") },
        { QStringLiteral("draw"), QStringLiteral("Ctrl+Alt+D") },
        { QStringLiteral("hideOverlay"), QStringLiteral("Ctrl+Alt+H") },
        { QStringLiteral("camera"), QStringLiteral("Ctrl+Alt+C") },
        { QStringLiteral("spotlight"), QStringLiteral("Ctrl+Alt+S") },
        { QStringLiteral("magnifier"), QStringLiteral("Ctrl+Alt+M") },
        { QStringLiteral("region"), QStringLiteral("Ctrl+Alt+A") }
    };
    return QSettings().value(QStringLiteral("hotkeys/") + action,
                             defaults.value(action)).toString();
}

int ConfigStore::liveServerPort() const
{
    return QSettings().value(QStringLiteral("live/port"), 8088).toInt();
}

void ConfigStore::setLiveServerPort(int port)
{
    QSettings().setValue(QStringLiteral("live/port"), port);
    emit settingsChanged();
}

QString ConfigStore::lastPreset() const
{
    return QSettings().value(QStringLiteral("presets/last")).toString();
}

void ConfigStore::setLastPreset(const QString &name)
{
    QSettings().setValue(QStringLiteral("presets/last"), name);
}

QVariant ConfigStore::value(const QString &key, const QVariant &fallback) const
{
    return QSettings().value(key, fallback);
}

void ConfigStore::setValue(const QString &key, const QVariant &value)
{
    QSettings().setValue(key, value);
    emit settingsChanged();
}

QJsonObject ConfigStore::readJson(const QString &key) const
{
    const QString text = QSettings().value(key).toString();
    if (text.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    return doc.object();
}

void ConfigStore::writeJson(const QString &key, const QJsonObject &object)
{
    QSettings().setValue(key, QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

QJsonObject ConfigStore::videoToJson(const VideoConfig &config)
{
    QJsonObject object;
    object[QStringLiteral("mode")] = static_cast<int>(config.mode);
    object[QStringLiteral("monitorIndex")] = config.monitorIndex;
    object[QStringLiteral("region")] = QJsonArray{ config.region.x(), config.region.y(),
                                                   config.region.width(), config.region.height() };
    object[QStringLiteral("windowId")] = static_cast<qint64>(config.windowId);
    object[QStringLiteral("windowTitle")] = config.windowTitle;
    object[QStringLiteral("codec")] = config.codec;
    object[QStringLiteral("fps")] = config.fps;
    object[QStringLiteral("bitrateKbps")] = config.bitrateKbps;
    object[QStringLiteral("captureCursor")] = config.captureCursor;
    return object;
}

VideoConfig ConfigStore::videoFromJson(const QJsonObject &object)
{
    VideoConfig config;
    const int mode = object.value(QStringLiteral("mode")).toInt(0);
    config.mode = (mode >= 0 && mode <= static_cast<int>(CaptureMode::Region))
                      ? static_cast<CaptureMode>(mode)
                      : CaptureMode::FullScreen;
    config.monitorIndex = qMax(0, object.value(QStringLiteral("monitorIndex")).toInt(0));
    const QJsonArray region = object.value(QStringLiteral("region")).toArray();
    if (region.size() == 4)
        config.region = QRect(region.at(0).toInt(), region.at(1).toInt(),
                              region.at(2).toInt(), region.at(3).toInt());
    config.windowId = static_cast<WId>(object.value(QStringLiteral("windowId")).toVariant().toLongLong());
    config.windowTitle = object.value(QStringLiteral("windowTitle")).toString();
    config.codec = object.value(QStringLiteral("codec")).toString(QStringLiteral("h264")).toLower();
    if (config.codec != QStringLiteral("h265") && config.codec != QStringLiteral("hevc"))
        config.codec = QStringLiteral("h264");
    config.fps = qBound(1, object.value(QStringLiteral("fps")).toInt(30), 120);
    config.bitrateKbps = qBound(500, object.value(QStringLiteral("bitrateKbps")).toInt(8000), 100000);
    config.captureCursor = object.value(QStringLiteral("captureCursor")).toBool(true);
    return config;
}

QJsonObject ConfigStore::audioToJson(const AudioConfig &config)
{
    QJsonObject object;
    object[QStringLiteral("systemEnabled")] = config.systemEnabled;
    object[QStringLiteral("micEnabled")] = config.micEnabled;
    object[QStringLiteral("systemDeviceId")] = config.systemDeviceId;
    object[QStringLiteral("micDeviceId")] = config.micDeviceId;
    object[QStringLiteral("noiseSuppression")] = config.noiseSuppression;
    object[QStringLiteral("micGainDb")] = config.micGainDb;
    object[QStringLiteral("systemGainDb")] = config.systemGainDb;
    return object;
}

AudioConfig ConfigStore::audioFromJson(const QJsonObject &object)
{
    AudioConfig config;
    config.systemEnabled = object.value(QStringLiteral("systemEnabled")).toBool(true);
    config.micEnabled = object.value(QStringLiteral("micEnabled")).toBool(true);
    config.systemDeviceId = object.value(QStringLiteral("systemDeviceId")).toString();
    config.micDeviceId = object.value(QStringLiteral("micDeviceId")).toString();
    config.noiseSuppression = object.value(QStringLiteral("noiseSuppression")).toBool(true);
    config.micGainDb = qBound(-20, object.value(QStringLiteral("micGainDb")).toInt(0), 20);
    config.systemGainDb = qBound(-20, object.value(QStringLiteral("systemGainDb")).toInt(0), 20);
    return config;
}

QJsonObject ConfigStore::overlayToJson(const OverlayConfig &config)
{
    QJsonObject object;
    object[QStringLiteral("penColor")] = colorToString(config.penColor);
    object[QStringLiteral("penWidth")] = config.penWidth;
    object[QStringLiteral("showMouseHighlight")] = config.showMouseHighlight;
    object[QStringLiteral("mouseSize")] = config.mouseSize;
    object[QStringLiteral("leftClickColor")] = colorToString(config.leftClickColor);
    object[QStringLiteral("rightClickColor")] = colorToString(config.rightClickColor);
    object[QStringLiteral("clickDurationMs")] = config.clickDurationMs;
    object[QStringLiteral("showKeys")] = config.showKeys;
    object[QStringLiteral("keyFontSize")] = config.keyFontSize;
    object[QStringLiteral("keyOpacity")] = config.keyOpacity;
    object[QStringLiteral("keyPosition")] = QJsonArray{ config.keyPosition.x(), config.keyPosition.y() };
    object[QStringLiteral("showCamera")] = config.showCamera;
    object[QStringLiteral("cameraDeviceId")] = config.cameraDeviceId;
    object[QStringLiteral("cameraMirror")] = config.cameraMirror;
    object[QStringLiteral("cameraOpacity")] = config.cameraOpacity;
    object[QStringLiteral("cameraRect")] = QJsonArray{ config.cameraRect.x(), config.cameraRect.y(),
                                                       config.cameraRect.width(), config.cameraRect.height() };
    object[QStringLiteral("layout")] = static_cast<int>(config.layout);
    object[QStringLiteral("pptImagePath")] = config.pptImagePath;
    object[QStringLiteral("pptWidth")] = config.pptWidth;
    object[QStringLiteral("pptHeight")] = config.pptHeight;
    object[QStringLiteral("overlaysVisible")] = config.overlaysVisible;
    object[QStringLiteral("spotlightEnabled")] = config.spotlightEnabled;
    object[QStringLiteral("spotlightRect")] = QJsonArray{ config.spotlightRect.x(), config.spotlightRect.y(),
                                                          config.spotlightRect.width(), config.spotlightRect.height() };
    object[QStringLiteral("spotlightDim")] = config.spotlightDim;
    object[QStringLiteral("magnifierEnabled")] = config.magnifierEnabled;
    object[QStringLiteral("magnifierRect")] = QJsonArray{ config.magnifierRect.x(), config.magnifierRect.y(),
                                                          config.magnifierRect.width(), config.magnifierRect.height() };
    object[QStringLiteral("magnifierZoom")] = config.magnifierZoom;
    object[QStringLiteral("magnifierWidth")] = config.magnifierWidth;
    return object;
}

OverlayConfig ConfigStore::overlayFromJson(const QJsonObject &object)
{
    OverlayConfig config;
    config.penColor = colorFromString(object.value(QStringLiteral("penColor")).toString(), config.penColor);
    config.penWidth = qBound(1, object.value(QStringLiteral("penWidth")).toInt(4), 40);
    config.showMouseHighlight = object.value(QStringLiteral("showMouseHighlight")).toBool(true);
    config.mouseSize = qBound(16, object.value(QStringLiteral("mouseSize")).toInt(48), 200);
    config.leftClickColor = colorFromString(object.value(QStringLiteral("leftClickColor")).toString(), config.leftClickColor);
    config.rightClickColor = colorFromString(object.value(QStringLiteral("rightClickColor")).toString(), config.rightClickColor);
    config.clickDurationMs = qBound(120, object.value(QStringLiteral("clickDurationMs")).toInt(600), 3000);
    config.showKeys = object.value(QStringLiteral("showKeys")).toBool(true);
    config.keyFontSize = qBound(10, object.value(QStringLiteral("keyFontSize")).toInt(22), 72);
    config.keyOpacity = qBound(0, object.value(QStringLiteral("keyOpacity")).toInt(85), 100);
    const QJsonArray keyPos = object.value(QStringLiteral("keyPosition")).toArray();
    if (keyPos.size() == 2)
        config.keyPosition = QPoint(keyPos.at(0).toInt(-1), keyPos.at(1).toInt(-1));
    config.showCamera = object.value(QStringLiteral("showCamera")).toBool(false);
    config.cameraDeviceId = object.value(QStringLiteral("cameraDeviceId")).toString();
    config.cameraMirror = object.value(QStringLiteral("cameraMirror")).toBool(true);
    config.cameraOpacity = qBound(20, object.value(QStringLiteral("cameraOpacity")).toInt(100), 100);
    const QJsonArray cameraRect = object.value(QStringLiteral("cameraRect")).toArray();
    if (cameraRect.size() == 4)
        config.cameraRect = QRect(cameraRect.at(0).toInt(), cameraRect.at(1).toInt(),
                                  cameraRect.at(2).toInt(), cameraRect.at(3).toInt());
    const int layout = object.value(QStringLiteral("layout")).toInt(0);
    config.layout = (layout >= 0 && layout <= static_cast<int>(LayoutTemplate::Triple))
                        ? static_cast<LayoutTemplate>(layout)
                        : LayoutTemplate::Single;
    config.pptImagePath = object.value(QStringLiteral("pptImagePath")).toString();
    config.pptWidth = object.value(QStringLiteral("pptWidth")).toInt(640);
    config.pptHeight = object.value(QStringLiteral("pptHeight")).toInt(360);
    config.overlaysVisible = object.value(QStringLiteral("overlaysVisible")).toBool(true);
    config.spotlightEnabled = object.value(QStringLiteral("spotlightEnabled")).toBool(false);
    const QJsonArray spotlightRect = object.value(QStringLiteral("spotlightRect")).toArray();
    if (spotlightRect.size() == 4)
        config.spotlightRect = QRect(spotlightRect.at(0).toInt(), spotlightRect.at(1).toInt(),
                                     spotlightRect.at(2).toInt(), spotlightRect.at(3).toInt());
    config.spotlightDim = qBound(40, object.value(QStringLiteral("spotlightDim")).toInt(140), 220);
    config.magnifierEnabled = object.value(QStringLiteral("magnifierEnabled")).toBool(false);
    const QJsonArray magnifierRect = object.value(QStringLiteral("magnifierRect")).toArray();
    if (magnifierRect.size() == 4)
        config.magnifierRect = QRect(magnifierRect.at(0).toInt(), magnifierRect.at(1).toInt(),
                                     magnifierRect.at(2).toInt(), magnifierRect.at(3).toInt());
    config.magnifierZoom = qBound(1.2, object.value(QStringLiteral("magnifierZoom")).toDouble(2.0), 4.0);
    config.magnifierWidth = qBound(200, object.value(QStringLiteral("magnifierWidth")).toInt(420), 800);
    return config;
}

QJsonObject ConfigStore::watermarkToJson(const WatermarkConfig &config)
{
    QJsonObject object;
    object[QStringLiteral("enabled")] = config.enabled;
    object[QStringLiteral("text")] = config.text;
    object[QStringLiteral("imagePath")] = config.imagePath;
    object[QStringLiteral("position")] = config.position;
    object[QStringLiteral("margin")] = config.margin;
    object[QStringLiteral("fontSize")] = config.fontSize;
    object[QStringLiteral("color")] = colorToString(config.color);
    object[QStringLiteral("opacity")] = config.opacity;
    object[QStringLiteral("scale")] = config.scale;
    return object;
}

WatermarkConfig ConfigStore::watermarkFromJson(const QJsonObject &object)
{
    WatermarkConfig config;
    config.enabled = object.value(QStringLiteral("enabled")).toBool(false);
    config.text = object.value(QStringLiteral("text")).toString();
    config.imagePath = object.value(QStringLiteral("imagePath")).toString();
    config.position = object.value(QStringLiteral("position")).toString(QStringLiteral("bottom-right"));
    config.margin = object.value(QStringLiteral("margin")).toInt(24);
    config.fontSize = object.value(QStringLiteral("fontSize")).toInt(28);
    config.color = colorFromString(object.value(QStringLiteral("color")).toString(), QColor(Qt::white));
    config.opacity = object.value(QStringLiteral("opacity")).toInt(85);
    config.scale = object.value(QStringLiteral("scale")).toDouble(0.18);
    return config;
}

QJsonObject ConfigStore::streamToJson(const StreamConfig &config)
{
    QJsonObject object;
    object[QStringLiteral("enabled")] = config.enabled;
    object[QStringLiteral("serverUrl")] = config.serverUrl;
    object[QStringLiteral("streamKey")] = config.streamKey;
    object[QStringLiteral("saveLocal")] = config.saveLocal;
    return object;
}

StreamConfig ConfigStore::streamFromJson(const QJsonObject &object)
{
    StreamConfig config;
    config.enabled = object.value(QStringLiteral("enabled")).toBool(false);
    config.serverUrl = object.value(QStringLiteral("serverUrl")).toString();
    config.streamKey = object.value(QStringLiteral("streamKey")).toString();
    config.saveLocal = object.value(QStringLiteral("saveLocal")).toBool(true);
    return config;
}

} // namespace lightrec
