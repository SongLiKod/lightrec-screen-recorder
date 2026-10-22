#include "capture/WgcCapture.h"

#ifdef LIGHTREC_HAVE_WGC

#include "core/WindowUtils.h"

#include <QGuiApplication>
#include <QScreen>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <windows.h>
#include <dwmapi.h>

#include <chrono>
#include <cstring>

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

namespace lightrec {

namespace {

QRect windowFrameRect(HWND hwnd)
{
    RECT rect = {};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))))
        GetWindowRect(hwnd, &rect);
    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

HMONITOR monitorForRect(const QRect &rect)
{
    return MonitorFromPoint(POINT{ rect.center().x(), rect.center().y() }, MONITOR_DEFAULTTONEAREST);
}

QRect monitorRect(HMONITOR monitor)
{
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        return QRect(info.rcMonitor.left, info.rcMonitor.top,
                     info.rcMonitor.right - info.rcMonitor.left,
                     info.rcMonitor.bottom - info.rcMonitor.top);
    }
    return {};
}

} // namespace

WgcCapture::WgcCapture() = default;

WgcCapture::~WgcCapture()
{
    stop();
}

bool WgcCapture::start(const CaptureConfig &config, QString *error)
{
    m_config = config;
    m_lastRect = resolveRect();
    if (m_lastRect.width() < 2 || m_lastRect.height() < 2) {
        if (error)
            *error = QStringLiteral("捕获区域无效");
        return false;
    }

    if (!GraphicsCaptureSession::IsSupported()) {
        if (error)
            *error = QStringLiteral("当前系统不支持 Windows Graphics Capture");
        return false;
    }

    try {
        if (!createDevice(error))
            return false;
        if (!createItem(error))
            return false;
        if (!createPool(error))
            return false;
        m_running = true;
        return true;
    } catch (const winrt::hresult_error &ex) {
        if (error)
            *error = QStringLiteral("WGC 初始化失败: %1").arg(QString::fromWCharArray(ex.message().c_str()));
        stop();
        return false;
    }
}

void WgcCapture::stop()
{
    m_running = false;
    try {
        if (m_pool) {
            m_pool.FrameArrived(m_frameToken);
            m_pool.Close();
            m_pool = nullptr;
        }
        if (m_session) {
            m_session.Close();
            m_session = nullptr;
        }
    } catch (...) {
    }
    m_pendingFrame = nullptr;
    m_item = nullptr;
    m_directDevice = nullptr;
    m_staging.Reset();
    m_context.Reset();
    m_device.Reset();
}

bool WgcCapture::createDevice(QString *error)
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                   &m_device, &obtained, &m_context);
    if (FAILED(hr)) {
        if (error)
            *error = QStringLiteral("无法创建 Direct3D 11 设备");
        return false;
    }

    winrt::com_ptr<IDXGIDevice> dxgiDevice;
    winrt::check_hresult(m_device.As(&dxgiDevice));
    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
    m_directDevice = inspectable.as<IDirect3DDevice>();
    return true;
}

bool WgcCapture::createItem(QString *error)
{
    const auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();

    try {
        if (m_config.mode == CaptureMode::Window) {
            const HWND hwnd = reinterpret_cast<HWND>(m_config.windowId);
            if (!hwnd || !IsWindow(hwnd)) {
                if (error)
                    *error = QStringLiteral("目标窗口已关闭");
                return false;
            }
            winrt::check_hresult(interop->CreateForWindow(
                hwnd, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(m_item)));
        } else {
            const HMONITOR monitor = monitorForRect(m_lastRect);
            winrt::check_hresult(interop->CreateForMonitor(
                monitor, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(m_item)));
        }
    } catch (const winrt::hresult_error &ex) {
        if (error)
            *error = QStringLiteral("无法创建捕获对象: %1").arg(QString::fromWCharArray(ex.message().c_str()));
        return false;
    }

    if (!m_item) {
        if (error)
            *error = QStringLiteral("无法创建捕获对象");
        return false;
    }

    m_itemSize = QSize(m_item.Size().Width, m_item.Size().Height);
    if (m_config.mode == CaptureMode::Window) {
        m_frameSize = m_itemSize;
    } else {
        QScreen *screen = QGuiApplication::screenAt(m_lastRect.center());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        const qreal ratio = screen ? screen->devicePixelRatio() : 1.0;
        m_frameSize = QSize(qRound(m_lastRect.width() * ratio),
                            qRound(m_lastRect.height() * ratio));
    }
    return true;
}

bool WgcCapture::createPool(QString *error)
{
    try {
        m_pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
            m_directDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, m_item.Size());

        m_frameToken = m_pool.FrameArrived([this](const Direct3D11CaptureFramePool &,
                                                  const winrt::Windows::Foundation::IInspectable &) {
            Direct3D11CaptureFrame frame = nullptr;
            try {
                frame = m_pool.TryGetNextFrame();
            } catch (...) {
                return;
            }
            if (!frame)
                return;
            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_pendingFrame = frame;
                const auto size = frame.ContentSize();
                if (size.Width != m_itemSize.width() || size.Height != m_itemSize.height()) {
                    m_itemSize = QSize(size.Width, size.Height);
                    m_itemChanged = true;
                }
            }
            m_frameCondition.notify_one();
        });

        m_sizeToken = m_item.SizeChanged([this](const GraphicsCaptureItem &, const winrt::Windows::Foundation::Size &) {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            m_itemChanged = true;
        });

        m_session = m_pool.CreateCaptureSession(m_item);
        try {
            m_session.IsCursorCaptureEnabled(m_config.captureCursor);
        } catch (...) {
        }
        try {
            m_session.IsBorderRequired(false);
        } catch (...) {
        }
        m_session.StartCapture();
    } catch (const winrt::hresult_error &ex) {
        if (error)
            *error = QStringLiteral("启动 WGC 会话失败: %1").arg(QString::fromWCharArray(ex.message().c_str()));
        return false;
    }
    return true;
}

