#include "core/Logger.h"
#include "core/AppPaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <cstdio>

namespace lightrec {

namespace {
QMutex g_logMutex;
QFile *g_logFile = nullptr;
QString g_logPath;
QtMessageHandler g_previousHandler = nullptr;

QString levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QStringLiteral("DEBUG");
    case QtInfoMsg: return QStringLiteral("INFO ");
    case QtWarningMsg: return QStringLiteral("WARN ");
    case QtCriticalMsg: return QStringLiteral("ERROR");
    case QtFatalMsg: return QStringLiteral("FATAL");
    }
    return QStringLiteral("INFO ");
}
} // namespace

void Logger::install()
{
    AppPaths::ensureDirectories();
    g_logPath = AppPaths::logsDir() + QStringLiteral("/lightrec.log");

    // Rotate once the log grows beyond 4 MB.
    if (QFile::exists(g_logPath) && QFileInfo(g_logPath).size() > 4 * 1024 * 1024) {
        const QString oldPath = g_logPath + QStringLiteral(".old");
        QFile::remove(oldPath);
        QFile::rename(g_logPath, oldPath);
    }

    g_logFile = new QFile(g_logPath);
    g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    g_previousHandler = qInstallMessageHandler(&Logger::messageHandler);
}

QString Logger::logFilePath()
{
    return g_logPath;
}

void Logger::shutdown()
{
    QMutexLocker locker(&g_logMutex);
    if (g_logFile) {
        g_logFile->flush();
        g_logFile->close();
        delete g_logFile;
        g_logFile = nullptr;
    }
}

void Logger::messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QString line = QStringLiteral("%1 [%2] %3%4")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  levelName(type), message,
                                  context.file ? QStringLiteral(" (%1:%2)").arg(QString::fromUtf8(context.file)).arg(context.line) : QString());

    {
        QMutexLocker locker(&g_logMutex);
        if (g_logFile && g_logFile->isOpen()) {
            QTextStream stream(g_logFile);
            stream << line << QLatin1Char('\n');
            stream.flush();
        }
    }

    fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    fflush(stderr);

    if (g_previousHandler)
        g_previousHandler(type, context, message);

    if (type == QtFatalMsg)
        abort();
}

} // namespace lightrec
