#include "capture/DxgiCapture.h"

#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QVector>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwmapi.h>
#include <windows.h>

#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace lightrec {

namespace {

QRect screenGeometry(int index)
{
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return QRect(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    if (index < 0 || index >= screens.size())
        index = 0;
    return screens.at(index)->geometry();
}

QRect windowFrameRect(HWND hwnd)
{
    RECT rect = {};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))))
        GetWindowRect(hwnd, &rect);
    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

QScreen *screenForLogicalRect(const QRect &rect)
{
    QScreen *screen = QGuiApplication::screenAt(rect.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    return screen;
}

QRect logicalToPhysical(const QRect &rect)
{
    QScreen *screen = screenForLogicalRect(rect);
    const qreal ratio = screen ? screen->devicePixelRatio() : 1.0;
    return QRect(qRound(rect.x() * ratio), qRound(rect.y() * ratio),
                 qRound(rect.width() * ratio), qRound(rect.height() * ratio));
}

QRect physicalToLogical(const QRect &rect)
{
    QScreen *screen = QGuiApplication::screenAt(
        QPoint(qRound(rect.center().x() / (QGuiApplication::primaryScreen()
                                              ? QGuiApplication::primaryScreen()->devicePixelRatio()
                                              : 1.0)),
               qRound(rect.center().y() / (QGuiApplication::primaryScreen()
                                              ? QGuiApplication::primaryScreen()->devicePixelRatio()
                                              : 1.0))));
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const qreal ratio = screen ? screen->devicePixelRatio() : 1.0;
    return QRect(qRound(rect.x() / ratio), qRound(rect.y() / ratio),
                 qRound(rect.width() / ratio), qRound(rect.height() / ratio));
}

} // namespace

DxgiCapture::DxgiCapture() = default;
DxgiCapture::~DxgiCapture()
{
    stop();
}

bool DxgiCapture::start(const CaptureConfig &config, QString *error)
{
    stop();
    m_config = config;

    if (!initDevice(error))
        return false;

    m_lastRect = resolveRect();
    m_lastPhysicalRect = logicalToPhysical(m_lastRect);
    if (m_lastRect.width() < 2 || m_lastRect.height() < 2) {
        if (error)
            *error = QStringLiteral("捕获区域无效");
        return false;
    }

    if (!selectOutput(m_lastPhysicalRect, error))
        return false;
    if (!createDuplication(error))
        return false;

    m_started = true;
    return true;
}

void DxgiCapture::stop()
{
    releaseDuplication();
    m_staging.Reset();
    m_output1.Reset();
    m_output.Reset();
    m_adapter.Reset();
    m_context.Reset();
    m_device.Reset();
    m_started = false;
    m_outputIndex = -1;
}

bool DxgiCapture::initDevice(QString *error)
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                   &m_device, &obtained, &m_context);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                               levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                               &m_device, &obtained, &m_context);
    }
    if (FAILED(hr)) {
        if (error)
            *error = QStringLiteral("无法创建 Direct3D 11 设备 (0x%1)").arg(quint32(hr), 8, 16, QLatin1Char('0'));
        return false;
    }
    return true;
}

bool DxgiCapture::selectOutput(const QRect &targetRect, QString *error)
{
    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(m_device.As(&dxgiDevice))) {
        if (error)
            *error = QStringLiteral("无法获取 DXGI 设备");
        return false;
    }
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) {
        if (error)
            *error = QStringLiteral("无法获取显卡适配器");
        return false;
    }
    if (FAILED(adapter.As(&m_adapter))) {
        if (error)
            *error = QStringLiteral("无法获取显卡适配器");
        return false;
    }

    int bestIndex = -1;
    qint64 bestArea = -1;
    ComPtr<IDXGIOutput> bestOutput;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIOutput> output;
        if (m_adapter->EnumOutputs(i, &output) == DXGI_ERROR_NOT_FOUND)
            break;
        if (!output)
            continue;
        DXGI_OUTPUT_DESC desc = {};
        if (FAILED(output->GetDesc(&desc)) || !desc.AttachedToDesktop)
            continue;
        const QRect rect(desc.DesktopCoordinates.left, desc.DesktopCoordinates.top,
                         desc.DesktopCoordinates.right - desc.DesktopCoordinates.left,
                         desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top);
        const QRect intersection = rect.intersected(targetRect);
        const qint64 area = static_cast<qint64>(intersection.width()) * intersection.height();
        if (area > bestArea) {
            bestArea = area;
            bestIndex = static_cast<int>(i);
            bestOutput = output;
            m_outputRect = rect;
        }
    }

    if (bestIndex < 0 || bestArea <= 0) {
        if (error)
            *error = QStringLiteral("未找到可捕获的显示器");
        return false;
    }

    m_output = bestOutput;
    m_output1.Reset();
    if (FAILED(m_output.As(&m_output1))) {
        if (error)
            *error = QStringLiteral("当前系统不支持 Desktop Duplication");
        return false;
    }
    m_outputIndex = bestIndex;
    return true;
}

