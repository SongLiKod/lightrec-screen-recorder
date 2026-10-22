#pragma once

#include <QObject>

class QLocalServer;

namespace lightrec {

class SingleInstance : public QObject
{
    Q_OBJECT
public:
    explicit SingleInstance(QObject *parent = nullptr);
    ~SingleInstance() override;

    bool tryStart();
    void notifyExistingInstance();

signals:
    void activationRequested();

private:
    void handleConnection();

    QLocalServer *m_server = nullptr;
    QString m_key;
};

} // namespace lightrec
