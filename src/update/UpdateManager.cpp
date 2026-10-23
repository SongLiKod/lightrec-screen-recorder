#include "update/UpdateManager.h"

#include "lightrec_version.h"

#include <QCoreApplication>
#include <qcryptographichash.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace lightrec {

namespace {

const char *kGitHubLatest =
    "https://api.github.com/repos/SongLiKod/lightrec-screen-recorder/releases/latest";

QString normalizedVersion(QString version)
{
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        version.remove(0, 1);
    return version;
}

QString powerShellQuote(const QString &value)
{
    return QLatin1Char('\'') + QString(value).replace(QLatin1Char('\''), QStringLiteral("''"))
           + QLatin1Char('\'');
}

QString commandLineQuote(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

} // namespace

UpdateManager::UpdateManager(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

UpdateManager::~UpdateManager()
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
}

QString UpdateManager::currentVersion()
{
    return QString::fromLatin1(LIGHTREC_VERSION_STRING);
}

int UpdateManager::compareVersions(const QString &left, const QString &right)
{
    const QStringList a = normalizedVersion(left).split(QLatin1Char('.'));
    const QStringList b = normalizedVersion(right).split(QLatin1Char('.'));
    const int count = std::max(a.size(), b.size());
    for (int i = 0; i < count; ++i) {
        const int av = i < a.size() ? a.at(i).toInt() : 0;
        const int bv = i < b.size() ? b.at(i).toInt() : 0;
        if (av != bv)
            return av < bv ? -1 : 1;
    }
    return 0;
}

void UpdateManager::checkForUpdates()
{
    if (m_busy)
        return;
    m_busy = true;
    m_cancelled = false;
    m_remoteVersion.clear();
    m_downloadUrl.clear();
    m_sha256.clear();
    m_notes.clear();
    m_archivePath.clear();

    QString url = qEnvironmentVariable("LIGHTREC_UPDATE_URL").trimmed();
    if (url.isEmpty())
        url = QString::fromLatin1(kGitHubLatest);

    emit statusChanged(QStringLiteral("正在检查更新…"));
    QNetworkRequest request{ QUrl(url) };
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LightRec/%1").arg(currentVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json, application/json");
    m_reply = m_network->get(request);

    connect(m_reply, &QNetworkReply::finished, this, [this] {
        if (!m_reply)
            return;
        const QByteArray body = m_reply->readAll();
        const QNetworkReply::NetworkError error = m_reply->error();
        const QString errorText = m_reply->errorString();
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_reply->deleteLater();
        m_reply = nullptr;

        if (m_cancelled)
            return;
        if (error != QNetworkReply::NoError) {
            if (status == 404)
                fail(QStringLiteral("尚未发布可用的更新包"));
            else
                fail(QStringLiteral("检查更新失败：%1").arg(errorText));
            return;
        }
        parseManifest(body);
    });
}

void UpdateManager::parseManifest(const QByteArray &data)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QStringLiteral("更新清单格式不正确"));
        return;
    }

    const QJsonObject object = document.object();
    m_remoteVersion = normalizedVersion(object.value(QStringLiteral("tag_name")).toString());
    if (m_remoteVersion.isEmpty())
        m_remoteVersion = normalizedVersion(object.value(QStringLiteral("version")).toString());
    m_notes = object.value(QStringLiteral("body")).toString();
    if (m_notes.isEmpty())
        m_notes = object.value(QStringLiteral("notes")).toString();
    m_sha256 = object.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    m_downloadUrl = object.value(QStringLiteral("url")).toString().trimmed();

    // GitHub Releases API: select a Windows ZIP asset.  Prefer the expected
    // portable package, then fall back to the first ZIP asset.
    const QJsonArray assets = object.value(QStringLiteral("assets")).toArray();
    QString fallbackZip;
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        const QString url = asset.value(QStringLiteral("browser_download_url")).toString();
        if (!name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive) || url.isEmpty())
            continue;
        if (name.contains(QStringLiteral("win64"), Qt::CaseInsensitive)) {
            m_downloadUrl = url;
            break;
        }
        if (fallbackZip.isEmpty())
            fallbackZip = url;
    }
    if (m_downloadUrl.isEmpty())
        m_downloadUrl = fallbackZip;

    if (m_remoteVersion.isEmpty() || m_downloadUrl.isEmpty()) {
        fail(QStringLiteral("更新清单缺少版本号或下载地址"));
        return;
    }
    if (compareVersions(currentVersion(), m_remoteVersion) >= 0) {
        m_busy = false;
        emit statusChanged(QStringLiteral("当前已是最新版本"));
        emit finished(true, QStringLiteral("当前已是最新版本（%1）").arg(currentVersion()), {});
        return;
    }

    emit updateAvailable(m_remoteVersion, m_notes);
    emit statusChanged(QStringLiteral("发现新版本 %1，正在下载…").arg(m_remoteVersion));
    startDownload();
}

