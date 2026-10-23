#include "live/LiveServer.h"
#include "live/QrCode.h"

#include <QBuffer>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>

namespace lightrec {

namespace {

QByteArray reasonPhrase(int status)
{
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 404: return "Not Found";
    case 503: return "Service Unavailable";
    default: return "OK";
    }
}

QByteArray buildResponse(int status, const QByteArray &contentType, const QByteArray &body,
                         bool close = true)
{
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(status) + " " + reasonPhrase(status) + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Cache-Control: no-store, no-cache, must-revalidate\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    if (close)
        response += "Connection: close\r\n";
    response += "\r\n";
    response += body;
    return response;
}

} // namespace

LiveServer::LiveServer(QObject *parent)
    : QObject(parent)
{
    m_throttle.start();
}

LiveServer::~LiveServer()
{
    stop();
}

bool LiveServer::start(quint16 port, QString *error)
{
    stop();

    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::AnyIPv4, port)) {
        if (error)
            *error = QStringLiteral("无法启动局域网直播服务：%1").arg(m_server->errorString());
        delete m_server;
        m_server = nullptr;
        return false;
    }

    m_port = m_server->serverPort();
    m_url = QStringLiteral("http://%1:%2/").arg(localAddress()).arg(m_port);
    connect(m_server, &QTcpServer::newConnection, this, &LiveServer::handleNewConnection);
    return true;
}

void LiveServer::stop()
{
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value().socket) {
            it.value().socket->disconnect(this);
            it.value().socket->abort();
            it.value().socket->deleteLater();
        }
    }
    m_clients.clear();
    m_lastJpeg.clear();

    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    m_port = 0;
    m_url.clear();
    updateViewerCount();
}

bool LiveServer::isRunning() const
{
    return m_server && m_server->isListening();
}

int LiveServer::viewerCount() const
{
    int count = 0;
    for (const Client &client : m_clients) {
        if (client.streaming)
            ++count;
    }
    return count;
}

QImage LiveServer::qrCode(int size) const
{
    if (m_url.isEmpty())
        return QImage();
    return QrCode::encode(m_url, std::max(2, size / 45), 4);
}

QString LiveServer::localAddress() const
{
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &network : interfaces) {
        const auto flags = network.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)
            || (flags & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry &entry : network.addressEntries()) {
            const QHostAddress address = entry.ip();
            if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()
                && !address.isLinkLocal()) {
                return address.toString();
            }
        }
    }
    return QStringLiteral("127.0.0.1");
}

void LiveServer::handleNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket)
            continue;
        Client client;
        client.socket = socket;
        m_clients.insert(socket, client);

        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handleReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { handleDisconnected(socket); });
    }
}

void LiveServer::handleReadyRead(QTcpSocket *socket)
{
    auto it = m_clients.find(socket);
    if (it == m_clients.end())
        return;

    it.value().request += socket->readAll();
    if (it.value().streaming)
        return;
    if (!it.value().request.contains("\r\n\r\n") && it.value().request.size() < 16384)
        return;

    handleRequest(it.value(), it.value().request);
}

void LiveServer::handleRequest(Client &client, const QByteArray &request)
{
    QTcpSocket *socket = client.socket;
    if (!socket)
        return;

    const QByteArray requestLine = request.left(request.indexOf("\r\n"));
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) {
        sendSimple(socket, 404, "text/plain; charset=utf-8", "Bad Request");
        return;
    }
    QByteArray path = parts.at(1);
    const int query = path.indexOf('?');
    if (query >= 0)
        path = path.left(query);

    if (path == "/" || path == "/index.html") {
        QFile file(QStringLiteral(":/html/live.html"));
        QByteArray html;
        if (file.open(QIODevice::ReadOnly))
            html = file.readAll();
        if (html.isEmpty())
            html = "<html><body><h1>LightRec Live</h1></body></html>";
        sendSimple(socket, 200, "text/html; charset=utf-8", html);
        return;
    }

    if (path == "/api/status") {
        QJsonObject object;
        object[QStringLiteral("viewers")] = viewerCount();
        object[QStringLiteral("maxViewers")] = m_maxViewers;
        object[QStringLiteral("url")] = m_url;
        object[QStringLiteral("live")] = isRunning();
        const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
        sendSimple(socket, 200, "application/json; charset=utf-8", body);
        return;
    }

    if (path == "/qr.png") {
        const QImage image = qrCode(360);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        sendSimple(socket, 200, "image/png", png);
        return;
    }

    if (path == "/stream.mjpg") {
        if (viewerCount() >= m_maxViewers) {
            sendSimple(socket, 503, "text/html; charset=utf-8",
                       "<html><body style='font-family:sans-serif;padding:40px'>"
                       "<h2>观看人数已达上限</h2><p>请稍后再试。</p></body></html>");
            return;
        }

        QByteArray header;
        header += "HTTP/1.1 200 OK\r\n";
        header += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n";
        header += "Cache-Control: no-store, no-cache, must-revalidate\r\n";
        header += "Pragma: no-cache\r\n";
        header += "Connection: close\r\n";
        header += "Access-Control-Allow-Origin: *\r\n\r\n";
        socket->write(header);
        client.streaming = true;
        if (!m_lastJpeg.isEmpty()) {
            QByteArray frame;
            frame += "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ";
            frame += QByteArray::number(m_lastJpeg.size());
            frame += "\r\n\r\n";
            frame += m_lastJpeg;
            frame += "\r\n";
            socket->write(frame);
        }
        updateViewerCount();
        return;
    }

    if (path == "/favicon.ico") {
        socket->write(buildResponse(204, "image/x-icon", QByteArray()));
        socket->disconnectFromHost();
        return;
    }

    sendSimple(socket, 404, "text/plain; charset=utf-8", "Not Found");
}

void LiveServer::sendSimple(QTcpSocket *socket, int status, const QByteArray &contentType,
                            const QByteArray &body)
{
    if (!socket)
        return;
    socket->write(buildResponse(status, contentType, body));
    socket->disconnectFromHost();
}

void LiveServer::handleDisconnected(QTcpSocket *socket)
{
    if (!socket)
        return;
    const auto it = m_clients.find(socket);
    if (it != m_clients.end()) {
        m_clients.erase(it);
        updateViewerCount();
    }
    socket->deleteLater();
}

void LiveServer::pushFrame(const QImage &frame)
{
    if (!isRunning() || frame.isNull())
        return;
    // No viewers: don't spend CPU on JPEG encoding.  A newly connected viewer
    // receives the next frame (within ~70 ms).
    if (viewerCount() == 0) {
        m_lastJpeg.clear();
        return;
    }
    if (m_throttle.elapsed() < 70)
        return;
    m_throttle.restart();

    QImage image = frame;
    if (image.width() > 1280)
        image = image.scaledToWidth(1280, Qt::SmoothTransformation);

    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "JPEG", 72))
        return;
    m_lastJpeg = jpeg;

    QByteArray packet;
    packet += "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ";
    packet += QByteArray::number(m_lastJpeg.size());
    packet += "\r\n\r\n";
    packet += m_lastJpeg;
    packet += "\r\n";

    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        Client &client = it.value();
        if (!client.streaming || !client.socket)
            continue;
        if (client.socket->bytesToWrite() > 4 * 1024 * 1024)
            continue; // slow viewer: drop frames instead of blocking the server
        client.socket->write(packet);
    }
}

void LiveServer::updateViewerCount()
{
    const int count = viewerCount();
    if (count == m_lastViewerCount)
        return;
    m_lastViewerCount = count;
    emit viewerCountChanged(count);
}

} // namespace lightrec
