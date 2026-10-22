#pragma once

#include "capture/ScreenCapture.h"
#include "core/Types.h"

#include <QImage>

#include <atomic>
#include <condition_variable>
#include <mutex>

#ifdef LIGHTREC_HAVE_WGC

#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <d3d11.h>
#include <wrl/client.h>

namespace lightrec {

/**
 * Windows Graphics Capture backend (Windows 10 1903+).
 * Used as the preferred capture path; ScreenCapture falls back to DXGI when
 * the API is unavailable (older builds, disabled by group policy, ...).
 */
class WgcCapture : public ICaptureBackend
{
public:
    WgcCapture();
    ~WgcCapture() override;

    bool start(const CaptureConfig &config, QString *error) override;
    void stop() override;
    bool updateConfig(const CaptureConfig &config, QString *error) override;
    bool grab(QImage &frame, int timeoutMs, QString *error) override;
    QRect captureRect() const override { return m_lastRect; }
    QSize sourceSize() const override { return m_frameSize; }
    QString name() const override { return QStringLiteral("Windows Graphics Capture"); }

private:
    bool createItem(QString *error);
    bool createDevice(QString *error);
    bool createPool(QString *error);
    void onFrameArrived(const winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame &frame);
    QRect resolveRect() const;

    CaptureConfig m_config;
    QRect m_lastRect;
    QSize m_itemSize;
    QSize m_frameSize;

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_pool{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_directDevice{ nullptr };

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_staging;

    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame m_pendingFrame{ nullptr };
    std::mutex m_frameMutex;
    std::condition_variable m_frameCondition;
    std::atomic<bool> m_running{ false };
    bool m_itemChanged = false;
    winrt::event_token m_frameToken{};
    winrt::event_token m_sizeToken{};
};

} // namespace lightrec

#endif // LIGHTREC_HAVE_WGC
