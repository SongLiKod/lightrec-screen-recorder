#pragma once

#include "media/MediaProbe.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QMediaPlayer;
class QAudioOutput;
class QPushButton;
class QSlider;
class QVideoWidget;

namespace lightrec {

class FFmpegTool;

class PreviewDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreviewDialog(const QString &filePath, QWidget *parent = nullptr);
    ~PreviewDialog() override;

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    // 预览可选的声音：默认是录制时开启的音轨（两条都在时为混合），
    // 也可以只听系统声音或只听麦克风。
    enum AudioChoice { ChoiceMixed = 0, ChoiceSystem = 1, ChoiceMic = 2 };

    void setupTrackSelector();
    void applyAudioChoice(int choice);
    void startAudioPreparation(int choice, const QString &output);
    void handleAudioPrepared(bool success, const QString &message);
    // 准备不可行时的统一处置：内容等价才回退原文件，否则停下报错。
    void handlePreparationUnavailable(int choice, const QString &message);
    void playSource(const QString &path);
    QString previewCachePath(int choice) const;
    static void prunePreviewCache();

    void updatePosition(qint64 position);
    void updateDuration(qint64 duration);
    void togglePlay();
    void toggleFullScreen();

    QString m_filePath;
    MediaInfo m_media;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QVideoWidget *m_videoWidget = nullptr;
    QSlider *m_positionSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_fullscreenButton = nullptr;
    QComboBox *m_trackCombo = nullptr;
    FFmpegTool *m_ffmpeg = nullptr;
    int m_pendingChoice = -1;
    QString m_pendingOutput;
    qint64 m_resumePosition = 0;
    bool m_playing = false;
};

} // namespace lightrec
