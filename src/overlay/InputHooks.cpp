#include "overlay/InputHooks.h"

#include <QDateTime>
#include <QCursor>
#include <QDebug>

#include <windows.h>

namespace lightrec {

namespace {
InputHooks *g_instance = nullptr;
}

InputHooks &InputHooks::instance()
{
    static InputHooks hooks;
    return hooks;
}

InputHooks::InputHooks(QObject *parent)
    : QObject(parent)
{
    g_instance = this;
}

InputHooks::~InputHooks()
{
    uninstall();
    if (g_instance == this)
        g_instance = nullptr;
}

bool InputHooks::install()
{
    if (m_installed)
        return true;

    HINSTANCE instance = GetModuleHandleW(nullptr);
    m_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, &InputHooks::keyboardProc, instance, 0);
    m_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, &InputHooks::mouseProc, instance, 0);
    m_installed = m_keyboardHook != nullptr && m_mouseHook != nullptr;

    if (!m_installed) {
        qWarning() << "Failed to install input hooks" << GetLastError();
        uninstall();
    }
    return m_installed;
}

void InputHooks::uninstall()
{
    if (m_keyboardHook) {
        UnhookWindowsHookEx(static_cast<HHOOK>(m_keyboardHook));
        m_keyboardHook = nullptr;
    }
    if (m_mouseHook) {
        UnhookWindowsHookEx(static_cast<HHOOK>(m_mouseHook));
        m_mouseHook = nullptr;
    }
    m_installed = false;
}

QString InputHooks::keyName(unsigned int vk)
{
    if (vk >= 'A' && vk <= 'Z')
        return QString(QChar(static_cast<char16_t>(vk)));
    if (vk >= '0' && vk <= '9')
        return QString(QChar(static_cast<char16_t>(vk)));
    if (vk >= VK_F1 && vk <= VK_F24)
        return QStringLiteral("F%1").arg(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return QStringLiteral("Num%1").arg(vk - VK_NUMPAD0);

    switch (vk) {
    case VK_SPACE: return QStringLiteral("Space");
    case VK_RETURN: return QStringLiteral("Enter");
    case VK_ESCAPE: return QStringLiteral("Esc");
    case VK_TAB: return QStringLiteral("Tab");
    case VK_BACK: return QStringLiteral("Backspace");
    case VK_DELETE: return QStringLiteral("Delete");
    case VK_INSERT: return QStringLiteral("Insert");
    case VK_HOME: return QStringLiteral("Home");
    case VK_END: return QStringLiteral("End");
    case VK_PRIOR: return QStringLiteral("PgUp");
    case VK_NEXT: return QStringLiteral("PgDn");
    case VK_LEFT: return QStringLiteral("←");
    case VK_RIGHT: return QStringLiteral("→");
    case VK_UP: return QStringLiteral("↑");
    case VK_DOWN: return QStringLiteral("↓");
    case VK_SNAPSHOT: return QStringLiteral("PrtSc");
    case VK_PAUSE: return QStringLiteral("Pause");
    case VK_CAPITAL: return QStringLiteral("CapsLock");
    case VK_NUMLOCK: return QStringLiteral("NumLock");
    case VK_SCROLL: return QStringLiteral("ScrollLock");
    case VK_OEM_1: return QStringLiteral(";");
    case VK_OEM_PLUS: return QStringLiteral("=");
    case VK_OEM_COMMA: return QStringLiteral(",");
    case VK_OEM_MINUS: return QStringLiteral("-");
    case VK_OEM_PERIOD: return QStringLiteral(".");
    case VK_OEM_2: return QStringLiteral("/");
    case VK_OEM_3: return QStringLiteral("`");
    case VK_OEM_4: return QStringLiteral("[");
    case VK_OEM_5: return QStringLiteral("\\");
    case VK_OEM_6: return QStringLiteral("]");
    case VK_OEM_7: return QStringLiteral("'");
    default: return QString();
    }
}

LRESULT CALLBACK InputHooks::keyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && g_instance) {
        const auto *info = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            const unsigned int vk = info->vkCode;
            const bool isModifier = vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL
                                    || vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU
                                    || vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT
                                    || vk == VK_LWIN || vk == VK_RWIN;
            if (!isModifier) {
                QString name = keyName(vk);
                if (!name.isEmpty()) {
                    QStringList parts;
                    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
                        parts << QStringLiteral("Ctrl");
                    if (GetAsyncKeyState(VK_MENU) & 0x8000)
                        parts << QStringLiteral("Alt");
                    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
                        parts << QStringLiteral("Shift");
                    if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000))
                        parts << QStringLiteral("Win");
                    parts << name;

                    const QString combination = parts.join(QLatin1Char('+'));
                    const qint64 now = QDateTime::currentMSecsSinceEpoch();
                    if (combination != g_instance->m_lastCombination
                        || now - g_instance->m_lastCombinationMs > 180) {
                        g_instance->m_lastCombination = combination;
                        g_instance->m_lastCombinationMs = now;
                        emit g_instance->keyPressed(combination);
                    }
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK InputHooks::mouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && g_instance) {
        const auto *info = reinterpret_cast<MSLLHOOKSTRUCT *>(lParam);
        Q_UNUSED(info)
        // MSLLHOOKSTRUCT is in physical pixels; QCursor::pos() is in Qt's
        // logical coordinate space and matches the overlay windows on high-DPI
        // desktops.
        const QPoint position = QCursor::pos();
        if (wParam == WM_LBUTTONDOWN)
            emit g_instance->mouseClicked(Qt::LeftButton, position);
        else if (wParam == WM_RBUTTONDOWN)
            emit g_instance->mouseClicked(Qt::RightButton, position);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

} // namespace lightrec
