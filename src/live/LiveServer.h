#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;

namespace lightrec {

/**
 * Lightweight MJPEG live server for the local network.  Built on QTcpServer so
 * that every connected browser can receive a continuous multipart stream with
 * very little overhead.  At least 16 viewers are supported; excess connections
 * receive a friendly 503 page.
 */
class LiveServer : public QObject
{
    Q_OBJECT
public:
    explicit LiveServer(QObject *parent = nullptr);
    ~LiveServer() override;

    bool start(quint16 port, QString *error);
    void stop();
    bool isRunning() const;
    quint16 port() const { return m_port; }
    QString url() const { return m_url; }
    QImage qrCode(int size = 260) const;
    int viewerCount() const;
    int maxViewers() const { return m_maxViewers; }

public slots:
    void pushFrame(const QImage &frame);

signals:
    void viewerCountChanged(int count);
    void errorOccurred(const QString &message);

private:
    struct Client {
        QTcpSocket *socket = nullptr;
        QByteArray request;
        bool streaming = false;
    };

    void handleNewConnection();
    void handleReadyRead(QTcpSocket *socket);
    void handleDisconnected(QTcpSocket *socket);
    void handleRequest(Client &client, const QByteArray &request);
    void sendSimple(QTcpSocket *socket, int status, const QByteArray &contentType,
                    const QByteArray &body);
    void broadcast(const QByteArray &jpeg);
    void updateViewerCount();
    QString localAddress() const;

    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, Client> m_clients;
    QByteArray m_lastJpeg;
    quint16 m_port = 0;
    QString m_url;
    int m_maxViewers = 16;
    int m_lastViewerCount = 0;
    QElapsedTimer m_throttle;
};

} // namespace lightrec
