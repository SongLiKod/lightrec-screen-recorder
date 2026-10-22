#pragma once

#include <QDialog>

class QLabel;
class QMediaPlayer;
class QAudioOutput;
class QSlider;
class QVideoWidget;

namespace lightrec {

class PreviewDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreviewDialog(const QString &filePath, QWidget *parent = nullptr);
    ~PreviewDialog() override;

protected:
    void showEvent(QShowEvent *event) override;

private:
    void updatePosition(qint64 position);
    void updateDuration(qint64 duration);
    void togglePlay();

    QString m_filePath;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QVideoWidget *m_videoWidget = nullptr;
    QSlider *m_positionSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    bool m_playing = false;
};

} // namespace lightrec
