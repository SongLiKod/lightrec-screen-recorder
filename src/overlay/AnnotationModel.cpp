#include "overlay/AnnotationModel.h"

#include <QFontMetricsF>
#include <QMutexLocker>
#include <QPainterPath>

namespace lightrec {

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
