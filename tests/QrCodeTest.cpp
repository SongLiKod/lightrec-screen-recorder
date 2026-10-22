#include "live/QrCode.h"

#include <QGuiApplication>
#include <QImage>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QString path = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                  : QStringLiteral("lightrec_qr_test.png");
    const QImage image = lightrec::QrCode::encode(
        QStringLiteral("http://192.168.1.100:8088/"), 6, 4);
    if (image.isNull())
        return 1;
    return image.save(path) ? 0 : 2;
}
