#pragma once

#include "core/Types.h"

#include <QList>
#include <QObject>
#include <QStringList>

namespace lightrec {

class PresetManager : public QObject
{
    Q_OBJECT
public:
    explicit PresetManager(QObject *parent = nullptr);

    QList<Preset> presets() const;
    QStringList names() const;
    Preset load(const QString &name) const;
    bool exists(const QString &name) const;

    bool save(const Preset &preset, QString *error);
    bool remove(const QString &name, QString *error);

    static Preset fromOptions(const RecordOptions &options, const QString &name);
    static RecordOptions toOptions(const Preset &preset);

    void ensureDefaultPreset();

    static QString directory();

signals:
    void presetsChanged();

private:
    static QString filePath(const QString &name);
    Preset readFile(const QString &path) const;
};

} // namespace lightrec
