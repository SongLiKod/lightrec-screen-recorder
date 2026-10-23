#include "ui/PreviewDialog.h"

#include <QApplication>
#include <QDir>
#include <QtTest/QTest>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    lightrec::PreviewDialog dialog(QDir::tempPath() + QStringLiteral("/lightrec_missing_preview.mp4"));
    dialog.show();
    QTest::qWait(150);

    QTest::keyClick(&dialog, Qt::Key_F11);
    QTest::qWait(250);
    if (!dialog.isFullScreen())
        return 1;

    QTest::keyClick(&dialog, Qt::Key_Escape);
    QTest::qWait(250);
    return dialog.isFullScreen() ? 2 : 0;
}
