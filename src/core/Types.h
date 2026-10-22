#pragma once

#include <QColor>
#include <QDateTime>
#include <QMetaType>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QWindow>
#include <QStringList>
#include <QTime>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

namespace lightrec {

enum class CaptureMode {
    FullScreen = 0,
    Window = 1,
    Region = 2
};

enum class LayoutTemplate {
    Single = 0,   // screen + draggable camera PiP
    Split = 1,    // screen + camera side by side
    Triple = 2    // screen + camera + PPT auxiliary area
};

enum class ThemeMode {
    System = 0,
    Light = 1,
    Dark = 2
};

enum class RecordState {
    Idle = 0,
    Starting = 1,
    Recording = 2,
    Paused = 3,
    Stopping = 4
};

enum class AnnotationTool {
    None = 0,
    Pen = 1,
    Line = 2,
    Arrow = 3,
    Rect = 4,
    Ellipse = 5,
    Text = 6
};

struct MonitorInfo {
    int index = 0;
    QRect geometry;
    QString name;
    bool primary = false;
    qreal devicePixelRatio = 1.0;
};

struct WindowInfo {
    WId id = 0;
    QString title;
    QString processName;
    QRect rect;
};

struct AudioDeviceInfo {
    QString id;
    QString name;
    bool isDefault = false;
    bool loopback = false;
};

struct VideoConfig {
    CaptureMode mode = CaptureMode::FullScreen;
    int monitorIndex = 0;
    QRect region;
    WId windowId = 0;
    QString windowTitle;
    QString codec = QStringLiteral("h264");
    int fps = 30;
    int bitrateKbps = 8000;
    bool captureCursor = true;
};

using CaptureConfig = VideoConfig;

struct AudioConfig {
    bool systemEnabled = true;
    bool micEnabled = true;
    QString systemDeviceId;   // empty = default render device
    QString micDeviceId;      // empty = default capture device
    bool noiseSuppression = true;
    int micGainDb = 0;
    int systemGainDb = 0;
};

struct OverlayConfig {
    QColor penColor = QColor(0xE8, 0x3B, 0x3B);
    int penWidth = 4;

    bool showMouseHighlight = true;
    int mouseSize = 48;
    QColor leftClickColor = QColor(0x2D, 0x8C, 0xF0);
    QColor rightClickColor = QColor(0xFF, 0x95, 0x00);
    int clickDurationMs = 600;

    bool showKeys = true;
    int keyFontSize = 22;
    int keyOpacity = 85;
    QPoint keyPosition = QPoint(-1, -1); // global position, -1 = default bottom-left

    bool showCamera = false;
    QString cameraDeviceId;
    bool cameraMirror = true;
    int cameraOpacity = 100;
    QRect cameraRect = QRect(0, 0, 360, 202);
    LayoutTemplate layout = LayoutTemplate::Single;
    QString pptImagePath;
    int pptWidth = 640;
    int pptHeight = 360;

    bool overlaysVisible = true;

    // Local highlight / spotlight and magnifier (EV-style quick tools).
    bool spotlightEnabled = false;
    QRect spotlightRect;
    int spotlightDim = 140;

    bool magnifierEnabled = false;
    QRect magnifierRect;
    double magnifierZoom = 2.0;
    int magnifierWidth = 420;
};

struct WatermarkConfig {
    bool enabled = false;
    QString text;
    QString imagePath;
    QString position = QStringLiteral("bottom-right");
    int margin = 24;
    int fontSize = 28;
    QColor color = QColor(Qt::white);
    int opacity = 85;
    double scale = 0.18;
};

struct StreamConfig {
    bool enabled = false;
    QString serverUrl;
    QString streamKey;
    bool saveLocal = true;

    QString fullUrl() const
    {
        if (serverUrl.isEmpty())
            return QString();
        if (streamKey.isEmpty())
            return serverUrl;
        if (serverUrl.endsWith(QLatin1Char('/')))
            return serverUrl + streamKey;
        return serverUrl + QLatin1Char('/') + streamKey;
    }
};

struct RecordOptions {
    VideoConfig video;
    AudioConfig audio;
    OverlayConfig overlay;
    StreamConfig stream;
    WatermarkConfig watermark;
    QString outputPath;        // empty when streaming only
    QString presetName;
    bool minimizeOnStart = true;
};

struct Preset {
    QString name = QStringLiteral("默认预设");
    VideoConfig video;
    AudioConfig audio;
    OverlayConfig overlay;
    WatermarkConfig watermark;
    StreamConfig stream;
    QString createdAt;
    QString updatedAt;
};

struct ScheduleTask {
    QString id;
    QString name;
    QTime startTime = QTime(9, 0);
    QTime stopTime = QTime(10, 0);
    QString presetName;
    bool enabled = true;
    bool autoStop = true;
    QString lastRunDate;
};

struct RecordingFileInfo {
    QString path;
    QString fileName;
    qint64 sizeBytes = 0;
    qint64 durationMs = 0;
    QDateTime created;
    bool broken = false;
    int width = 0;
    int height = 0;
    QString codec;
};

inline QString captureModeName(CaptureMode mode)
{
    switch (mode) {
    case CaptureMode::FullScreen: return QStringLiteral("全屏");
    case CaptureMode::Window: return QStringLiteral("窗口");
    case CaptureMode::Region: return QStringLiteral("选区");
    }
    return QString();
}

inline QString recordStateName(RecordState state)
{
    switch (state) {
    case RecordState::Idle: return QStringLiteral("空闲");
    case RecordState::Starting: return QStringLiteral("准备中");
    case RecordState::Recording: return QStringLiteral("录制中");
    case RecordState::Paused: return QStringLiteral("已暂停");
    case RecordState::Stopping: return QStringLiteral("停止中");
    }
    return QString();
}

inline QString formatDuration(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    const qint64 totalSeconds = ms / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

inline QString formatSize(qint64 bytes)
{
    const double kb = 1024.0;
    if (bytes < kb)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < kb * kb)
        return QStringLiteral("%1 KB").arg(bytes / kb, 0, 'f', 1);
    if (bytes < kb * kb * kb)
        return QStringLiteral("%1 MB").arg(bytes / (kb * kb), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / (kb * kb * kb), 0, 'f', 2);
}

} // namespace lightrec

Q_DECLARE_METATYPE(lightrec::CaptureMode)
Q_DECLARE_METATYPE(lightrec::LayoutTemplate)
Q_DECLARE_METATYPE(lightrec::ThemeMode)
Q_DECLARE_METATYPE(lightrec::RecordState)
Q_DECLARE_METATYPE(lightrec::AnnotationTool)
Q_DECLARE_METATYPE(lightrec::OverlayConfig)
Q_DECLARE_METATYPE(lightrec::RecordOptions)
Q_DECLARE_METATYPE(lightrec::ScheduleTask)
Q_DECLARE_METATYPE(lightrec::RecordingFileInfo)
