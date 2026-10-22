#pragma once

#include <QObject>
#include <QString>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

namespace lightrec {

/**
 * One-click FFmpeg runtime installer.  Downloads the official BtbN 7.1 GPL
 * shared build through a mirror, extracts it silently and installs
 * ffmpeg.exe + shared DLLs into the per-user LightRec directory.
 */
class FFmpegInstaller : public QObject
{
    Q_OBJECT
public:
    explicit FFmpegInstaller(QObject *parent = nullptr);
    ~FFmpegInstaller() override;

    static QString installDir();
    static bool isInstalled();
    static QString installedExecutable();

    void install();
    void cancel();
    bool isBusy() const { return m_reply != nullptr || m_process != nullptr; }

signals:
    void statusChanged(const QString &text);
    void progressChanged(qint64 received, qint64 total);
    void logLine(const QString &line);
    void finished(bool success, const QString &message);

private:
    void startDownload(const QString &url);
    void handleDownloadFinished();
    void extractArchive();
    void handleExtractFinished(int exitCode);
    void installFromDirectory(const QString &directory);
    void fail(const QString &message);
    void cleanup();

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile *m_archive = nullptr;
    QProcess *m_process = nullptr;
    QString m_archivePath;
    QString m_extractDir;
    bool m_cancelled = false;
};

} // namespace lightrec
