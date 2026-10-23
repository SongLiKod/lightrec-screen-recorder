#pragma once

#include <QRect>
#include <QWidget>

namespace lightrec {

/**
 * Non-interactive, always-on-top recording range indicator.  The border is
 * excluded from screen capture (WDA_EXCLUDEFROMCAPTURE), so it is visible to
 * the user but never appears in the recorded video.
 */
class CaptureBorderWindow : public QWidget
{
    Q_OBJECT
public:
    explicit CaptureBorderWindow(QWidget *parent = nullptr);

    void showBorder(const QRect &captureRect);
    void setCaptureRect(const QRect &captureRect);
    void hideBorder();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QRect m_captureRect;
};

} // namespace lightrec
