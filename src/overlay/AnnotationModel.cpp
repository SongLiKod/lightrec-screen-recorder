#include "overlay/AnnotationModel.h"

#include <QFontMetricsF>
#include <QMutexLocker>
#include <QPainterPath>
#include <QPainterPathStroker>

namespace lightrec {

namespace {

bool nearStroke(const AnnotationStroke &stroke, const QPointF &point, qreal radius)
{
    if (stroke.tool == AnnotationTool::Text) {
        const QFontMetricsF metrics(stroke.font);
        return metrics.boundingRect(stroke.text).translated(stroke.start)
            .adjusted(-radius, -radius, radius, radius)
            .contains(point);
    }

    QPainterPath path;
    switch (stroke.tool) {
    case AnnotationTool::Pen:
        if (stroke.points.isEmpty())
            return false;
        path.moveTo(stroke.points.first());
        for (int i = 1; i < stroke.points.size(); ++i)
            path.lineTo(stroke.points.at(i));
        if (stroke.points.size() == 1)
            path.lineTo(stroke.points.first() + QPointF(0.01, 0.01));
        break;
    case AnnotationTool::Line:
    case AnnotationTool::Arrow:
        path.moveTo(stroke.start);
        path.lineTo(stroke.end);
        break;
    case AnnotationTool::Rect:
        path.addRect(QRectF(stroke.start, stroke.end).normalized());
        break;
    case AnnotationTool::Ellipse:
        path.addEllipse(QRectF(stroke.start, stroke.end).normalized());
        break;
    default:
        return false;
    }

    QPainterPathStroker stroker;
    stroker.setWidth(stroke.width + radius * 2.0);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(path).contains(point);
}

} // namespace

QRectF AnnotationStroke::bounds() const
{
    QRectF rect(start, end);
    if (tool == AnnotationTool::Pen) {
        for (const QPointF &point : points)
            rect = rect.united(QRectF(point, QSizeF(0, 0)));
    }
    if (tool == AnnotationTool::Text) {
        const QFontMetricsF metrics(font);
        rect = metrics.boundingRect(text).translated(start);
    }
    return rect.normalized().adjusted(-width, -width, width, width);
}

AnnotationModel::AnnotationModel(QObject *parent)
    : QObject(parent)
{
}

void AnnotationModel::addStroke(const AnnotationStroke &stroke)
{
    {
        QMutexLocker locker(&m_mutex);
        m_strokes.append(stroke);
    }
    emit changed();
}

int AnnotationModel::eraseAt(const QPointF &point, qreal radius)
{
    int removed = 0;
    {
        QMutexLocker locker(&m_mutex);
        for (int i = m_strokes.size() - 1; i >= 0; --i) {
            if (nearStroke(m_strokes.at(i), point, qMax<qreal>(1.0, radius))) {
                m_strokes.removeAt(i);
                ++removed;
            }
        }
    }
    if (removed > 0)
        emit changed();
    return removed;
}

void AnnotationModel::undo()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_strokes.isEmpty())
            return;
        m_strokes.removeLast();
    }
    emit changed();
}

void AnnotationModel::clear()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_strokes.isEmpty())
            return;
        m_strokes.clear();
    }
    emit changed();
}

QVector<AnnotationStroke> AnnotationModel::strokes() const
{
    QMutexLocker locker(&m_mutex);
    return m_strokes;
}

int AnnotationModel::count() const
{
    QMutexLocker locker(&m_mutex);
    return m_strokes.size();
}

} // namespace lightrec
