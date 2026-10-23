#include "record/RecordingLibrary.h"

#include "core/AppPaths.h"
#include "media/MediaProbe.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace lightrec {

RecordingLibrary::RecordingLibrary(QObject *parent)
    : QObject(parent)
{
}

QString RecordingLibrary::outputDir() const
{
    return AppPaths::outputDir();
}

QList<RecordingFileInfo> RecordingLibrary::scan() const
{
    QList<RecordingFileInfo> result;
    QDir dir(AppPaths::outputDir());
    if (!dir.exists())
        return result;

    const QFileInfoList files = dir.entryInfoList({ QStringLiteral("*.mp4") }, QDir::Files, QDir::Time);
    for (const QFileInfo &file : files) {
        if (file.fileName().endsWith(QStringLiteral(".part"), Qt::CaseInsensitive))
            continue;
        RecordingFileInfo info = MediaProbe::fileInfo(file.absoluteFilePath());
        result.append(info);
    }
    return result;
}

bool RecordingLibrary::renameRecording(const QString &path, const QString &newBaseName,
                                       QString *newPath, QString *error)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        if (error)
            *error = QStringLiteral("文件不存在");
        return false;
    }

    QString base = AppPaths::sanitizeFileName(newBaseName);
    if (base.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive))
        base.chop(4);
    const QString target = info.absolutePath() + QLatin1Char('/') + base + QStringLiteral(".mp4");
    if (QFileInfo::exists(target) && target.compare(path, Qt::CaseInsensitive) != 0) {
        if (error)
            *error = QStringLiteral("同名文件已存在");
        return false;
    }
    if (!QFile::rename(path, target)) {
        if (error)
            *error = QStringLiteral("重命名失败，请检查文件是否被占用");
        return false;
    }
    if (newPath)
        *newPath = target;
    emit changed();
    return true;
}

bool RecordingLibrary::deleteRecording(const QString &path, QString *error)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        if (error)
            *error = QStringLiteral("文件不存在");
        return false;
    }
    if (!QFile::remove(path)) {
        if (error)
            *error = QStringLiteral("删除失败，请检查文件是否被占用");
        return false;
    }
    emit changed();
    return true;
}

QString RecordingLibrary::suggestedName(const QString &path) const
{
    return QFileInfo(path).completeBaseName();
}

} // namespace lightrec