void UpdateManager::startDownload()
{
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/LightRec/updates");
    if (!QDir().mkpath(directory)) {
        fail(QStringLiteral("无法创建更新缓存目录"));
        return;
    }
    m_archivePath = directory + QStringLiteral("/LightRec-%1-win64.zip").arg(m_remoteVersion);
    QFile::remove(m_archivePath);

    m_archive = new QFile(m_archivePath, this);
    if (!m_archive->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(QStringLiteral("无法创建更新缓存：%1").arg(m_archive->errorString()));
        return;
    }

    QNetworkRequest request{ QUrl(m_downloadUrl) };
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LightRec/%1").arg(currentVersion()));
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (m_reply && m_archive)
            m_archive->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                emit progressChanged(received, total);
            });
    connect(m_reply, &QNetworkReply::finished, this, &UpdateManager::handleDownloadFinished);
}

void UpdateManager::handleDownloadFinished()
{
    if (!m_reply || m_cancelled)
        return;
    const QNetworkReply::NetworkError error = m_reply->error();
    const QString errorText = m_reply->errorString();
    if (m_archive) {
        if (error == QNetworkReply::NoError)
            m_archive->write(m_reply->readAll());
        m_archive->close();
    }
    m_reply->deleteLater();
    m_reply = nullptr;
    if (m_archive) {
        delete m_archive;
        m_archive = nullptr;
    }
    if (error != QNetworkReply::NoError) {
        fail(QStringLiteral("下载更新失败：%1").arg(errorText));
        return;
    }
    if (QFileInfo(m_archivePath).size() < 1024) {
        fail(QStringLiteral("更新包不完整"));
        return;
    }

    if (!m_sha256.isEmpty()) {
        QFile file(m_archivePath);
        if (!file.open(QIODevice::ReadOnly)) {
            fail(QStringLiteral("无法校验更新包"));
            return;
        }
        const QString actual = QString::fromLatin1(
            QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
        if (actual.compare(m_sha256, Qt::CaseInsensitive) != 0) {
            fail(QStringLiteral("更新包校验失败，请重试"));
            return;
        }
    }

    m_busy = false;
    emit progressChanged(1, 1);
    emit statusChanged(QStringLiteral("更新包已下载，准备安装…"));
    emit finished(true, QStringLiteral("更新包已下载，准备安装…"), m_archivePath);
}

void UpdateManager::fail(const QString &message)
{
    if (m_cancelled)
        return;
    m_busy = false;
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
    if (!m_archivePath.isEmpty())
        QFile::remove(m_archivePath);
    emit statusChanged(message);
    emit finished(false, message, {});
}

bool UpdateManager::installAndRestart(const QString &archivePath, QString *error)
{
    if (!QFileInfo::exists(archivePath) || QFileInfo(archivePath).size() < 1024) {
        if (error)
            *error = QStringLiteral("更新包不存在或不完整");
        return false;
    }
    const QString target = QDir::cleanPath(QCoreApplication::applicationDirPath());
    if (target.isEmpty()) {
        if (error)
            *error = QStringLiteral("无法确定程序安装目录");
        return false;
    }

    // Do not quit and hand off to an updater if files cannot be replaced.
    const QString probePath = target + QStringLiteral("/.lightrec-update-probe-%1")
        .arg(QCoreApplication::applicationPid());
    QFile probe(probePath);
    if (!probe.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("程序目录不可写，请将 LightRec 放到有写入权限的目录后再更新");
        return false;
    }
    probe.close();
    probe.remove();

    const QString scriptDirectory =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/LightRec/update");
    QDir().mkpath(scriptDirectory);
    const QString scriptPath = scriptDirectory + QStringLiteral("/apply-update.ps1");
    QFile::remove(scriptPath);

    const QString targetQuoted = powerShellQuote(QDir::toNativeSeparators(target));
    const QString archiveQuoted = powerShellQuote(QDir::toNativeSeparators(archivePath));
    const qint64 pid = QCoreApplication::applicationPid();
    const QString script = QStringLiteral(
        "$ErrorActionPreference = 'Stop'\n"
        "$log = Join-Path $env:TEMP 'lightrec-update.log'\n"
        "function Write-UpdateLog($message) {\n"
        "  try { Add-Content -LiteralPath $log -Value ('[' + (Get-Date -Format o) + '] ' + $message) } catch {}\n"
        "}\n"
        "try {\n"
        "  Write-UpdateLog 'waiting for LightRec to exit'\n"
        "  Wait-Process -Id %1 -Timeout 60 -ErrorAction SilentlyContinue\n"
        "  Start-Sleep -Milliseconds 800\n"
        "  $extract = Join-Path $env:TEMP ('LightRec-update-' + [guid]::NewGuid().ToString('N'))\n"
        "  Expand-Archive -LiteralPath %2 -DestinationPath $extract -Force\n"
        "  $exe = Get-ChildItem -LiteralPath $extract -Filter 'LightRec.exe' -Recurse -File | Select-Object -First 1\n"
        "  if ($null -eq $exe) { throw 'update package does not contain LightRec.exe' }\n"
        "  $source = $exe.Directory.FullName\n"
        "  Get-ChildItem -LiteralPath $source -Force | ForEach-Object {\n"
        "    Copy-Item -LiteralPath $_.FullName -Destination %3 -Recurse -Force\n"
        "  }\n"
        "  Write-UpdateLog 'files replaced; restarting'\n"
        "  Start-Process -FilePath (Join-Path %3 'LightRec.exe') -WorkingDirectory %3\n"
        "} catch {\n"
        "  Write-UpdateLog ('update failed: ' + $_)\n"
        "  try { Start-Process -FilePath (Join-Path %3 'LightRec.exe') -WorkingDirectory %3 } catch {}\n"
        "} finally {\n"
        "  if ($extract) { Remove-Item -LiteralPath $extract -Recurse -Force -ErrorAction SilentlyContinue }\n"
        "  Remove-Item -LiteralPath %2 -Force -ErrorAction SilentlyContinue\n"
        "  Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue\n"
        "}\n")
        .arg(QString::number(pid), archiveQuoted, targetQuoted);

    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法创建更新脚本");
        return false;
    }
    // PowerShell 5 reads Unicode paths correctly from a UTF-8 BOM script.
    scriptFile.write(QByteArray("\xEF\xBB\xBF"));
    scriptFile.write(script.toUtf8());
    scriptFile.close();

#ifdef Q_OS_WIN
    QString powerShell = qEnvironmentVariable("SystemRoot")
        + QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe");
    if (!QFileInfo::exists(powerShell))
        powerShell = QStandardPaths::findExecutable(QStringLiteral("powershell"));
    if (powerShell.isEmpty()) {
        if (error)
            *error = QStringLiteral("系统中未找到 PowerShell，无法完成静默更新");
        return false;
    }

    const QString command = commandLineQuote(QDir::toNativeSeparators(powerShell))
        + QStringLiteral(" -NoProfile -NonInteractive -ExecutionPolicy Bypass")
        + QStringLiteral(" -WindowStyle Hidden -File ")
        + commandLineQuote(QDir::toNativeSeparators(scriptPath));
    std::wstring commandLine = command.toStdWString();
    std::wstring workingDirectory = QDir::toNativeSeparators(target).toStdWString();

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process = {};
    const BOOL started = CreateProcessW(
        nullptr, commandLine.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
        nullptr, workingDirectory.c_str(), &startup, &process);
    if (!started) {
        if (error)
            *error = QStringLiteral("无法启动更新安装程序（错误码 %1）").arg(GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    Q_UNUSED(scriptPath)
    if (error)
        *error = QStringLiteral("当前平台不支持自动更新");
    return false;
#endif
}

} // namespace lightrec
