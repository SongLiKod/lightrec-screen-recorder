#pragma once

#include <QString>

namespace lightrec {

class AppPaths
{
public:
    static QString configDir();
    static QString presetsDir();
    static QString sessionsDir();
    static QString logsDir();
    static QString defaultOutputDir();
    static QString outputDir();
    static void setOutputDir(const QString &dir);

    static QString ffmpegExe();
    static QString bundledResource(const QString &relative);

    static void ensureDirectories();
    static QString uniqueOutputPath(const QString &dir, const QString &prefix = QStringLiteral("LightRec"));
    static QString sanitizeFileName(const QString &name);
};

} // namespace lightrec