QRect WgcCapture::resolveRect() const
{
    switch (m_config.mode) {
    case CaptureMode::FullScreen:
        return QGuiApplication::screens().isEmpty()
                   ? QRect(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN))
                   : QGuiApplication::screens().at(qBound(0, m_config.monitorIndex,
                                                          QGuiApplication::screens().size() - 1))->geometry();
    case CaptureMode::Region: {
        QRect region = m_config.region;
        if (region.width() < 2 || region.height() < 2) {
            region = QGuiApplication::screens().isEmpty()
                         ? QRect(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN))
                         : QGuiApplication::primaryScreen()->geometry();
        }
        return region;
    }
    case CaptureMode::Window: {
        const HWND hwnd = reinterpret_cast<HWND>(m_config.windowId);
        if (hwnd && IsWindow(hwnd) && !IsIconic(hwnd)) {
            const QRect rect = windowFrameRect(hwnd);
            if (rect.width() >= 2 && rect.height() >= 2)
                return rect;
        }
        return m_lastRect.isValid() ? m_lastRect : QRect(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    }
    }
    return {};
}

bool WgcCapture::updateConfig(const CaptureConfig &config, QString *error)
{
    m_config = config;
    if (!m_running)
        return true;

    if (config.mode == CaptureMode::Window) {
        // The capture item follows the window automatically.
        return true;
    }

    const QRect target = resolveRect();
    const HMONITOR current = monitorForRect(m_lastRect);
    const HMONITOR wanted = monitorForRect(target);
    if (current != wanted) {
        stop();
        return start(config, error);
    }
    return true;
}

bool WgcCapture::grab(QImage &frame, int timeoutMs, QString *error)
{
    if (!m_running || !m_pool) {
        if (error)
            *error = QStringLiteral("WGC 尚未启动");
        return false;
    }

    Direct3D11CaptureFrame captured = nullptr;
    {
        std::unique_lock<std::mutex> lock(m_frameMutex);
        if (!m_frameCondition.wait_for(lock, std::chrono::milliseconds(qMax(1, timeoutMs)),
                                       [this] { return m_pendingFrame != nullptr; })) {
            m_lastRect = resolveRect();
            return false;
        }
        captured = m_pendingFrame;
        m_pendingFrame = nullptr;
    }

    try {
        const auto size = captured.ContentSize();
        if (m_itemChanged) {
            m_pool.Recreate(m_directDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
            m_itemChanged = false;
            m_staging.Reset();
        }
        m_itemSize = QSize(size.Width, size.Height);

        if (!m_staging) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = size.Width;
            desc.Height = size.Height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            winrt::check_hresult(m_device->CreateTexture2D(&desc, nullptr, &m_staging));
        }

        const auto surface = captured.Surface();
        winrt::com_ptr<ID3D11Texture2D> texture;
        winrt::check_hresult(GetDXGIInterface(surface, texture.put()));
        m_context->CopyResource(m_staging.Get(), texture.get());
    } catch (const winrt::hresult_error &ex) {
        if (error)
            *error = QStringLiteral("读取 WGC 帧失败: %1").arg(QString::fromWCharArray(ex.message().c_str()));
        captured.Close();
        return false;
    }
    captured.Close();

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(m_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        if (error)
            *error = QStringLiteral("读取 WGC 帧失败");
        return false;
    }

    QImage full(static_cast<int>(m_itemSize.width()), static_cast<int>(m_itemSize.height()),
                QImage::Format_ARGB32);
    for (int y = 0; y < full.height(); ++y) {
        memcpy(full.scanLine(y),
               static_cast<const uchar *>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
               static_cast<size_t>(full.width()) * 4);
    }
    m_context->Unmap(m_staging.Get(), 0);

    const QRect target = resolveRect();
    m_lastRect = target;

    // The item is either the monitor or the window in physical pixels; the
    // capture rect is expressed in Qt's logical coordinates.
    QRect local;
    if (m_config.mode == CaptureMode::Window) {
        const double scaleX = static_cast<double>(m_itemSize.width()) / qMax(1, target.width());
        const double scaleY = static_cast<double>(m_itemSize.height()) / qMax(1, target.height());
        local = QRect(0, 0, qRound(target.width() * scaleX),
                      qRound(target.height() * scaleY));
    } else {
        QScreen *screen = QGuiApplication::screenAt(target.center());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        const QRect screenGeometry = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
        const double scaleX = static_cast<double>(m_itemSize.width()) / qMax(1, screenGeometry.width());
        const double scaleY = static_cast<double>(m_itemSize.height()) / qMax(1, screenGeometry.height());
        local = QRect(qRound((target.x() - screenGeometry.x()) * scaleX),
                      qRound((target.y() - screenGeometry.y()) * scaleY),
                      qRound(target.width() * scaleX),
                      qRound(target.height() * scaleY));
    }
    local = local.intersected(full.rect());
    if (local.isEmpty())
        return false;

    m_frameSize = local.size();
    frame = full.copy(local);
    return true;
}

} // namespace lightrec

#endif // LIGHTREC_HAVE_WGC
