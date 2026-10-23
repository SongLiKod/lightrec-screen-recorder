#include "ui/Widgets.h"

#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <QTimer>
#include <QtTest/QTest>

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    QApplication app(argc, argv);

    bool dialogSeen = false;
    QTimer poll;
    poll.setInterval(50);
    QObject::connect(&poll, &QTimer::timeout, [&dialogSeen, &poll] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QFileDialog *>(widget)) {
                dialogSeen = true;
                dialog->reject();
                poll.stop();
                return;
            }
        }
    });
    poll.start();
    QTimer::singleShot(10000, [] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QDialog *>(widget))
                dialog->reject();
        }
    });

    lightrec::ui::openFileDialog(nullptr, QStringLiteral("File dialog smoke test"));
    poll.stop();
    return dialogSeen ? 0 : 1;
}
