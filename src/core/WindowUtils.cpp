#include "core/WindowUtils.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#endif

namespace lightrec {

void WindowUtils::excludeFromCapture(WId windowId)
{
#ifdef Q_OS_WIN
    if (!windowId || qEnvironmentVariableIsSet("LIGHTREC_DEBUG_CAPTURE"))
        return;
    const HWND hwnd = reinterpret_cast<HWND>(windowId);
    // WDA_EXCLUDEFROMCAPTURE is available on Windows 10 2004+. Older builds
    // fall back to WDA_MONITOR, which still keeps the overlay out of captures.
    if (!SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE))
        SetWindowDisplayAffinity(hwnd, WDA_MONITOR);
#else
    Q_UNUSED(windowId)
#endif
}

void WindowUtils::setClickThrough(WId windowId, bool enabled)
{
#ifdef Q_OS_WIN
    if (!windowId)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(windowId);
    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_LAYERED | WS_EX_TOOLWINDOW;
    if (enabled) {
        exStyle |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    } else {
        exStyle &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    }
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#else
    Q_UNUSED(windowId)
    Q_UNUSED(enabled)
#endif
}

void WindowUtils::setTopmost(WId windowId, bool enabled)
{
#ifdef Q_OS_WIN
    if (!windowId)
        return;
    SetWindowPos(reinterpret_cast<HWND>(windowId),
                 enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
    Q_UNUSED(windowId)
    Q_UNUSED(enabled)
#endif
}

void WindowUtils::setNoActivate(WId windowId)
{
#ifdef Q_OS_WIN
    if (!windowId)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(windowId);
    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
#else
    Q_UNUSED(windowId)
#endif
}

QRect WindowUtils::virtualDesktopGeometry()
{
    QRect result;
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens)
        result = result.united(screen->geometry());
    if (result.isEmpty()) {
#ifdef Q_OS_WIN
        result = QRect(GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                       GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
#endif
    }
    return result;
}

QScreen *WindowUtils::screenAt(const QPoint &globalPoint)
{
    QScreen *screen = QGuiApplication::screenAt(globalPoint);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    return screen;
}

bool WindowUtils::isWindows11()
{
#ifdef Q_OS_WIN
    OSVERSIONINFOEXW info = {};
    info.dwOSVersionInfoSize = sizeof(info);
    info.dwMajorVersion = 10;
    info.dwMinorVersion = 0;
    info.dwBuildNumber = 22000;
    DWORDLONG condition = 0;
    VER_SET_CONDITION(condition, VER_MAJORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(condition, VER_MINORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(condition, VER_BUILDNUMBER, VER_GREATER_EQUAL);
    return VerifyVersionInfoW(&info, VER_MAJORVERSION | VER_MINORVERSION | VER_BUILDNUMBER, condition) != FALSE;
#else
    return false;
#endif
}

void WindowUtils::setAppUserModelId()
{
#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(L"LightRec.ScreenRecorder.1.1");
#endif
}

QString WindowUtils::windowProcessName(WId windowId)
{
#ifdef Q_OS_WIN
    if (!windowId)
        return {};
    DWORD pid = 0;
    GetWindowThreadProcessId(reinterpret_cast<HWND>(windowId), &pid);
    if (!pid)
        return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    QString name;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        name = QString::fromWCharArray(path);
        const int slash = name.lastIndexOf(QLatin1Char('\\'));
        if (slash >= 0)
            name = name.mid(slash + 1);
    }
    CloseHandle(process);
    return name;
#else
    Q_UNUSED(windowId)
    return {};
#endif
}

void WindowUtils::raiseWindow(WId windowId)
{
#ifdef Q_OS_WIN
    if (!windowId)
        return;
    HWND hwnd = reinterpret_cast<HWND>(windowId);
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
#else
    Q_UNUSED(windowId)
#endif
}

QIcon WindowUtils::appIcon()
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x2D, 0x8C, 0xF0));
    painter.drawRoundedRect(QRectF(4, 4, 56, 56), 14, 14);
    painter.setBrush(Qt::white);
    painter.drawEllipse(QPointF(32, 32), 13, 13);
    painter.setBrush(QColor(0x2D, 0x8C, 0xF0));
    painter.drawEllipse(QPointF(32, 32), 8, 8);
    painter.end();
    return QIcon(pixmap);
}

} // namespace lightrec
