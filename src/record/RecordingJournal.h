#pragma once

#include "core/Types.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QSize>
#include <QString>

namespace lightrec {

struct JournalEntry {
    QString sessionFile;
    QString outputPath;
    QString partPath;
    QDateTime startedAt;
    qint64 lastElapsedMs = 0;
    QSize outputSize;
    RecordOptions options;
    bool partExists = false;
};

/**
 * Crash-safe recording journal.  A fragmented MP4 is written to "<output>.part"
 * and a small JSON journal is atomically updated while recording.  If the app
 * is killed, the journal survives and the recording can be recovered on the
 * next launch.
 */
class RecordingJournal
{
public:
    RecordingJournal();
    ~RecordingJournal();

    bool begin(const RecordOptions &options, const QString &partPath,
               const QString &finalPath, const QSize &outputSize, QString *error);
    void checkpoint(qint64 elapsedMs);
    void complete();
    void discard();

    static QList<JournalEntry> pendingSessions();
    static void markRecovered(const QString &sessionFile);
    static bool recover(const JournalEntry &entry, QString *recoveredPath, QString *error);

    static QJsonObject optionsToJson(const RecordOptions &options);
    static RecordOptions optionsFromJson(const QJsonObject &object);

private:
    void write();

    QString m_sessionFile;
    QJsonObject m_data;
    QString m_finalPath;
};

} // namespace lightrec
