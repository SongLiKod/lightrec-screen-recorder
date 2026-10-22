#pragma once

#include <QWidget>

class QTimer;

namespace lightrec {

class CountdownWindow : public QWidget
{
    Q_OBJECT
public:
    explicit CountdownWindow(int seconds, QWidget *parent = nullptr);

    void start();

signals:
    void finished();
    void canceled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    int m_remaining = 0;
    QTimer *m_timer = nullptr;
};

} // namespace lightrec
