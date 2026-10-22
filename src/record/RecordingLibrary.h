#pragma once

#include "core/Types.h"

#include <QObject>
#include <QString>

namespace lightrec {

class RecordingLibrary : public QObject
{
    Q_OBJECT
public:
    explicit RecordingLibrary(QObject *parent = nullptr);

    QString outputDir() const;
    QList<RecordingFileInfo> scan() const;

    bool renameRecording(const QString &path, const QString &newBaseName,
                         QString *newPath, QString *error);
    bool deleteRecording(const QString &path, QString *error);
    QString suggestedName(const QString &path) const;

signals:
    void changed();

private:
    QString m_outputDir;
};

} // namespace lightrec
