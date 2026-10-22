#pragma once

#include <QColor>
#include <QString>

class QFrame;
class QLabel;
class QPushButton;
class QWidget;

namespace lightrec::ui {

QFrame *card(QWidget *parent = nullptr);
QLabel *sectionTitle(const QString &text, QWidget *parent = nullptr);
QLabel *mutedLabel(const QString &text, QWidget *parent = nullptr);
QLabel *valueLabel(const QString &text, QWidget *parent = nullptr);
QPushButton *primaryButton(const QString &text, QWidget *parent = nullptr);
QPushButton *secondaryButton(const QString &text, QWidget *parent = nullptr);
QPushButton *dangerButton(const QString &text, QWidget *parent = nullptr);
QPushButton *colorButton(const QColor &color, QWidget *parent = nullptr);
void updateColorButton(QPushButton *button, const QColor &color);
QWidget *labeledRow(const QString &label, QWidget *field, QWidget *parent = nullptr);
QWidget *labeledRow(const QString &label, QWidget *field, int labelWidth, QWidget *parent = nullptr);
void info(QWidget *parent, const QString &title, const QString &text);
void warn(QWidget *parent, const QString &title, const QString &text);
QString elidedPath(const QString &path, int maxLength = 52);

} // namespace lightrec::ui
