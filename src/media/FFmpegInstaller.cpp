#include "media/FFmpegInstaller.h"

#include "core/AppPaths.h"
#include "core/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace lightrec {

namespace {
const char *kPrimaryUrl =
    "https://ghproxy.net/https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/"
    "ffmpeg-master-latest-win64-gpl-shared.zip";
const char *kFallbackUrl =
    "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/"
    "ffmpeg-master-latest-win64-gpl-shared.zip";

QString quotePowerShell(const QString &value)
{
    return QLatin1Char('\'') + QString(value).replace(QLatin1Char('\''), QStringLiteral("''"))
           + QLatin1Char('\'');
}
} // namespace

FFmpegInstaller::FFmpegInstaller(QObject *parent)
    : QObject(parent)
{
}

FFmpegInstaller::~FFmpegInstaller()
{
    cancel();
    cleanup();
}

QString FFmpegInstaller::installDir()
{
    return AppPaths::configDir() + QStringLiteral("/ffmpeg");
}

QString FFmpegInstaller::installedExecutable()
{
    const QStringList candidates = {
        installDir() + QStringLiteral("/ffmpeg.exe"),
        AppPaths::configDir() + QStringLiteral("/ffmpeg/ffmpeg.exe")
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QString();
}

bool FFmpegInstaller::isInstalled()
{
    return !installedExecutable().isEmpty();
}

void FFmpegInstaller::install()
{
    if (isBusy())
        return;
    m_cancelled = false;
    m_archivePath = QDir::tempPath() + QStringLiteral("/lightrec_ffmpeg_runtime.zip");
    m_extractDir = QDir::tempPath() + QStringLiteral("/lightrec_ffmpeg_runtime");
    QFile::remove(m_archivePath);
    QDir(m_extractDir).removeRecursively();
    QDir().mkpath(m_extractDir);

    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
        connect(m_network, &QNetworkAccessManager::finished, this, [this](QNetworkReply *reply) {
            if (reply == m_reply)
                handleDownloadFinished();
        });
    }

    emit statusChanged(QStringLiteral("正在下载 FFmpeg 运行时…"));
    startDownload(QString::fromLatin1(kPrimaryUrl));
}

void FFmpegInstaller::startDownload(const QString &url)
{
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_archive) {
        m_archive->close();
        delete m_archive;
        m_archive = nullptr;
    }

    m_archive = new QFile(m_archivePath, this);
    if (!m_archive->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(QStringLiteral("无法创建下载缓存：%1").arg(m_archive->errorString()));
        return;
    }

    QNetworkRequest request{ QUrl(url) };
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LightRec/%1").arg(QCoreApplication::applicationVersion()));
    m_reply = m_network->get(request);

    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (m_reply && m_archive)
            m_archive->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                emit progressChanged(received, total);
            });
    connect(m_reply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError) {
        if (m_cancelled)
            return;
        // Retry once through the direct GitHub URL.
        if (m_reply && m_reply->url().toString().contains(QStringLiteral("ghproxy"))) {
            emit logLine(QStringLiteral("镜像下载失败，尝试直连 GitHub…"));
            startDownload(QString::fromLatin1(kFallbackUrl));
            return;
        }
        fail(QStringLiteral("下载失败：%1").arg(m_reply ? m_reply->errorString() : QString()));
    });
}

void FFmpegInstaller::handleDownloadFinished()
{
    if (!m_reply || m_cancelled)
        return;
    if (m_reply->error() != QNetworkReply::NoError)
        return; // errorOccurred handles retry/failure

    if (m_archive) {
        m_archive->write(m_reply->readAll());
        m_archive->close();
        delete m_archive;
        m_archive = nullptr;
    }
    m_reply->deleteLater();
    m_reply = nullptr;

    if (QFileInfo(m_archivePath).size() < 1024 * 1024) {
        fail(QStringLiteral("下载文件不完整，请检查网络后重试"));
        return;
    }

    emit statusChanged(QStringLiteral("正在解压 FFmpeg…"));
    extractArchive();
}

