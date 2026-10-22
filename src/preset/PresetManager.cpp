#include "preset/PresetManager.h"

#include "core/AppPaths.h"
#include "core/ConfigStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace lightrec {

PresetManager::PresetManager(QObject *parent)
    : QObject(parent)
{
    ensureDefaultPreset();
}

QString PresetManager::directory()
{
    return AppPaths::presetsDir();
}

QString PresetManager::filePath(const QString &name)
{
    return directory() + QLatin1Char('/') + AppPaths::sanitizeFileName(name) + QStringLiteral(".json");
}

QList<Preset> PresetManager::presets() const
{
    QList<Preset> result;
    QDir dir(directory());
    const QFileInfoList files = dir.entryInfoList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name);
    for (const QFileInfo &file : files) {
        const Preset preset = readFile(file.absoluteFilePath());
        if (!preset.name.isEmpty())
            result.append(preset);
    }
    return result;
}

QStringList PresetManager::names() const
{
    QStringList result;
    for (const Preset &preset : presets())
        result.append(preset.name);
    return result;
}

Preset PresetManager::load(const QString &name) const
{
    return readFile(filePath(name));
}

bool PresetManager::exists(const QString &name) const
{
    return QFileInfo::exists(filePath(name));
}

Preset PresetManager::readFile(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Preset();
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    Preset preset;
    preset.name = object.value(QStringLiteral("name")).toString();
    preset.video = ConfigStore::videoFromJson(object.value(QStringLiteral("video")).toObject());
    preset.audio = ConfigStore::audioFromJson(object.value(QStringLiteral("audio")).toObject());
    preset.overlay = ConfigStore::overlayFromJson(object.value(QStringLiteral("overlay")).toObject());
    preset.watermark = ConfigStore::watermarkFromJson(object.value(QStringLiteral("watermark")).toObject());
    preset.stream = ConfigStore::streamFromJson(object.value(QStringLiteral("stream")).toObject());
    preset.createdAt = object.value(QStringLiteral("createdAt")).toString();
    preset.updatedAt = object.value(QStringLiteral("updatedAt")).toString();
    return preset;
}

bool PresetManager::save(const Preset &preset, QString *error)
{
    if (preset.name.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("预设名称不能为空");
        return false;
    }

    AppPaths::ensureDirectories();
    QJsonObject object;
    object[QStringLiteral("name")] = preset.name;
    object[QStringLiteral("video")] = ConfigStore::videoToJson(preset.video);
    object[QStringLiteral("audio")] = ConfigStore::audioToJson(preset.audio);
    object[QStringLiteral("overlay")] = ConfigStore::overlayToJson(preset.overlay);
    object[QStringLiteral("watermark")] = ConfigStore::watermarkToJson(preset.watermark);
    object[QStringLiteral("stream")] = ConfigStore::streamToJson(preset.stream);
    object[QStringLiteral("createdAt")] = preset.createdAt.isEmpty()
                                              ? QDateTime::currentDateTime().toString(Qt::ISODate)
                                              : preset.createdAt;
    object[QStringLiteral("updatedAt")] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QSaveFile file(filePath(preset.name));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入预设文件");
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("预设保存失败");
        return false;
    }
    emit presetsChanged();
    return true;
}

bool PresetManager::remove(const QString &name, QString *error)
{
    if (!QFile::remove(filePath(name))) {
        if (error)
            *error = QStringLiteral("预设不存在或无法删除");
        return false;
    }
    emit presetsChanged();
    return true;
}

Preset PresetManager::fromOptions(const RecordOptions &options, const QString &name)
{
    Preset preset;
    preset.name = name;
    preset.video = options.video;
    preset.audio = options.audio;
    preset.overlay = options.overlay;
    preset.watermark = options.watermark;
    preset.stream = options.stream;
    preset.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    return preset;
}

RecordOptions PresetManager::toOptions(const Preset &preset)
{
    RecordOptions options;
    options.video = preset.video;
    options.audio = preset.audio;
    options.overlay = preset.overlay;
    options.watermark = preset.watermark;
    options.stream = preset.stream;
    options.presetName = preset.name;
    return options;
}

void PresetManager::ensureDefaultPreset()
{
    AppPaths::ensureDirectories();
    if (!presets().isEmpty())
        return;

    Preset preset;
    preset.name = QStringLiteral("默认预设");
    preset.video = ConfigStore::instance().videoDefaults();
    preset.audio = ConfigStore::instance().audioDefaults();
    preset.overlay = ConfigStore::instance().overlayDefaults();
    preset.watermark = ConfigStore::instance().watermarkDefaults();
    preset.stream = ConfigStore::instance().streamDefaults();
    QString error;
    save(preset, &error);
}

} // namespace lightrec
