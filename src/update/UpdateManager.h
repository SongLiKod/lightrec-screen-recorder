#pragma once

#include <QObject>
#include <QString>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace lightrec {

/**
 * In-app update checker/downloader for GitHub Releases (or a custom JSON
 * manifest via LIGHTREC_UPDATE_URL).  Installation is handed to a hidden
 * PowerShell helper that waits for LightRec to exit, replaces the portable
 * application files, and restarts LightRec without opening a browser/installer.
 */
class UpdateManager : public QObject
{
    Q_OBJECT
public:
    explicit UpdateManager(QObject *parent = nullptr);
    ~UpdateManager() override;

    void checkForUpdates();
    bool isBusy() const { return m_busy; }

    static QString currentVersion();
    static int compareVersions(const QString &left, const QString &right);
    static bool installAndRestart(const QString &archivePath, QString *error);

signals:
    void statusChanged(const QString &message);
    void progressChanged(qint64 received, qint64 total);
    void updateAvailable(const QString &version, const QString &notes);
    void finished(bool success, const QString &message, const QString &archivePath);

private:
    void parseManifest(const QByteArray &data);
    void startDownload();
    void handleDownloadFinished();
    void fail(const QString &message);

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile *m_archive = nullptr;
    QString m_archivePath;
    QString m_remoteVersion;
    QString m_downloadUrl;
    QString m_sha256;
    QString m_notes;
    bool m_busy = false;
    bool m_cancelled = false;
};

} // namespace lightrec
