#include "ui/RegionSelector.h"

#include "core/WindowUtils.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>

#include <algorithm>

namespace lightrec {

namespace {
constexpr int kHandleSize = 10;
constexpr int kMinSize = 24;
} // namespace

RegionSelector::RegionSelector(const QRect &initialRegion, bool adjusting, QWidget *parent)
    : QWidget(parent)
    , m_region(initialRegion)
    , m_initialRegion(initialRegion)
    , m_adjusting(adjusting)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    setWindowTitle(QStringLiteral("选择录制区域"));
    setGeometry(WindowUtils::virtualDesktopGeometry());
}

void RegionSelector::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    WindowUtils::excludeFromCapture(winId());
    raise();
    activateWindow();
    setFocus(Qt::ActiveWindowFocusReason);
}

QRect RegionSelector::handleRect(Handle handle) const
{
    if (m_region.isNull())
        return {};
    const QPoint center = [&]() {
        switch (handle) {
        case Handle::TopLeft: return m_region.topLeft();
        case Handle::Top: return QPoint(m_region.center().x(), m_region.top());
        case Handle::TopRight: return m_region.topRight();
        case Handle::Right: return QPoint(m_region.right(), m_region.center().y());
        case Handle::BottomRight: return m_region.bottomRight();
        case Handle::Bottom: return QPoint(m_region.center().x(), m_region.bottom());
        case Handle::BottomLeft: return m_region.bottomLeft();
        case Handle::Left: return QPoint(m_region.left(), m_region.center().y());
        default: return m_region.center();
        }
    }();
    return QRect(center - QPoint(kHandleSize / 2, kHandleSize / 2), QSize(kHandleSize, kHandleSize));
}

RegionSelector::Handle RegionSelector::handleAt(const QPoint &position) const
{
    const QPoint local = position + geometry().topLeft();
    const Handle handles[] = { Handle::TopLeft, Handle::Top, Handle::TopRight, Handle::Right,
                               Handle::BottomRight, Handle::Bottom, Handle::BottomLeft, Handle::Left };
    for (Handle handle : handles) {
        if (handleRect(handle).adjusted(-4, -4, 4, 4).contains(local))
            return handle;
    }
    if (m_region.contains(local))
        return Handle::Inside;
    return Handle::None;
}

void RegionSelector::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    const QPoint global = event->globalPosition().toPoint();
    m_dragStart = global;
    m_dragStartRegion = m_region;
    m_activeHandle = handleAt(event->position().toPoint());

    if (m_activeHandle == Handle::None) {
        m_activeHandle = Handle::Inside;
        m_region = QRect(global, QSize(0, 0));
        m_dragStartRegion = m_region;
        m_selecting = true;
    } else {
        m_selecting = false;
    }
    update();
}

void RegionSelector::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint global = event->globalPosition().toPoint();

    if (m_activeHandle == Handle::None) {
        const Handle hover = handleAt(event->position().toPoint());
        switch (hover) {
        case Handle::TopLeft:
        case Handle::BottomRight: setCursor(Qt::SizeFDiagCursor); break;
        case Handle::TopRight:
        case Handle::BottomLeft: setCursor(Qt::SizeBDiagCursor); break;
        case Handle::Top:
        case Handle::Bottom: setCursor(Qt::SizeVerCursor); break;
        case Handle::Left:
        case Handle::Right: setCursor(Qt::SizeHorCursor); break;
        case Handle::Inside: setCursor(Qt::SizeAllCursor); break;
        default: setCursor(Qt::CrossCursor); break;
        }
        return;
    }

    updateRegionFromDrag(global);
    update();
}

