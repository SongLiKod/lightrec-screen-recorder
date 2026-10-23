#pragma once

#include "capture/ScreenCapture.h"

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>

namespace lightrec {

/**
 * Window capture based on PrintWindow(PW_CLIENTONLY | PW_RENDERFULLCONTENT).
 *
 * Unlike a Desktop Duplication crop, this reads the selected window's own
 * rendering surface, so covering it with another application does not change
 * the recorded content.  It is used as the MinGW fallback when Windows
 * Graphics Capture is unavailable.
 */
class PrintWindowCapture : public ICaptureBackend
{
public:
    PrintWindowCapture();
    ~PrintWindowCapture() override;

    bool start(const CaptureConfig &config, QString *error) override;
    void stop() override;
    bool updateConfig(const CaptureConfig &config, QString *error) override;
    bool grab(QImage &frame, int timeoutMs, QString *error) override;

    QRect captureRect() const override { return m_lastRect; }
    QSize sourceSize() const override { return m_lastSize; }
    QString name() const override { return QStringLiteral("Window PrintWindow"); }

private:
    bool captureFrame(QImage &frame, QString *error);
    QRect logicalClientRect(QRect *physicalRect) const;

    CaptureConfig m_config;
    QRect m_lastRect;
    QSize m_lastSize;
    bool m_running = false;
};

} // namespace lightrec
