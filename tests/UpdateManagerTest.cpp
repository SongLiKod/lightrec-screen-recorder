#include "update/UpdateManager.h"

#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (lightrec::UpdateManager::compareVersions(QStringLiteral("1.1.0"),
                                                 QStringLiteral("1.2.0")) >= 0)
        return 1;
    if (lightrec::UpdateManager::compareVersions(QStringLiteral("v2.0.0"),
                                                 QStringLiteral("1.9.9")) <= 0)
        return 2;
    if (lightrec::UpdateManager::compareVersions(QStringLiteral("1.2.0"),
                                                 QStringLiteral("1.2.0")) != 0)
        return 3;
    if (lightrec::UpdateManager::currentVersion().isEmpty())
        return 4;
    return 0;
}
