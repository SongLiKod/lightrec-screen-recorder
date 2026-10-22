#pragma once

#include "core/Types.h"

#include <QColor>
#include <QWidget>

class QButtonGroup;
class QSpinBox;
class QToolButton;

namespace lightrec {

class AnnotationToolbar : public QWidget
{
    Q_OBJECT
public:
    explicit AnnotationToolbar(QWidget *parent = nullptr);

    void setTool(AnnotationTool tool);
    AnnotationTool tool() const { return m_tool; }
    void setPenColor(const QColor &color);
    QColor penColor() const { return m_penColor; }
    void setPenWidth(int width);
    int penWidth() const;

    void syncFromConfig(const OverlayConfig &config);
    void showCentered();

signals:
    void toolChanged(lightrec::AnnotationTool tool);
    void penColorChanged(const QColor &color);
    void penWidthChanged(int width);
    void undoRequested();
    void clearRequested();
    void hideRequested();
    void closeRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateColorButton();
    QToolButton *makeToolButton(AnnotationTool tool, const QString &text, const QString &tip);

    QButtonGroup *m_toolGroup = nullptr;
    QToolButton *m_colorButton = nullptr;
    QSpinBox *m_widthSpin = nullptr;
    AnnotationTool m_tool = AnnotationTool::Pen;
    QColor m_penColor = Qt::red;
    bool m_dragging = false;
    QPoint m_dragOffset;
};

} // namespace lightrec
