#pragma once

#include <QIcon>
#include <QRect>
#include <QString>

class QScreen;

namespace lightrec {

class WindowUtils
{
public:
    static void excludeFromCapture(WId windowId);
    static void setClickThrough(WId windowId, bool enabled);
    static void setTopmost(WId windowId, bool enabled);
    static void setNoActivate(WId windowId);
    static QRect virtualDesktopGeometry();
    static QScreen *screenAt(const QPoint &globalPoint);
    static bool isWindows11();
    static void setAppUserModelId();
    static QString windowProcessName(WId windowId);
    static void raiseWindow(WId windowId);
    static QIcon appIcon();
};

} // namespace lightrec
