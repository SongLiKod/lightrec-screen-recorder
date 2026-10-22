#include "ui/Widgets.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>

namespace lightrec::ui {

QFrame *card(QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("card"));
    return frame;
}

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("sectionTitle"));
    return label;
}

QLabel *mutedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("mutedLabel"));
    label->setWordWrap(true);
    return label;
}

QLabel *valueLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("valueLabel"));
    return label;
}

QPushButton *primaryButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("primaryButton"));
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QPushButton *secondaryButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QPushButton *dangerButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("dangerButton"));
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QPushButton *colorButton(const QColor &color, QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setFixedSize(30, 30);
    button->setCursor(Qt::PointingHandCursor);
    updateColorButton(button, color);
    return button;
}

void updateColorButton(QPushButton *button, const QColor &color)
{
    if (!button)
        return;
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(0x9A, 0xA8, 0xBA), 1));
    painter.setBrush(color);
    painter.drawEllipse(1, 1, 16, 16);
    painter.end();
    button->setIcon(QIcon(pixmap));
    button->setIconSize(QSize(18, 18));
    button->setProperty("colorValue", color.name(QColor::HexArgb));
}

QWidget *labeledRow(const QString &label, QWidget *field, QWidget *parent)
{
    return labeledRow(label, field, 92, parent);
}

QWidget *labeledRow(const QString &label, QWidget *field, int labelWidth, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *name = new QLabel(label, row);
    name->setObjectName(QStringLiteral("mutedLabel"));
    name->setMinimumWidth(labelWidth);
    name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(name);
    if (field) {
        field->setParent(row);
        layout->addWidget(field, 1);
    }
    return row;
}

void info(QWidget *parent, const QString &title, const QString &text)
{
    QMessageBox::information(parent, title, text);
}

void warn(QWidget *parent, const QString &title, const QString &text)
{
    QMessageBox::warning(parent, title, text);
}

QString elidedPath(const QString &path, int maxLength)
{
    if (path.length() <= maxLength)
        return path;
    const int head = maxLength / 2 - 2;
    const int tail = maxLength - head - 3;
    return path.left(head) + QStringLiteral("...") + path.right(tail);
}

} // namespace lightrec::ui
