#pragma once

#include "core/Types.h"

#include <QAbstractNativeEventFilter>
#include <QObject>

namespace lightrec {

class ThemeManager : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    static ThemeManager &instance();

    void initialize();
    void setMode(ThemeMode mode);
    ThemeMode mode() const { return m_mode; }
    bool isDark() const { return m_dark; }
    QString currentStyleSheet() const;

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void themeChanged(bool dark);

private:
    explicit ThemeManager(QObject *parent = nullptr);
    void refreshSystemTheme();
    void apply(bool force = false);

    ThemeMode m_mode = ThemeMode::System;
    bool m_dark = false;
    bool m_initialized = false;
};

} // namespace lightrec
