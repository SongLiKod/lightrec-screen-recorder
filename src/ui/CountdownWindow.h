#pragma once

#include <QString>
#include <QWidget>

class QCloseEvent;
class QTimer;

namespace lightrec {

class CountdownWindow : public QWidget
{
    Q_OBJECT
public:
    explicit CountdownWindow(int seconds, const QString &modeText = {},
                             const QString &detailText = {}, QWidget *parent = nullptr);

    void start();

signals:
    void finished();
    void canceled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    int m_remaining = 0;
    QString m_modeText;
    QString m_detailText;
    QTimer *m_timer = nullptr;
};

} // namespace lightrec
