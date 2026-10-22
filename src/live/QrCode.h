#pragma once

#include <QColor>
#include <QImage>
#include <QString>

namespace lightrec {

/**
 * Small self-contained QR encoder (byte mode, error correction level M,
 * versions 1-10) used for the LAN live URL.  No network service or third party
 * library is involved, which keeps the QR code available offline.
 */
class QrCode
{
public:
    static QImage encode(const QString &text, int moduleSize = 6, int borderModules = 4,
                         const QColor &foreground = QColor(0x10, 0x14, 0x1C),
                         const QColor &background = Qt::white);
};

} // namespace lightrec
