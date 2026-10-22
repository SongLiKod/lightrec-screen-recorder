#include "record/RecordingJournal.h"

#include "core/AppPaths.h"
#include "core/ConfigStore.h"
#include "core/Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QUuid>

namespace lightrec {

RecordingJournal::RecordingJournal() = default;

RecordingJournal::~RecordingJournal()
{
    // Never delete an active journal on destruction; it is needed for recovery.
}

bool RecordingJournal::begin(const RecordOptions &options, const QString &partPath,
                             const QString &finalPath, const QSize &outputSize, QString *error)
{
    if (finalPath.isEmpty()) {
        // Streaming-only sessions do not need a recovery journal.
        return true;
    }

    AppPaths::ensureDirectories();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_sessionFile = AppPaths::sessionsDir() + QLatin1Char('/') + id + QStringLiteral(".json");
    m_finalPath = finalPath;

    m_data = QJsonObject();
    m_data[QStringLiteral("version")] = 1;
    m_data[QStringLiteral("status")] = QStringLiteral("recording");
    m_data[QStringLiteral("startedAt")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_data[QStringLiteral("outputPath")] = finalPath;
    m_data[QStringLiteral("partPath")] = partPath;
    m_data[QStringLiteral("lastElapsedMs")] = 0;
    m_data[QStringLiteral("outputSize")] = QJsonArray{ outputSize.width(), outputSize.height() };
    m_data[QStringLiteral("options")] = optionsToJson(options);

    QSaveFile file(m_sessionFile);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法创建录制日志：%1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(m_data).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("无法写入录制日志：%1").arg(file.errorString());
        return false;
    }
    return true;
}

void RecordingJournal::checkpoint(qint64 elapsedMs)
{
    if (m_sessionFile.isEmpty())
        return;
    m_data[QStringLiteral("lastElapsedMs")] = elapsedMs;
    write();
}

void RecordingJournal::write()
{
    if (m_sessionFile.isEmpty())
        return;
    QSaveFile file(m_sessionFile);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(m_data).toJson(QJsonDocument::Compact));
    file.commit();
}

void RecordingJournal::complete()
{
    if (m_sessionFile.isEmpty())
        return;
    QFile::remove(m_sessionFile);
    m_sessionFile.clear();
    m_data = QJsonObject();
}

void RecordingJournal::discard()
{
    complete();
}

QList<JournalEntry> RecordingJournal::pendingSessions()
{
    QList<JournalEntry> result;
    QDir dir(AppPaths::sessionsDir());
    const QStringList files = dir.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Time);
    for (const QString &name : files) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        if (object.value(QStringLiteral("status")).toString() != QStringLiteral("recording"))
            continue;

        JournalEntry entry;
        entry.sessionFile = file.fileName();
        entry.outputPath = object.value(QStringLiteral("outputPath")).toString();
        entry.partPath = object.value(QStringLiteral("partPath")).toString();
        entry.startedAt = QDateTime::fromString(object.value(QStringLiteral("startedAt")).toString(),
                                                Qt::ISODate);
        entry.lastElapsedMs = static_cast<qint64>(object.value(QStringLiteral("lastElapsedMs")).toDouble());
        const QJsonArray size = object.value(QStringLiteral("outputSize")).toArray();
        if (size.size() == 2)
            entry.outputSize = QSize(size.at(0).toInt(), size.at(1).toInt());
        entry.options = optionsFromJson(object.value(QStringLiteral("options")).toObject());
        entry.partExists = !entry.partPath.isEmpty() && QFileInfo::exists(entry.partPath);
        result.append(entry);
    }
    return result;
}

void RecordingJournal::markRecovered(const QString &sessionFile)
{
    if (!sessionFile.isEmpty())
        QFile::remove(sessionFile);
}

bool RecordingJournal::recover(const JournalEntry &entry, QString *recoveredPath, QString *error)
{
    if (!entry.partExists) {
        if (error)
            *error = QStringLiteral("录制缓存文件已丢失，无法恢复");
        return false;
    }

    QString finalPath = entry.outputPath;
    if (finalPath.isEmpty())
        finalPath = entry.partPath;
    if (finalPath.endsWith(QStringLiteral(".part")))
        finalPath.chop(5);
    if (!finalPath.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive))
        finalPath += QStringLiteral(".mp4");

    if (QFileInfo::exists(finalPath)) {
        const QFileInfo info(finalPath);
        finalPath = info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
                    + QStringLiteral("_recovered.mp4");
    }

    bool ok = false;
    const QString ffmpeg = AppPaths::ffmpegExe();
    if (QFileInfo::exists(ffmpeg)) {
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(ffmpeg, {
            QStringLiteral("-hide_banner"), QStringLiteral("-y"),
            QStringLiteral("-err_detect"), QStringLiteral("ignore_err"),
            QStringLiteral("-fflags"), QStringLiteral("+genpts+discardcorrupt"),
            QStringLiteral("-i"), entry.partPath,
            QStringLiteral("-map"), QStringLiteral("0:v?"),
            QStringLiteral("-map"), QStringLiteral("0:a?"),
            QStringLiteral("-c"), QStringLiteral("copy"),
            QStringLiteral("-movflags"), QStringLiteral("+faststart"),
            finalPath
        });
        if (process.waitForStarted(5000) && process.waitForFinished(10 * 60 * 1000))
            ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0
                 && QFileInfo::exists(finalPath) && QFileInfo(finalPath).size() > 0;
        if (!ok)
            QFile::remove(finalPath);
    }

    if (!ok) {
        // The .part file is a fragmented MP4 and is usually still playable.
        ok = QFile::copy(entry.partPath, finalPath);
    }

    if (!ok) {
        if (error)
            *error = QStringLiteral("无法修复录制缓存，请手动尝试视频修复工具");
        return false;
    }

    QFile::remove(entry.partPath);
    markRecovered(entry.sessionFile);
    if (recoveredPath)
        *recoveredPath = finalPath;
    qInfo() << "Recovered recording:" << finalPath;
    return true;
}

QJsonObject RecordingJournal::optionsToJson(const RecordOptions &options)
{
    QJsonObject object;
    object[QStringLiteral("video")] = ConfigStore::videoToJson(options.video);
    object[QStringLiteral("audio")] = ConfigStore::audioToJson(options.audio);
    object[QStringLiteral("overlay")] = ConfigStore::overlayToJson(options.overlay);
    object[QStringLiteral("watermark")] = ConfigStore::watermarkToJson(options.watermark);
    object[QStringLiteral("stream")] = ConfigStore::streamToJson(options.stream);
    object[QStringLiteral("outputPath")] = options.outputPath;
    object[QStringLiteral("presetName")] = options.presetName;
    return object;
}

RecordOptions RecordingJournal::optionsFromJson(const QJsonObject &object)
{
    RecordOptions options;
    options.video = ConfigStore::videoFromJson(object.value(QStringLiteral("video")).toObject());
    options.audio = ConfigStore::audioFromJson(object.value(QStringLiteral("audio")).toObject());
    options.overlay = ConfigStore::overlayFromJson(object.value(QStringLiteral("overlay")).toObject());
    options.watermark = ConfigStore::watermarkFromJson(object.value(QStringLiteral("watermark")).toObject());
    options.stream = ConfigStore::streamFromJson(object.value(QStringLiteral("stream")).toObject());
    options.outputPath = object.value(QStringLiteral("outputPath")).toString();
    options.presetName = object.value(QStringLiteral("presetName")).toString();
    return options;
}

} // namespace lightrec
