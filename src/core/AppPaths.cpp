#include "core/AppPaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace lightrec {

QString AppPaths::configDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir.isEmpty() ? QDir::homePath() + QStringLiteral("/.lightrec") : dir;
}

QString AppPaths::presetsDir()
{
    return configDir() + QStringLiteral("/presets");
}

QString AppPaths::sessionsDir()
{
    return configDir() + QStringLiteral("/sessions");
}

QString AppPaths::logsDir()
{
    return configDir() + QStringLiteral("/logs");
}

QString AppPaths::defaultOutputDir()
{
    QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (videos.isEmpty())
        videos = QDir::homePath() + QStringLiteral("/Videos");
    return videos + QStringLiteral("/LightRec");
}

QString AppPaths::outputDir()
{
    QSettings settings;
    const QString dir = settings.value(QStringLiteral("output/directory")).toString();
    return dir.isEmpty() ? defaultOutputDir() : dir;
}

void AppPaths::setOutputDir(const QString &dir)
{
    QSettings settings;
    settings.setValue(QStringLiteral("output/directory"), dir);
}

QString AppPaths::ffmpegExe()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/ffmpeg.exe"),
        appDir + QStringLiteral("/ffmpeg/ffmpeg.exe"),
        configDir() + QStringLiteral("/ffmpeg/ffmpeg.exe"),
        appDir + QStringLiteral("/../ffmpeg/ffmpeg.exe"),
        appDir + QStringLiteral("/../third_party/ffmpeg/bin/ffmpeg.exe"),
        QStringLiteral("ffmpeg.exe")
    };
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile())
            return info.absoluteFilePath();
    }
    return QStringLiteral("ffmpeg.exe");
}

QString AppPaths::bundledResource(const QString &relative)
{
    return QCoreApplication::applicationDirPath() + QLatin1Char('/') + relative;
}

void AppPaths::ensureDirectories()
{
    QDir().mkpath(configDir());
    QDir().mkpath(presetsDir());
    QDir().mkpath(sessionsDir());
    QDir().mkpath(logsDir());
    QDir().mkpath(outputDir());
}

QString AppPaths::uniqueOutputPath(const QString &dir, const QString &prefix)
{
    QDir directory(dir);
    directory.mkpath(QStringLiteral("."));
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString base = prefix + QLatin1Char('_') + stamp;
    QString path = directory.filePath(base + QStringLiteral(".mp4"));
    int counter = 2;
    while (QFileInfo::exists(path)) {
        path = directory.filePath(QStringLiteral("%1_%2.mp4").arg(base).arg(counter++));
    }
    return path;
}

QString AppPaths::sanitizeFileName(const QString &name)
{
    QString result = name;
    static const QString invalid = QStringLiteral("\\/:*?\"<>|");
    for (const QChar &ch : invalid)
        result.replace(ch, QLatin1Char('_'));
    for (QChar &ch : result) {
        if (ch.unicode() < 0x20)
            ch = QLatin1Char('_');
    }
    result = result.trimmed();
    while (result.endsWith(QLatin1Char('.')) || result.endsWith(QLatin1Char(' ')))
        result.chop(1);
    if (result.isEmpty())
        result = QStringLiteral("LightRec");

    // Windows reserves these device names even when an extension is appended.
    static const QStringList reserved = {
        QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
        QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
        QStringLiteral("LPT8"), QStringLiteral("LPT9")
    };
    if (reserved.contains(result.toUpper()))
        result.prepend(QLatin1Char('_'));
    if (result.size() > 120)
        result.truncate(120);
    return result;
}

} // namespace lightrec
