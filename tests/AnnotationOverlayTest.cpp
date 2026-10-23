#include "overlay/AnnotationModel.h"
#include "overlay/AnnotationOverlay.h"
#include "overlay/AnnotationToolbar.h"
#include "overlay/OverlayCompositor.h"

#include <QApplication>
#include <QInputDialog>
#include <QTimer>
#include <QToolButton>
#include <QtTest/QTest>

using namespace lightrec;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    AnnotationModel model;
    AnnotationOverlay overlay(&model);
    overlay.setGeometry(0, 0, 800, 600);
    overlay.setDrawingEnabled(true);
    overlay.setTool(AnnotationTool::Text);
    overlay.show();

    // The text tool opens an application-modal QInputDialog from inside
    // mousePressEvent().  Fill and accept it from the event loop.
    QTimer::singleShot(200, [] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QInputDialog *>(widget)) {
                dialog->setTextValue(QStringLiteral("LightRec 文字标注"));
                dialog->accept();
                return;
            }
        }
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QDialog *>(widget))
                dialog->reject();
        }
    });

    QTest::mouseClick(&overlay, Qt::LeftButton, Qt::NoModifier, QPoint(200, 200));

    const auto strokes = model.strokes();
    if (strokes.size() != 1) {
        qCritical("Expected one text stroke, got %lld", static_cast<long long>(strokes.size()));
        return 1;
    }
    if (strokes.first().tool != AnnotationTool::Text
        || strokes.first().text != QStringLiteral("LightRec 文字标注")) {
        qCritical("Text stroke content mismatch");
        return 2;
    }

    QTest::keyClick(&overlay, Qt::Key_2);
    if (overlay.tool() != AnnotationTool::Line) {
        qCritical("Keyboard tool shortcut did not switch to line");
        return 3;
    }

    AnnotationToolbar toolbar;
    toolbar.show();
    toolbar.followAnchor(QRect(0, 0, 1600, 900), QRect(500, 10, 400, 80));
    const QPoint firstPosition = toolbar.pos();
    if (firstPosition.y() <= 90) {
        qCritical("Annotation toolbar did not move below the floating toolbar anchor");
        return 4;
    }
    toolbar.followAnchor(QRect(0, 0, 1600, 900), QRect(40, 500, 400, 80));
    if (toolbar.pos() == firstPosition) {
        qCritical("Annotation toolbar did not follow the moved anchor");
        return 5;
    }

    for (QToolButton *button : toolbar.findChildren<QToolButton *>()) {
        if (button->toolTip().contains(QStringLiteral("矩形"))) {
            QTest::mouseClick(button, Qt::LeftButton);
            break;
        }
    }
    if (toolbar.tool() != AnnotationTool::Rect) {
        qCritical("Clicking the toolbar tool button did not switch tools");
        return 6;
    }

    toolbar.setPenColorIndex(3);
    if (toolbar.penColor() != AnnotationToolbar::defaultColors().at(3)) {
        qCritical("Palette color selection failed");
        return 7;
    }
    const int beforeWidth = toolbar.penWidth();
    toolbar.adjustPenWidth(2);
    if (toolbar.penWidth() != qMin(24, beforeWidth + 2)) {
        qCritical("Pen width shortcut adjustment failed");
        return 8;
    }

    AnnotationModel liveModel;
    AnnotationOverlay liveOverlay(&liveModel);
    liveOverlay.setGeometry(0, 0, 800, 600);
    liveOverlay.setDrawingEnabled(true);
    liveOverlay.setTool(AnnotationTool::Pen);
    liveOverlay.setPenColor(Qt::red);
    liveOverlay.setPenWidth(8);
    liveOverlay.setWhiteboardEnabled(true);
    liveOverlay.show();
    QTest::qWait(50);
    QTest::mousePress(&liveOverlay, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTest::mouseMove(&liveOverlay, QPoint(200, 200), 10);
    QTest::mouseMove(&liveOverlay, QPoint(300, 100), 10);
    QTest::qWait(100);
    QImage liveImage(liveOverlay.size(), QImage::Format_ARGB32_Premultiplied);
    liveImage.fill(Qt::transparent);
    liveOverlay.render(&liveImage);
    int liveRed = 0;
    for (int y = 0; y < liveImage.height(); ++y) {
        for (int x = 0; x < liveImage.width(); ++x) {
            const QRgb pixel = liveImage.pixel(x, y);
            if (qRed(pixel) > 150 && qGreen(pixel) < 100 && qBlue(pixel) < 100)
                ++liveRed;
        }
    }
    QTest::mouseRelease(&liveOverlay, Qt::LeftButton, Qt::NoModifier, QPoint(300, 100));
    if (liveRed < 5) {
        qCritical("Whiteboard did not show the in-progress stroke (%d red pixels)", liveRed);
        return 13;
    }
    liveOverlay.hide();

    AnnotationStroke eraserTarget;
    eraserTarget.tool = AnnotationTool::Pen;
    eraserTarget.color = Qt::red;
    eraserTarget.width = 6;
    eraserTarget.start = QPointF(20, 20);
    eraserTarget.points = { QPointF(20, 20), QPointF(80, 80) };
    eraserTarget.end = QPointF(80, 80);
    model.addStroke(eraserTarget);
    if (model.eraseAt(QPointF(50, 50), 8) != 1) {
        qCritical("Eraser did not remove the touched stroke");
        return 9;
    }

    overlay.setWhiteboardEnabled(true);
    overlay.setGridEnabled(true);
    overlay.setGuidesEnabled(true);
    QImage whiteboard(overlay.size(), QImage::Format_ARGB32_Premultiplied);
    whiteboard.fill(Qt::transparent);
    overlay.render(&whiteboard);
    const QColor background = whiteboard.pixelColor(10, 10);
    if (background.alpha() < 200 || background.lightness() < 220) {
        qCritical("Whiteboard background was not rendered");
        return 10;
    }
    overlay.setWhiteboardEnabled(false);

    AnnotationStroke uiMagnifiedStroke;
    uiMagnifiedStroke.tool = AnnotationTool::Pen;
    uiMagnifiedStroke.color = QColor(255, 0, 0);
    uiMagnifiedStroke.width = 10;
    uiMagnifiedStroke.start = QPointF(120, 120);
    uiMagnifiedStroke.points = { QPointF(120, 120), QPointF(160, 160) };
    uiMagnifiedStroke.end = QPointF(160, 160);
    model.addStroke(uiMagnifiedStroke);
    overlay.setMagnifierRect(QRect(100, 100, 200, 150));
    overlay.setMagnifierOptions(400, 2.0);
    overlay.setMagnifierEnabled(true);
    QImage uiRendered(overlay.size(), QImage::Format_ARGB32_Premultiplied);
    uiRendered.fill(Qt::transparent);
    overlay.render(&uiRendered);
    const int uiMagnifierWidth = qBound(200, 400, overlay.width() / 2);
    const int uiMagnifierHeight = qMax(120, uiMagnifierWidth * 150 / 200);
    const int uiMagnifierLeft = overlay.width() - uiMagnifierWidth - 24;
    const int uiMagnifierTop = overlay.height() - uiMagnifierHeight - 24;
    int uiRedPixels = 0;
    for (int y = uiMagnifierTop; y < uiMagnifierTop + uiMagnifierHeight && y < uiRendered.height(); ++y) {
        for (int x = uiMagnifierLeft; x < uiMagnifierLeft + uiMagnifierWidth && x < uiRendered.width(); ++x) {
            const QRgb pixel = uiRendered.pixel(x, y);
            if (qRed(pixel) > 150 && qGreen(pixel) < 100 && qBlue(pixel) < 100)
                ++uiRedPixels;
        }
    }
    if (uiRedPixels < 5) {
        qCritical("Overlay magnifier did not include annotations (%d red pixels, size %dx%d, model %d)",
                  uiRedPixels, uiRendered.width(), uiRendered.height(), model.count());
        return 12;
    }
    overlay.setMagnifierEnabled(false);

    OverlayCompositor liveCompositor;
    liveCompositor.setOutputSize(QSize(800, 600));
    OverlayConfig liveConfig;
    liveConfig.overlaysVisible = true;
    liveConfig.showMouseHighlight = false;
    liveConfig.showKeys = false;
    liveCompositor.setConfig(liveConfig);
    AnnotationStroke inProgress;
    inProgress.tool = AnnotationTool::Pen;
    inProgress.color = QColor(255, 0, 0);
    inProgress.width = 10;
    inProgress.start = QPointF(100, 100);
    inProgress.points = { QPointF(100, 100), QPointF(300, 200) };
    inProgress.end = QPointF(300, 200);
    liveCompositor.setLiveStroke(inProgress, true);
    QImage blackScreen(800, 600, QImage::Format_ARGB32);
    blackScreen.fill(Qt::black);
    const QImage liveComposed = liveCompositor.compose(
        blackScreen, QRect(0, 0, 800, 600), 0);
    int liveComposedRed = 0;
    for (int y = 0; y < liveComposed.height(); ++y) {
        for (int x = 0; x < liveComposed.width(); ++x) {
            const QRgb pixel = liveComposed.pixel(x, y);
            if (qRed(pixel) > 150 && qGreen(pixel) < 100 && qBlue(pixel) < 100)
                ++liveComposedRed;
        }
    }
    if (liveComposedRed < 5) {
        qCritical("Compositor omitted the in-progress stroke (%d red pixels)", liveComposedRed);
        return 15;
    }
    liveCompositor.setLiveStroke(AnnotationStroke(), false);

    OverlayCompositor compositor;
    compositor.setOutputSize(QSize(800, 600));
    OverlayConfig magnifierConfig;
    magnifierConfig.overlaysVisible = true;
    magnifierConfig.showMouseHighlight = false;
    magnifierConfig.showKeys = false;
    magnifierConfig.magnifierEnabled = true;
    magnifierConfig.magnifierRect = QRect(100, 100, 200, 150);
    magnifierConfig.magnifierWidth = 400;
    compositor.setConfig(magnifierConfig);

    AnnotationStroke magnifiedStroke;
    magnifiedStroke.tool = AnnotationTool::Pen;
    magnifiedStroke.color = QColor(255, 0, 0);
    magnifiedStroke.width = 10;
    magnifiedStroke.start = QPointF(150, 150);
    magnifiedStroke.points = { QPointF(150, 150), QPointF(250, 250) };
    magnifiedStroke.end = QPointF(250, 250);
    compositor.setStrokes({ magnifiedStroke });

    QImage screen(800, 600, QImage::Format_ARGB32);
    screen.fill(Qt::black);
    const QImage composed = compositor.compose(screen, QRect(0, 0, 800, 600), 0);
    const QRect magnifierOutput(376, 276, 400, 300);
    int redPixels = 0;
    for (int y = magnifierOutput.top(); y <= magnifierOutput.bottom() && y < composed.height(); ++y) {
        for (int x = magnifierOutput.left(); x <= magnifierOutput.right() && x < composed.width(); ++x) {
            const QRgb pixel = composed.pixel(x, y);
            if (qRed(pixel) > 150 && qGreen(pixel) < 100 && qBlue(pixel) < 100)
                ++redPixels;
        }
    }
    if (redPixels < 5) {
        qCritical("Magnifier did not include annotation content (%d red pixels)", redPixels);
        return 11;
    }
    return 0;
}