void FFmpegInstaller::extractArchive()
{
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) { handleExtractFinished(exitCode); });

    const QString command = QStringLiteral(
        "Expand-Archive -LiteralPath %1 -DestinationPath %2 -Force")
        .arg(quotePowerShell(m_archivePath), quotePowerShell(m_extractDir));
    m_process->start(QStringLiteral("powershell.exe"),
                     { QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                       QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                       QStringLiteral("-Command"), command });
}

void FFmpegInstaller::handleExtractFinished(int exitCode)
{
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (m_cancelled)
        return;
    if (exitCode != 0) {
        fail(QStringLiteral("解压 FFmpeg 失败（退出码 %1）").arg(exitCode));
        return;
    }

    QString binDirectory;
    QDirIterator iterator(m_extractDir, { QStringLiteral("ffmpeg.exe") }, QDir::Files,
                          QDirIterator::Subdirectories);
    if (iterator.hasNext())
        binDirectory = QFileInfo(iterator.next()).absolutePath();

    if (binDirectory.isEmpty()) {
        fail(QStringLiteral("安装包中未找到 ffmpeg.exe"));
        return;
    }
    installFromDirectory(binDirectory);
}

void FFmpegInstaller::installFromDirectory(const QString &directory)
{
    QDir target(installDir());
    if (!target.exists() && !QDir().mkpath(installDir())) {
        fail(QStringLiteral("无法创建 FFmpeg 安装目录：%1").arg(installDir()));
        return;
    }

    const QFileInfoList files = QDir(directory).entryInfoList(
        { QStringLiteral("ffmpeg.exe"), QStringLiteral("avcodec*.dll"),
          QStringLiteral("avformat*.dll"), QStringLiteral("avutil*.dll"),
          QStringLiteral("swscale*.dll"), QStringLiteral("swresample*.dll"),
          QStringLiteral("avdevice*.dll"), QStringLiteral("avfilter*.dll"),
          QStringLiteral("postproc*.dll") },
        QDir::Files);
    if (files.isEmpty()) {
        fail(QStringLiteral("安装包内容为空"));
        return;
    }

    for (const QFileInfo &file : files) {
        const QString destination = target.filePath(file.fileName());
        QFile::remove(destination);
        if (!QFile::copy(file.absoluteFilePath(), destination)) {
            fail(QStringLiteral("无法写入 %1").arg(destination));
            return;
        }
    }

    // Portable installs can also place the runtime next to the executable so
    // the linked FFmpeg libraries pick it up on the next start.
    const QString appDir = QCoreApplication::applicationDirPath();
    if (QFileInfo(appDir).isWritable()) {
        for (const QFileInfo &file : files) {
            const QString destination = appDir + QLatin1Char('/') + file.fileName();
            if (QFileInfo::exists(destination))
                continue;
            QFile::copy(file.absoluteFilePath(), destination);
        }
    }

    emit statusChanged(QStringLiteral("FFmpeg 运行时已安装"));
    emit finished(true, QStringLiteral("FFmpeg 已安装到：\n%1\n\n视频工具箱可立即使用；"
                                       "如需完整硬件编码支持，请重启 LightRec。")
                            .arg(installDir()));
    cleanup();
}

void FFmpegInstaller::cancel()
{
    m_cancelled = true;
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_archive) {
        m_archive->close();
        delete m_archive;
        m_archive = nullptr;
    }
    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    emit statusChanged(QStringLiteral("已取消"));
}

void FFmpegInstaller::fail(const QString &message)
{
    if (m_cancelled)
        return;
    qWarning() << "FFmpeg installer:" << message;
    emit statusChanged(message);
    emit finished(false, message);
    cleanup();
}

void FFmpegInstaller::cleanup()
{
    QFile::remove(m_archivePath);
    QDir(m_extractDir).removeRecursively();
}

} // namespace lightrec
