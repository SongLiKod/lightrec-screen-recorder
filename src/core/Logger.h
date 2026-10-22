#pragma once

#include <QString>

namespace lightrec {

class Logger
{
public:
    static void install();
    static QString logFilePath();
    static void shutdown();

private:
    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message);
};

} // namespace lightrec
