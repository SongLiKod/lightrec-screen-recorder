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
    QApplication app(argc, argv);

    bool dialogSeen = false;
    QTimer::singleShot(300, [&dialogSeen] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QFileDialog *>(widget)) {
                dialogSeen = true;
                dialog->reject();
            }
        }
    });
    QTimer::singleShot(3000, [] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QDialog *>(widget))
                dialog->reject();
        }
    });

    lightrec::ui::openFileDialog(nullptr, QStringLiteral("File dialog smoke test"));
    return dialogSeen ? 0 : 1;
}
