#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QObject>
#include <QString>

namespace lightrec {

class HotkeyManager : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    static HotkeyManager &instance();

    void initialize();
    bool registerHotkey(const QString &action, const QString &sequence);
    void unregisterHotkey(const QString &action);
    void unregisterAll();
    QString registeredSequence(const QString &action) const;
    QString lastError() const { return m_lastError; }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void hotkeyPressed(const QString &action);

private:
    explicit HotkeyManager(QObject *parent = nullptr);
    ~HotkeyManager() override;

    QHash<QString, int> m_actionToId;
    QHash<int, QString> m_idToAction;
    QHash<QString, QString> m_sequences;
    int m_nextId = 0x4C00;
    QString m_lastError;
    bool m_installed = false;
};

} // namespace lightrec
