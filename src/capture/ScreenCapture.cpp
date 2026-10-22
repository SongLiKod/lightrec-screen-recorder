#include "capture/ScreenCapture.h"
#include "capture/DxgiCapture.h"
#include "core/WindowUtils.h"

#ifdef LIGHTREC_HAVE_WGC
#include "capture/WgcCapture.h"
#endif

#include <QGuiApplication>
#include <QScreen>

#include <windows.h>
#include <dwmapi.h>

namespace lightrec {

namespace {

struct WindowEnumContext {
    QList<WindowInfo> windows;
    DWORD ownPid = 0;
};

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto *context = reinterpret_cast<WindowEnumContext *>(lParam);
    if (!context)
        return TRUE;

    if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
        return TRUE;

    const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW)
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == context->ownPid)
        return TRUE;

    const int titleLength = GetWindowTextLengthW(hwnd);
    if (titleLength <= 0)
        return TRUE;

    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, 511);

    RECT rect = {};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))))
        GetWindowRect(hwnd, &rect);

    WindowInfo info;
    info.id = reinterpret_cast<WId>(hwnd);
    info.title = QString::fromWCharArray(title);
    info.processName = WindowUtils::windowProcessName(info.id);
    info.rect = QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    if (info.rect.width() >= 80 && info.rect.height() >= 60)
        context->windows.append(info);
    return TRUE;
}

} // namespace

ScreenCapture::ScreenCapture() = default;
ScreenCapture::~ScreenCapture() = default;

QList<MonitorInfo> ScreenCapture::monitors()
{
    QList<MonitorInfo> result;
    const auto screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        QScreen *screen = screens.at(i);
        MonitorInfo info;
        info.index = i;
        info.geometry = screen->geometry();
        info.name = screen->name();
        info.primary = screen == QGuiApplication::primaryScreen();
        info.devicePixelRatio = screen->devicePixelRatio();
        result.append(info);
    }
    return result;
}

QList<WindowInfo> ScreenCapture::windows()
{
    WindowEnumContext context;
    context.ownPid = GetCurrentProcessId();
    EnumWindows(&enumWindowsProc, reinterpret_cast<LPARAM>(&context));
    return context.windows;
}

bool ScreenCapture::start(const CaptureConfig &config, QString *error)
{
    stop();
    m_lastError.clear();

#ifdef LIGHTREC_HAVE_WGC
    {
        auto wgc = std::make_unique<WgcCapture>();
        QString wgcError;
        if (wgc->start(config, &wgcError)) {
            m_backend = std::move(wgc);
            m_running = true;
            qInfo() << "Screen capture backend: WGC";
            return true;
        }
        m_lastError = wgcError;
        qInfo() << "WGC unavailable, falling back to DXGI:" << wgcError;
    }
#endif

    auto dxgi = std::make_unique<DxgiCapture>();
    QString dxgiError;
    if (!dxgi->start(config, &dxgiError)) {
        m_lastError = dxgiError;
        if (error)
            *error = dxgiError;
        return false;
    }
    m_backend = std::move(dxgi);
    m_running = true;
    qInfo() << "Screen capture backend: DXGI";
    return true;
}

void ScreenCapture::stop()
{
    if (m_backend) {
        m_backend->stop();
        m_backend.reset();
    }
    m_running = false;
}

bool ScreenCapture::isRunning() const
{
    return m_running && m_backend;
}

bool ScreenCapture::updateConfig(const CaptureConfig &config, QString *error)
{
    if (!m_backend)
        return false;
    const bool ok = m_backend->updateConfig(config, error);
    if (!ok && error)
        m_lastError = *error;
    return ok;
}

bool ScreenCapture::grab(QImage &frame, int timeoutMs, QString *error)
{
    if (!m_backend) {
        if (error)
            *error = QStringLiteral("屏幕捕获尚未启动");
        return false;
    }
    const bool ok = m_backend->grab(frame, timeoutMs, error);
    if (!ok && error && !error->isEmpty())
        m_lastError = *error;
    return ok;
}

QRect ScreenCapture::captureRect() const
{
    return m_backend ? m_backend->captureRect() : QRect();
}

QSize ScreenCapture::sourceSize() const
{
    return m_backend ? m_backend->sourceSize() : QSize();
}

QString ScreenCapture::backendName() const
{
    return m_backend ? m_backend->name() : QString();
}

} // namespace lightrec
