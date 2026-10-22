#include "core/SingleInstance.h"

#include <QLocalServer>
#include <QLocalSocket>

namespace lightrec {

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
    , m_key(QStringLiteral("LightRec.ScreenRecorder.SingleInstance"))
{
}

SingleInstance::~SingleInstance()
{
    if (m_server) {
        m_server->close();
        QLocalServer::removeServer(m_key);
    }
}

bool SingleInstance::tryStart()
{
    QLocalSocket probe;
    probe.connectToServer(m_key);
    if (probe.waitForConnected(250)) {
        probe.write("activate");
        probe.flush();
        probe.waitForBytesWritten(250);
        return false;
    }

    QLocalServer::removeServer(m_key);
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_key)) {
        // Another instance may have won the race; treat as already running.
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::handleConnection);
    return true;
}

void SingleInstance::notifyExistingInstance()
{
    QLocalSocket socket;
    socket.connectToServer(m_key);
    if (socket.waitForConnected(250)) {
        socket.write("activate");
        socket.flush();
        socket.waitForBytesWritten(250);
    }
}

void SingleInstance::handleConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        if (!socket)
            continue;
        connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
        if (socket->waitForReadyRead(100)) {
            const QByteArray data = socket->readAll();
            if (data.contains("activate"))
                emit activationRequested();
        }
        socket->disconnectFromServer();
    }
}

} // namespace lightrec
