#include "core/Types.h"
#include "media/FFmpegTool.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

bool run(const QString &program, const QStringList &arguments, QString *output)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished();
        if (output)
            *output = QStringLiteral("timeout");
        return false;
    }
    if (output)
        *output = QString::fromLocal8Bit(process.readAllStandardError())
                  + QString::fromLocal8Bit(process.readAllStandardOutput());
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

int brightPixels(const QString &path, const QRect &area)
{
    const QImage image(path);
    if (image.isNull())
        return -1;
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    const QRect clipped = area.intersected(rgb.rect());
    int bright = 0;
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
            const QRgb pixel = rgb.pixel(x, y);
            if (qRed(pixel) > 160 && qGreen(pixel) > 160 && qBlue(pixel) > 160)
                ++bright;
        }
    }
    return bright;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        const QString adjacent = QCoreApplication::applicationDirPath()
                                 + QStringLiteral("/ffmpeg.exe");
        if (QFileInfo::exists(adjacent))
            ffmpeg = adjacent;
    }
    if (ffmpeg.isEmpty()) {
        out << "ffmpeg not found in PATH or next to the test executable\n";
        return 2;
    }

    QTemporaryDir dir;
    if (!dir.isValid())
        return 2;

    const QString input = dir.filePath(QStringLiteral("input.mp4"));
    const QString textFrame = dir.filePath(QStringLiteral("text.png"));
    const QString imageFrame = dir.filePath(QStringLiteral("image.png"));
    const QString textOutput = dir.filePath(QStringLiteral("text.mp4"));
    const QString driftOutput = dir.filePath(QStringLiteral("drift.mp4"));
    const QString randomOutput = dir.filePath(QStringLiteral("random.mp4"));
    const QString imageOutput = dir.filePath(QStringLiteral("image.mp4"));
    const QString textFile = dir.filePath(QStringLiteral("watermark.txt"));
    const QString watermarkImage = dir.filePath(QStringLiteral("watermark.png"));

    QFile text(textFile);
    if (!text.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 2;
    text.write(QStringLiteral("测试水印").toUtf8());
    text.close();

    QImage imageSource(160, 80, QImage::Format_ARGB32_Premultiplied);
    imageSource.fill(Qt::white);
    if (!imageSource.save(watermarkImage))
        return 2;

    QString log;
    if (!run(ffmpeg,
             { QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
               QStringLiteral("-f"), QStringLiteral("lavfi"),
               QStringLiteral("-i"), QStringLiteral("color=c=black:s=640x360:d=2:r=30"),
               QStringLiteral("-c:v"), QStringLiteral("libx264"),
               QStringLiteral("-preset"), QStringLiteral("ultrafast"),
               QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), input },
             &log)) {
        out << "failed to create input: " << log << "\n";
        return 2;
    }

    lightrec::WatermarkConfig textConfig;
    textConfig.enabled = true;
    textConfig.text = QStringLiteral("TESTWM");
    textConfig.position = QStringLiteral("bottom-right");
    textConfig.margin = 20;
    textConfig.fontSize = 48;
    textConfig.color = Qt::white;
    textConfig.opacity = 100;
    textConfig.scale = 0.18;

    const QStringList textArgs =
        lightrec::FFmpegTool::watermarkArguments(input, textOutput, textConfig, textFile);
    if (!run(ffmpeg, textArgs, &log)) {
        out << "text watermark failed: " << log << "\n";
        return 1;
    }
    if (!run(ffmpeg,
             { QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
               QStringLiteral("-ss"), QStringLiteral("0.5"), QStringLiteral("-i"), textOutput,
               QStringLiteral("-frames:v"), QStringLiteral("1"), textFrame },
             &log)) {
        out << "text frame failed: " << log << "\n";
        return 1;
    }
    const int textBright = brightPixels(textFrame, QRect(320, 180, 320, 180));
    out << "text bottom-right bright pixels: " << textBright << "\n";
    if (textBright <= 10)
        return 1;

    lightrec::WatermarkConfig driftConfig = textConfig;
    driftConfig.position = QStringLiteral("drift");
    if (!run(ffmpeg, lightrec::FFmpegTool::watermarkArguments(input, driftOutput, driftConfig, textFile),
             &log)) {
        out << "drift watermark failed: " << log << "\n";
        return 1;
    }
    const QString driftFrame = dir.filePath(QStringLiteral("drift.png"));
    if (!run(ffmpeg,
             { QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
               QStringLiteral("-ss"), QStringLiteral("0.8"), QStringLiteral("-i"), driftOutput,
               QStringLiteral("-frames:v"), QStringLiteral("1"), driftFrame },
             &log)) {
        out << "drift frame failed: " << log << "\n";
        return 1;
    }
    if (brightPixels(driftFrame, QRect(0, 0, 640, 360)) <= 10) {
        out << "drift watermark is not visible\n";
        return 1;
    }

    lightrec::WatermarkConfig randomConfig = textConfig;
    randomConfig.position = QStringLiteral("random");
    if (!run(ffmpeg,
             lightrec::FFmpegTool::watermarkArguments(input, randomOutput, randomConfig, textFile),
             &log)) {
        out << "random watermark failed: " << log << "\n";
        return 1;
    }

    lightrec::WatermarkConfig imageConfig;
    imageConfig.enabled = true;
    imageConfig.imagePath = watermarkImage;
    imageConfig.position = QStringLiteral("top-left");
    imageConfig.margin = 20;
    imageConfig.opacity = 100;
    imageConfig.scale = 0.18;

    const QStringList imageArgs =
        lightrec::FFmpegTool::watermarkArguments(input, imageOutput, imageConfig, QString());
    if (!run(ffmpeg, imageArgs, &log)) {
        out << "image watermark failed: " << log << "\n";
        return 1;
    }
    if (!run(ffmpeg,
             { QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
               QStringLiteral("-ss"), QStringLiteral("0.5"), QStringLiteral("-i"), imageOutput,
               QStringLiteral("-frames:v"), QStringLiteral("1"), imageFrame },
             &log)) {
        out << "image frame failed: " << log << "\n";
        return 1;
    }
    const int imageBright = brightPixels(imageFrame, QRect(0, 0, 320, 180));
    out << "image top-left bright pixels: " << imageBright << "\n";
    if (imageBright <= 10)
        return 1;

    imageConfig.position = QStringLiteral("drift");
    const QString imageDrift = dir.filePath(QStringLiteral("image-drift.mp4"));
    if (!run(ffmpeg,
             lightrec::FFmpegTool::watermarkArguments(input, imageDrift, imageConfig, QString()),
             &log)) {
        out << "image drift watermark failed: " << log << "\n";
        return 1;
    }
    return 0;
}
