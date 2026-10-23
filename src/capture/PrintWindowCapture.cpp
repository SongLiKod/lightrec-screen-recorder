#include "capture/PrintWindowCapture.h"

#include <QDebug>

#include <cstring>

#include <windows.h>

#ifndef PW_CLIENTONLY
#define PW_CLIENTONLY 0x00000001
#endif
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace lightrec {

namespace {

void drawCursorToDC(HDC dc, HWND hwnd, int width, int height)
{
    if (GetForegroundWindow() != hwnd)
        return;

    CURSORINFO cursor = {};
    cursor.cbSize = sizeof(cursor);
    if (!GetCursorInfo(&cursor) || !(cursor.flags & CURSOR_SHOWING) || !cursor.hCursor)
        return;

    POINT point = cursor.ptScreenPos;
    if (!ScreenToClient(hwnd, &point))
        return;
    if (point.x < -64 || point.y < -64 || point.x > width + 64 || point.y > height + 64)
        return;

    ICONINFO icon = {};
    if (!GetIconInfo(cursor.hCursor, &icon))
        return;
    const int hotX = static_cast<int>(icon.xHotspot);
    const int hotY = static_cast<int>(icon.yHotspot);
    const int cursorWidth = GetSystemMetrics(SM_CXCURSOR);
    const int cursorHeight = GetSystemMetrics(SM_CYCURSOR);
    DrawIconEx(dc, point.x - hotX, point.y - hotY, cursor.hCursor,
               cursorWidth, cursorHeight, 0, nullptr, DI_NORMAL);
    if (icon.hbmColor)
        DeleteObject(icon.hbmColor);
    if (icon.hbmMask)
        DeleteObject(icon.hbmMask);
}

} // namespace

PrintWindowCapture::PrintWindowCapture() = default;

PrintWindowCapture::~PrintWindowCapture()
{
    stop();
}

bool PrintWindowCapture::start(const CaptureConfig &config, QString *error)
{
    stop();
    m_config = config;
    if (m_config.mode != CaptureMode::Window) {
        if (error)
            *error = QStringLiteral("PrintWindow 捕获仅支持窗口录制");
        return false;
    }

    const HWND hwnd = reinterpret_cast<HWND>(m_config.windowId);
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        if (error)
            *error = QStringLiteral("目标窗口已关闭或最小化，请重新选择窗口");
        return false;
    }

    QRect physical;
    m_lastRect = logicalClientRect(&physical);
    m_lastSize = physical.size();
    if (m_lastRect.width() < 2 || m_lastRect.height() < 2) {
        if (error)
            *error = QStringLiteral("目标窗口没有可捕获的客户区");
        return false;
    }

    QImage probe;
    if (!captureFrame(probe, error)) {
        if (error && error->isEmpty())
            *error = QStringLiteral("目标窗口不支持独立窗口捕获");
        return false;
    }

    m_running = true;
    return true;
}

void PrintWindowCapture::stop()
{
    m_running = false;
}

bool PrintWindowCapture::updateConfig(const CaptureConfig &config, QString *error)
{
    Q_UNUSED(error)
    m_config = config;
    return true;
}

bool PrintWindowCapture::grab(QImage &frame, int timeoutMs, QString *error)
{
    Q_UNUSED(timeoutMs)
    if (!m_running)
        return false;
    return captureFrame(frame, error);
}

QRect PrintWindowCapture::logicalClientRect(QRect *physicalRect) const
{
    const HWND hwnd = reinterpret_cast<HWND>(m_config.windowId);
    if (!hwnd || !IsWindow(hwnd))
        return {};

    RECT client = {};
    if (!GetClientRect(hwnd, &client))
        return {};
    POINT origin = { 0, 0 };
    if (!ClientToScreen(hwnd, &origin))
        return {};

    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width < 2 || height < 2)
        return {};

    if (physicalRect)
        *physicalRect = QRect(origin.x, origin.y, width, height);

    const double scale = qMax(1.0, GetDpiForWindow(hwnd) / 96.0);
    return QRect(qRound(origin.x / scale), qRound(origin.y / scale),
                 qRound(width / scale), qRound(height / scale));
}

bool PrintWindowCapture::captureFrame(QImage &frame, QString *error)
{
    const HWND hwnd = reinterpret_cast<HWND>(m_config.windowId);
    if (!hwnd || !IsWindow(hwnd)) {
        if (error)
            *error = QStringLiteral("目标窗口已关闭");
        return false;
    }
    if (IsIconic(hwnd)) {
        // Keep the last frame while minimized; the recording loop repeats it.
        return false;
    }

    RECT client = {};
    if (!GetClientRect(hwnd, &client))
        return false;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width < 2 || height < 2)
        return false;

    HDC screenDc = GetDC(nullptr);
    HDC memoryDc = CreateCompatibleDC(screenDc);
    if (!screenDc || !memoryDc) {
        if (memoryDc)
            DeleteDC(memoryDc);
        if (screenDc)
            ReleaseDC(nullptr, screenDc);
        if (error)
            *error = QStringLiteral("无法创建窗口捕获绘图上下文");
        return false;
    }

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap)
            DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        if (error)
            *error = QStringLiteral("无法创建窗口捕获缓冲");
        return false;
    }

    HGDIOBJ old = SelectObject(memoryDc, bitmap);
    SetBkMode(memoryDc, TRANSPARENT);
    const BOOL ok = PrintWindow(hwnd, memoryDc, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
    if (ok && m_config.captureCursor)
        drawCursorToDC(memoryDc, hwnd, width, height);

    QImage captured;
    if (ok) {
        captured = QImage(width, height, QImage::Format_ARGB32);
        for (int y = 0; y < height; ++y) {
            std::memcpy(captured.scanLine(y),
                        static_cast<const uchar *>(bits) + static_cast<size_t>(y) * width * 4,
                        static_cast<size_t>(width) * 4);
        }
    }

    SelectObject(memoryDc, old);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);

    if (!ok) {
        if (error)
            *error = QStringLiteral("目标窗口无法通过 PrintWindow 捕获");
        return false;
    }

    frame = captured;

    // PrintWindow often leaves the alpha channel at zero; the compositor paints
    // onto an opaque canvas, so force alpha to fully opaque.
    for (int y = 0; y < height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(frame.scanLine(y));
        for (int x = 0; x < width; ++x)
            line[x] |= 0xff000000u;
    }

    m_lastRect = logicalClientRect(nullptr);
    m_lastSize = QSize(width, height);
    return true;
}

} // namespace lightrec
