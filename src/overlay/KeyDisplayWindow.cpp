#include "overlay/KeyDisplayWindow.h"
#include "core/WindowUtils.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace lightrec {

KeyDisplayWindow::KeyDisplayWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setWindowTitle(QStringLiteral("按键显示"));

    m_cleanupTimer.setInterval(250);
    connect(&m_cleanupTimer, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const int before = m_entries.size();
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                       [now](const KeyEntry &entry) {
                                           return now - entry.startMs > 3200;
                                       }),
                        m_entries.end());
        if (m_entries.size() != before) {
            setAttribute(Qt::WA_TransparentForMouseEvents, m_entries.isEmpty());
            updateSize();
            update();
        }
    });
    m_cleanupTimer.start();

    setStyle(22, 85);
    resize(280, 100);
}

void KeyDisplayWindow::setEntries(const QVector<KeyEntry> &entries)
{
    m_entries = entries;
    while (m_entries.size() > 3)
        m_entries.removeFirst();
    setAttribute(Qt::WA_TransparentForMouseEvents, m_entries.isEmpty());
    updateSize();
    update();
}

void KeyDisplayWindow::setStyle(int fontSize, int opacity)
{
    m_fontSize = qBound(10, fontSize, 72);
    m_opacity = qBound(0, opacity, 100);
    updateSize();
    update();
}

void KeyDisplayWindow::setGlobalPosition(const QPoint &position)
{
    move(position);
    emit positionChanged(position);
}

void KeyDisplayWindow::updateSize()
{
    QFont font(QStringLiteral("Microsoft YaHei UI"), m_fontSize, QFont::DemiBold);
    const QFontMetrics metrics(font);
    int textWidth = 0;
    for (const KeyEntry &entry : m_entries)
        textWidth = std::max(textWidth, metrics.horizontalAdvance(entry.text));
    const int panelWidth = std::max(120, textWidth + 32);
    const int panelHeight = std::max(54, (metrics.height() + 10)
                                             * std::max(1, static_cast<int>(m_entries.size())) + 32);
    if (width() != panelWidth || height() != panelHeight)
        resize(panelWidth, panelHeight);
}

void KeyDisplayWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (m_entries.isEmpty())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(m_opacity / 100.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x0F, 0x14, 0x1C, 215));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 10, 10);

    QFont font(QStringLiteral("Microsoft YaHei UI"), m_fontSize, QFont::DemiBold);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const int lineHeight = metrics.height() + 10;
    painter.setPen(QColor(0xF2, 0xF6, 0xFC));
    int y = 16 + metrics.ascent();
    for (const KeyEntry &entry : m_entries) {
        painter.drawText(16, y, entry.text);
        y += lineHeight;
    }
}

void KeyDisplayWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void KeyDisplayWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void KeyDisplayWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_dragging = false;
        emit positionChanged(frameGeometry().topLeft());
    }
    QWidget::mouseReleaseEvent(event);
}

} // namespace lightrec