bool DxgiCapture::createDuplication(QString *error)
{
    releaseDuplication();

    DXGI_OUTPUT_DESC desc = {};
    m_output->GetDesc(&desc);
    m_outputRect = QRect(desc.DesktopCoordinates.left, desc.DesktopCoordinates.top,
                         desc.DesktopCoordinates.right - desc.DesktopCoordinates.left,
                         desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top);
    m_outputSize = m_outputRect.size();

    HRESULT hr = m_output1->DuplicateOutput(m_device.Get(), &m_duplication);
    if (FAILED(hr)) {
        if (error)
            *error = QStringLiteral("创建屏幕捕获失败 (0x%1)，请尝试关闭其他录屏/远程软件")
                         .arg(quint32(hr), 8, 16, QLatin1Char('0'));
        return false;
    }

    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = static_cast<UINT>(m_outputSize.width());
    textureDesc.Height = static_cast<UINT>(m_outputSize.height());
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_STAGING;
    textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = m_device->CreateTexture2D(&textureDesc, nullptr, &m_staging);
    if (FAILED(hr)) {
        if (error)
            *error = QStringLiteral("创建捕获缓冲失败");
        return false;
    }
    return true;
}

void DxgiCapture::releaseDuplication()
{
    if (m_duplication)
        m_duplication->ReleaseFrame();
    m_duplication.Reset();
    m_staging.Reset();
}

bool DxgiCapture::updateConfig(const CaptureConfig &config, QString *error)
{
    m_config = config;
    if (!m_started)
        return true;

    const QRect target = resolveRect();
    if (target.width() < 2 || target.height() < 2)
        return true;
    m_lastPhysicalRect = logicalToPhysical(target);
    m_lastRect = target;

    if (!m_duplication)
        return createDuplication(error);

    // Only switch the duplicated output when the target moved to another
    // monitor; region changes are just a crop of the existing frame.
    const QRect intersection = m_lastPhysicalRect.intersected(m_outputRect);
    if (intersection.width() <= 0 || intersection.height() <= 0) {
        if (!selectOutput(m_lastPhysicalRect, error))
            return false;
        if (!createDuplication(error))
            return false;
    }
    return true;
}

QRect DxgiCapture::resolveRect() const
{
    switch (m_config.mode) {
    case CaptureMode::FullScreen: {
        return screenGeometry(m_config.monitorIndex);
    }
    case CaptureMode::Region: {
        QRect region = m_config.region;
        if (region.width() < 2 || region.height() < 2)
            region = screenGeometry(m_config.monitorIndex);
        return region;
    }
    case CaptureMode::Window: {
        const HWND hwnd = reinterpret_cast<HWND>(m_config.windowId);
        if (hwnd && IsWindow(hwnd) && !IsIconic(hwnd)) {
            const QRect physical = windowFrameRect(hwnd);
            if (physical.width() >= 2 && physical.height() >= 2)
                return physicalToLogical(physical);
        }
        return m_lastRect.isValid() ? m_lastRect : screenGeometry(m_config.monitorIndex);
    }
    }
    return screenGeometry(0);
}