void RegionSelector::updateRegionFromDrag(const QPoint &global)
{
    const QRect desktop = geometry();
    if (m_selecting) {
        QRect region = QRect(m_dragStart, global).normalized();
        region = region.intersected(desktop);
        m_region = region;
        return;
    }

    QRect region = m_dragStartRegion;
    const QPoint delta = global - m_dragStart;
    switch (m_activeHandle) {
    case Handle::Inside:
        region.translate(delta);
        break;
    case Handle::TopLeft:
        region.setTopLeft(region.topLeft() + delta);
        break;
    case Handle::Top:
        region.setTop(region.top() + delta.y());
        break;
    case Handle::TopRight:
        region.setTopRight(region.topRight() + delta);
        break;
    case Handle::Right:
        region.setRight(region.right() + delta.x());
        break;
    case Handle::BottomRight:
        region.setBottomRight(region.bottomRight() + delta);
        break;
    case Handle::Bottom:
        region.setBottom(region.bottom() + delta.y());
        break;
    case Handle::BottomLeft:
        region.setBottomLeft(region.bottomLeft() + delta);
        break;
    case Handle::Left:
        region.setLeft(region.left() + delta.x());
        break;
    default:
        break;
    }

    if (region.width() < kMinSize)
        region.setWidth(kMinSize);
    if (region.height() < kMinSize)
        region.setHeight(kMinSize);

    // Keep the region inside the virtual desktop.
    if (region.left() < desktop.left())
        region.moveLeft(desktop.left());
    if (region.top() < desktop.top())
        region.moveTop(desktop.top());
    if (region.right() > desktop.right())
        region.moveRight(desktop.right());
    if (region.bottom() > desktop.bottom())
        region.moveBottom(desktop.bottom());

    m_region = region;
}

void RegionSelector::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_activeHandle = Handle::None;
    m_selecting = false;
    if (m_region.width() < kMinSize || m_region.height() < kMinSize)
        m_region = m_initialRegion;
    update();
}

void RegionSelector::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_region.width() >= kMinSize
        && m_region.height() >= kMinSize) {
        confirm();
    }
}

void RegionSelector::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit canceled();
        close();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        confirm();
        return;
    }
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right
        || event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        const int step = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        if (event->key() == Qt::Key_Left)
            m_region.translate(-step, 0);
        else if (event->key() == Qt::Key_Right)
            m_region.translate(step, 0);
        else if (event->key() == Qt::Key_Up)
            m_region.translate(0, -step);
        else
            m_region.translate(0, step);
        m_region = m_region.intersected(geometry());
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

void RegionSelector::confirm()
{
    if (m_region.width() < kMinSize || m_region.height() < kMinSize)
        return;
    QRect region = m_region.normalized();
    // Even dimensions keep H.264 happy.
    if (region.width() & 1)
        region.setWidth(region.width() - 1);
    if (region.height() & 1)
        region.setHeight(region.height() - 1);
    emit regionSelected(region);
    close();
}

void RegionSelector::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(rect(), QColor(8, 12, 18, 150));

    if (m_region.isValid() && !m_region.isNull()) {
        const QRect local = m_region.translated(-geometry().topLeft());
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillRect(local, Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

        painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(local);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x2D, 0x8C, 0xF0));
        const Handle handles[] = { Handle::TopLeft, Handle::Top, Handle::TopRight, Handle::Right,
                                   Handle::BottomRight, Handle::Bottom, Handle::BottomLeft,
                                   Handle::Left };
        for (Handle handle : handles) {
            painter.drawRoundedRect(handleRect(handle).translated(-geometry().topLeft()), 2, 2);
        }

        const QString sizeText = QStringLiteral("%1 × %2").arg(m_region.width()).arg(m_region.height());
        QFont font(QStringLiteral("Microsoft YaHei UI"), 10);
        font.setBold(true);
        painter.setFont(font);
        const QFontMetrics metrics(font);
        const int textWidth = metrics.horizontalAdvance(sizeText) + 16;
        QRect badge(local.left(), std::max(0, local.top() - 28), textWidth, 22);
        if (badge.top() < 0)
            badge.moveTop(local.top() + 6);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x2D, 0x8C, 0xF0));
        painter.drawRoundedRect(badge, 6, 6);
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, sizeText);
    }

    const QString hint = m_adjusting
                             ? QStringLiteral("拖动边框或控制点调整选区，Enter 确认，Esc 取消（录制继续）")
                             : QStringLiteral("拖动鼠标框选录制区域，Enter 确认，Esc 取消");
    QFont hintFont(QStringLiteral("Microsoft YaHei UI"), 11);
    painter.setFont(hintFont);
    const QFontMetrics hintMetrics(hintFont);
    const int hintWidth = hintMetrics.horizontalAdvance(hint) + 28;
    QRect hintRect((width() - hintWidth) / 2, 28, hintWidth, 34);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(15, 20, 28, 220));
    painter.drawRoundedRect(hintRect, 8, 8);
    painter.setPen(QColor(0xE6, 0xED, 0xF6));
    painter.drawText(hintRect, Qt::AlignCenter, hint);
}

} // namespace lightrec
