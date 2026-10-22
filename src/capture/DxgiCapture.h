#pragma once

#include "capture/ScreenCapture.h"
#include "core/Types.h"

#include <QImage>
#include <QRect>
#include <QString>

#include <wrl/client.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct IDXGIOutputDuplication;
struct IDXGIAdapter1;
struct IDXGIOutput1;
struct IDXGIOutput;

namespace lightrec {

class DxgiCapture : public ICaptureBackend
{
public:
    DxgiCapture();
    ~DxgiCapture() override;

    bool start(const CaptureConfig &config, QString *error) override;
    void stop() override;
    bool updateConfig(const CaptureConfig &config, QString *error) override;
    bool grab(QImage &frame, int timeoutMs, QString *error) override;

    QRect captureRect() const override { return m_lastRect; }
    QSize sourceSize() const override { return m_lastPhysicalRect.size(); }
    QString name() const override { return QStringLiteral("DXGI Desktop Duplication"); }

private:
    bool initDevice(QString *error);
    bool selectOutput(const QRect &targetRect, QString *error);
    bool createDuplication(QString *error);
    void releaseDuplication();
    QRect resolveRect() const;
    bool copyToImage(QImage &image, QString *error);
    void drawCursor(QImage &image) const;
    QImage cursorImage(QPoint *hotspot) const;

    CaptureConfig m_config;
    QRect m_lastRect;          // logical / Qt coordinates
    QRect m_lastPhysicalRect;  // desktop pixels
    QSize m_outputSize;

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> m_adapter;
    Microsoft::WRL::ComPtr<IDXGIOutput> m_output;
    Microsoft::WRL::ComPtr<IDXGIOutput1> m_output1;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> m_duplication;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_staging;

    int m_outputIndex = -1;
    QRect m_outputRect;
    bool m_started = false;
};

} // namespace lightrec
