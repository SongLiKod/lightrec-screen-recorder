#pragma once

#include "core/Types.h"

#include <QColor>
#include <QFont>
#include <QMutex>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QVector>

namespace lightrec {

struct AnnotationStroke {
    AnnotationTool tool = AnnotationTool::None;
    QColor color = Qt::red;
    int width = 4;
    QPointF start;
    QPointF end;
    QVector<QPointF> points; // freehand pen
    QString text;
    QFont font;

    QRectF bounds() const;
};

class AnnotationModel : public QObject
{
    Q_OBJECT
public:
    explicit AnnotationModel(QObject *parent = nullptr);

    void addStroke(const AnnotationStroke &stroke);
    void undo();
    void clear();
    QVector<AnnotationStroke> strokes() const;
    int count() const;

signals:
    void changed();

private:
    mutable QMutex m_mutex;
    QVector<AnnotationStroke> m_strokes;
};

} // namespace lightrec