bool DxgiCapture::grab(QImage &frame, int timeoutMs, QString *error)
{
    if (!m_started || !m_duplication) {
        if (error)
            *error = QStringLiteral("屏幕捕获尚未启动");
        return false;
    }

    const QRect target = resolveRect();
    if (target.width() < 2 || target.height() < 2)
        return false;

    const QRect targetPhysical = logicalToPhysical(target);
    const QRect intersection = targetPhysical.intersected(m_outputRect);
    if (intersection.width() <= 0 || intersection.height() <= 0) {
        if (!updateConfig(m_config, error))
            return false;
        return false;
    }

    DXGI_OUTDUPL_FRAME_INFO info = {};
    ComPtr<IDXGIResource> resource;
    HRESULT hr = m_duplication->AcquireNextFrame(static_cast<UINT>(std::max(1, timeoutMs)), &info, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        m_lastRect = target;
        m_lastPhysicalRect = targetPhysical;
        return false;
    }
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        // Desktop switched, display mode changed or secure desktop appeared.
        if (!createDuplication(error))
            return false;
        m_lastRect = target;
        m_lastPhysicalRect = targetPhysical;
        return false;
    }
    if (FAILED(hr)) {
        if (error)
            *error = QStringLiteral("捕获屏幕帧失败 (0x%1)").arg(quint32(hr), 8, 16, QLatin1Char('0'));
        return false;
    }

    ComPtr<ID3D11Texture2D> texture;
    hr = resource.As(&texture);
    if (SUCCEEDED(hr)) {
        m_context->CopyResource(m_staging.Get(), texture.Get());
    }
    m_duplication->ReleaseFrame();

    if (FAILED(hr)) {
        if (error)
            *error = QStringLiteral("获取屏幕纹理失败");
        return false;
    }

    QImage full;
    if (!copyToImage(full, error))
        return false;

    QRect crop = targetPhysical.intersected(m_outputRect);
    crop.translate(-m_outputRect.topLeft());
    crop = crop.intersected(full.rect());
    if (crop.isEmpty())
        return false;

    frame = full.copy(crop);
    m_lastRect = target;
    m_lastPhysicalRect = targetPhysical;

    if (m_config.captureCursor)
        drawCursor(frame);

    return true;
}

bool DxgiCapture::copyToImage(QImage &image, QString *error)
{
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = m_context->Map(m_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        if (error)
            *error = QStringLiteral("读取屏幕帧失败");
        return false;
    }

    image = QImage(static_cast<int>(m_outputSize.width()), static_cast<int>(m_outputSize.height()),
                   QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        memcpy(image.scanLine(y),
               static_cast<const uchar *>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
               static_cast<size_t>(image.width()) * 4);
    }
    m_context->Unmap(m_staging.Get(), 0);
    return true;
}

QImage DxgiCapture::cursorImage(QPoint *hotspot) const
{
    CURSORINFO info = {};
    info.cbSize = sizeof(info);
    if (!GetCursorInfo(&info) || !(info.flags & CURSOR_SHOWING) || !info.hCursor)
        return {};

    const int size = GetSystemMetrics(SM_CXCURSOR);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(memDc, bitmap);
    memset(bits, 0, static_cast<size_t>(size) * size * 4);

    if (!DrawIconEx(memDc, 0, 0, info.hCursor, size, size, 0, nullptr, DI_NORMAL)) {
        SelectObject(memDc, old);
        DeleteObject(bitmap);
        DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);
        return {};
    }

    QImage image(size, size, QImage::Format_ARGB32);
    memcpy(image.bits(), bits, static_cast<size_t>(size) * size * 4);
    image = image.mirrored(false, false);

    ICONINFO iconInfo = {};
    if (GetIconInfo(info.hCursor, &iconInfo)) {
        if (hotspot)
            *hotspot = QPoint(static_cast<int>(iconInfo.xHotspot), static_cast<int>(iconInfo.yHotspot));
        if (iconInfo.hbmColor)
            DeleteObject(iconInfo.hbmColor);
        if (iconInfo.hbmMask)
            DeleteObject(iconInfo.hbmMask);
    }

    SelectObject(memDc, old);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return image;
}

void DxgiCapture::drawCursor(QImage &image) const
{
    POINT cursorPos = {};
    if (!GetCursorPos(&cursorPos))
        return;
    if (!image.rect().translated(m_lastPhysicalRect.topLeft())
             .contains(QPoint(cursorPos.x, cursorPos.y)))
        return;

    QPoint hotspot;
    const QImage cursor = cursorImage(&hotspot);
    if (cursor.isNull())
        return;

    const QPoint local(cursorPos.x - m_lastPhysicalRect.x() - hotspot.x(),
                       cursorPos.y - m_lastPhysicalRect.y() - hotspot.y());
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(local, cursor);
}

} // namespace lightrec
