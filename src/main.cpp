#include "core/AppPaths.h"
#include "core/ConfigStore.h"
#include "core/Logger.h"
#include "core/SingleInstance.h"
#include "core/ThemeManager.h"
#include "core/WindowUtils.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QFont>
#include <QGuiApplication>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#endif

int main(int argc, char *argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

#ifdef Q_OS_WIN
    // Media Foundation (h264_mf/hevc_mf) requires MTA.  Qt's OLE usage only
    // needs STA for drag & drop, which LightRec does not use.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationDisplayName(QStringLiteral("LightRec 轻录"));
    QApplication::setApplicationVersion(QStringLiteral("1.1.0"));

    lightrec::ConfigStore::instance();
    lightrec::Logger::install();
    lightrec::WindowUtils::setAppUserModelId();
    lightrec::AppPaths::ensureDirectories();

    app.setWindowIcon(lightrec::WindowUtils::appIcon());

    QFont font;
    font.setFamilies({ QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI") });
    font.setPointSize(9);
    app.setFont(font);

    lightrec::SingleInstance instance;
    if (!instance.tryStart()) {
        // Another instance is already running; it was asked to show itself.
        lightrec::Logger::shutdown();
        return 0;
    }

    lightrec::ThemeManager::instance().initialize();

    lightrec::MainWindow window;
    QObject::connect(&instance, &lightrec::SingleInstance::activationRequested, &window, [&window] {
        window.showNormal();
        window.raise();
        window.activateWindow();
    });
    window.show();

    const QStringList arguments = app.arguments();
    const int diagnosticIndex = arguments.indexOf(QStringLiteral("--record-test"));
    if (diagnosticIndex >= 0) {
        int seconds = 4;
        if (diagnosticIndex + 1 < arguments.size())
            seconds = qBound(1, arguments.at(diagnosticIndex + 1).toInt(), 120);
        QTimer::singleShot(1200, &window, [&window, seconds] {
            window.startDiagnosticRecording(seconds);
        });
        QTimer::singleShot((seconds + 30) * 1000, &app, &QCoreApplication::quit);
    }

    const int result = app.exec();
    lightrec::Logger::shutdown();
    return result;
}
