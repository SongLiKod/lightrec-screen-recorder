#include "core/HotkeyManager.h"
#include "core/ConfigStore.h"
#include "core/Logger.h"

#include <QApplication>
#include <QKeySequence>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace lightrec {

namespace {

#ifdef Q_OS_WIN
UINT virtualKeyForQtKey(Qt::Key key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return static_cast<UINT>('A' + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return static_cast<UINT>('0' + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return VK_F1 + static_cast<UINT>(key - Qt::Key_F1);

    switch (key) {
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Escape: return VK_ESCAPE;
    case Qt::Key_Tab: return VK_TAB;
    case Qt::Key_Backtab: return VK_TAB;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Return:
    case Qt::Key_Enter: return VK_RETURN;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Delete: return VK_DELETE;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    case Qt::Key_Print: return VK_SNAPSHOT;
    case Qt::Key_Pause: return VK_PAUSE;
    case Qt::Key_CapsLock: return VK_CAPITAL;
    case Qt::Key_NumLock: return VK_NUMLOCK;
    case Qt::Key_ScrollLock: return VK_SCROLL;
    case Qt::Key_Minus: return VK_OEM_MINUS;
    case Qt::Key_Plus:
    case Qt::Key_Equal: return VK_OEM_PLUS;
    case Qt::Key_Comma: return VK_OEM_COMMA;
    case Qt::Key_Period: return VK_OEM_PERIOD;
    case Qt::Key_Slash: return VK_OEM_2;
    case Qt::Key_Backslash: return VK_OEM_5;
    case Qt::Key_Semicolon: return VK_OEM_1;
    case Qt::Key_Apostrophe: return VK_OEM_7;
    case Qt::Key_BracketLeft: return VK_OEM_4;
    case Qt::Key_BracketRight: return VK_OEM_6;
    case Qt::Key_QuoteLeft: return VK_OEM_3;
    default: return 0;
    }
}
#endif

} // namespace

HotkeyManager &HotkeyManager::instance()
{
    static HotkeyManager manager;
    return manager;
}

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
{
}

HotkeyManager::~HotkeyManager()
{
    unregisterAll();
}

void HotkeyManager::initialize()
{
    if (m_installed)
        return;
    qApp->installNativeEventFilter(this);
    m_installed = true;
}

bool HotkeyManager::registerHotkey(const QString &action, const QString &sequence)
{
#ifdef Q_OS_WIN
    if (!m_installed)
        initialize();

    unregisterHotkey(action);

    const QKeySequence keySequence(sequence);
    if (keySequence.isEmpty()) {
        m_lastError = QStringLiteral("快捷键为空");
        return false;
    }

    const QKeyCombination combination = keySequence[0];
    UINT mods = MOD_NOREPEAT;
    const Qt::KeyboardModifiers qtMods = combination.keyboardModifiers();
    if (qtMods.testFlag(Qt::ControlModifier)) mods |= MOD_CONTROL;
    if (qtMods.testFlag(Qt::AltModifier)) mods |= MOD_ALT;
    if (qtMods.testFlag(Qt::ShiftModifier)) mods |= MOD_SHIFT;
    if (qtMods.testFlag(Qt::MetaModifier)) mods |= MOD_WIN;

    const UINT vk = virtualKeyForQtKey(combination.key());
    if (vk == 0) {
        m_lastError = QStringLiteral("不支持的快捷键：%1").arg(sequence);
        return false;
    }

    const int id = m_nextId++;
    if (!RegisterHotKey(nullptr, id, mods, vk)) {
        m_lastError = QStringLiteral("快捷键 %1 已被其他程序占用").arg(sequence);
        qWarning() << "RegisterHotKey failed" << sequence << GetLastError();
        return false;
    }

    m_actionToId.insert(action, id);
    m_idToAction.insert(id, action);
    m_sequences.insert(action, sequence);
    qInfo() << "Registered hotkey" << action << sequence;
    return true;
#else
    Q_UNUSED(action)
    Q_UNUSED(sequence)
    return false;
#endif
}

void HotkeyManager::unregisterHotkey(const QString &action)
{
#ifdef Q_OS_WIN
    const auto it = m_actionToId.find(action);
    if (it == m_actionToId.end())
        return;
    UnregisterHotKey(nullptr, it.value());
    m_idToAction.remove(it.value());
    m_actionToId.erase(it);
    m_sequences.remove(action);
#else
    Q_UNUSED(action)
#endif
}

void HotkeyManager::unregisterAll()
{
#ifdef Q_OS_WIN
    for (auto it = m_actionToId.constBegin(); it != m_actionToId.constEnd(); ++it)
        UnregisterHotKey(nullptr, it.value());
#endif
    m_actionToId.clear();
    m_idToAction.clear();
    m_sequences.clear();
}

QString HotkeyManager::registeredSequence(const QString &action) const
{
    return m_sequences.value(action);
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result)
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg && msg->message == WM_HOTKEY) {
            const int id = static_cast<int>(msg->wParam);
            const QString action = m_idToAction.value(id);
            if (!action.isEmpty()) {
                emit hotkeyPressed(action);
                return true;
            }
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
#endif
    return false;
}

} // namespace lightrec
