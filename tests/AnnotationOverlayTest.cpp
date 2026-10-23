#include "overlay/AnnotationModel.h"
#include "overlay/AnnotationOverlay.h"
#include "overlay/AnnotationToolbar.h"

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
    return 0;
}
