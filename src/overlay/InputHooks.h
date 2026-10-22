#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <Qt>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace lightrec {

/**
 * Low-level keyboard and mouse hooks used for the key display overlay and the
 * click ripples.  Hooks are installed on the GUI thread and only observe input;
 * they never block or modify events.
 */
class InputHooks : public QObject
{
    Q_OBJECT
public:
    static InputHooks &instance();

    bool install();
    void uninstall();
    bool isInstalled() const { return m_installed; }

signals:
    void keyPressed(const QString &combination);
    void mouseClicked(Qt::MouseButton button, const QPoint &globalPosition);

private:
    explicit InputHooks(QObject *parent = nullptr);
    ~InputHooks() override;

    static QString keyName(unsigned int vk);
    static LRESULT CALLBACK keyboardProc(int code, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK mouseProc(int code, WPARAM wParam, LPARAM lParam);

    bool m_installed = false;
    void *m_keyboardHook = nullptr;
    void *m_mouseHook = nullptr;
    QString m_lastCombination;
    qint64 m_lastCombinationMs = 0;
};

} // namespace lightrec
