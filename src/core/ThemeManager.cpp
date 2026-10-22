#include "core/ThemeManager.h"
#include "core/ConfigStore.h"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace lightrec {

ThemeManager &ThemeManager::instance()
{
    static ThemeManager manager;
    return manager;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
}

void ThemeManager::initialize()
{
    m_mode = ConfigStore::instance().themeMode();
    refreshSystemTheme();
    qApp->installNativeEventFilter(this);
    m_initialized = true;
    apply(true);
}

void ThemeManager::setMode(ThemeMode mode)
{
    if (m_mode == mode && m_initialized)
        return;
    m_mode = mode;
    ConfigStore::instance().setThemeMode(mode);
    refreshSystemTheme();
    apply(true);
}

QString ThemeManager::currentStyleSheet() const
{
    QFile file(m_dark ? QStringLiteral(":/qss/dark.qss") : QStringLiteral(":/qss/light.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

void ThemeManager::refreshSystemTheme()
{
    if (m_mode == ThemeMode::Light) {
        m_dark = false;
        return;
    }
    if (m_mode == ThemeMode::Dark) {
        m_dark = true;
        return;
    }

#ifdef Q_OS_WIN
    QSettings personalize(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    m_dark = personalize.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
#else
    m_dark = QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
#endif
}

void ThemeManager::apply(bool force)
{
    static bool lastDark = false;
    if (!force && lastDark == m_dark)
        return;
    lastDark = m_dark;

    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    if (m_dark) {
        palette.setColor(QPalette::Window, QColor(0x1B, 0x1F, 0x27));
        palette.setColor(QPalette::WindowText, QColor(0xE6, 0xEA, 0xF1));
        palette.setColor(QPalette::Base, QColor(0x15, 0x18, 0x1E));
        palette.setColor(QPalette::AlternateBase, QColor(0x22, 0x27, 0x31));
        palette.setColor(QPalette::Text, QColor(0xE6, 0xEA, 0xF1));
        palette.setColor(QPalette::Button, QColor(0x25, 0x2B, 0x36));
        palette.setColor(QPalette::ButtonText, QColor(0xE6, 0xEA, 0xF1));
        palette.setColor(QPalette::Highlight, QColor(0x2D, 0x8C, 0xF0));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::ToolTipBase, QColor(0x25, 0x2B, 0x36));
        palette.setColor(QPalette::ToolTipText, QColor(0xE6, 0xEA, 0xF1));
        palette.setColor(QPalette::PlaceholderText, QColor(0x8A, 0x94, 0xA6));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x6B, 0x74, 0x85));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6B, 0x74, 0x85));
    } else {
        palette.setColor(QPalette::Window, QColor(0xF4, 0xF7, 0xFB));
        palette.setColor(QPalette::WindowText, QColor(0x25, 0x2F, 0x3D));
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::AlternateBase, QColor(0xF7, 0xF9, 0xFC));
        palette.setColor(QPalette::Text, QColor(0x25, 0x2F, 0x3D));
        palette.setColor(QPalette::Button, Qt::white);
        palette.setColor(QPalette::ButtonText, QColor(0x25, 0x2F, 0x3D));
        palette.setColor(QPalette::Highlight, QColor(0x2D, 0x8C, 0xF0));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::ToolTipBase, Qt::white);
        palette.setColor(QPalette::ToolTipText, QColor(0x25, 0x2F, 0x3D));
        palette.setColor(QPalette::PlaceholderText, QColor(0x9A, 0xA4, 0xB4));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0xB0, 0xB8, 0xC4));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0xB0, 0xB8, 0xC4));
    }

    QApplication::setPalette(palette);
    qApp->setStyleSheet(currentStyleSheet());

    for (QWidget *widget : QApplication::topLevelWidgets()) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }

    emit themeChanged(m_dark);
}

bool ThemeManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result)
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg && msg->message == WM_SETTINGCHANGE && m_mode == ThemeMode::System) {
            const wchar_t *area = reinterpret_cast<const wchar_t *>(msg->lParam);
            if (!area || wcscmp(area, L"ImmersiveColorSet") == 0 || wcscmp(area, L"Personalize") == 0) {
                refreshSystemTheme();
                apply();
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
