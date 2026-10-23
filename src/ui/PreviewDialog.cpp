#include "ui/PreviewDialog.h"
#include "core/Types.h"
#include "ui/Widgets.h"

#include <QAudioOutput>
#include <QDesktopServices>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

namespace lightrec {

PreviewDialog::PreviewDialog(const QString &filePath, QWidget *parent)
    : QDialog(parent)
    , m_filePath(filePath)
{
    setWindowTitle(QStringLiteral("预览 - %1").arg(QFileInfo(filePath).fileName()));
    resize(960, 620);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setMinimumHeight(420);
    m_videoWidget->setStyleSheet(QStringLiteral("background:#000; border-radius:8px;"));
    layout->addWidget(m_videoWidget, 1);

    m_statusLabel = ui::mutedLabel(QString(), this);
    layout->addWidget(m_statusLabel);

    auto *controls = new QHBoxLayout();
    auto *playButton = ui::primaryButton(QStringLiteral("播放"), this);
    m_positionSlider = new QSlider(Qt::Horizontal, this);
    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), this);
    m_timeLabel->setObjectName(QStringLiteral("mutedLabel"));
    auto *openFolder = ui::secondaryButton(QStringLiteral("打开目录"), this);
    m_fullscreenButton = ui::secondaryButton(QStringLiteral("全屏"), this);
    m_fullscreenButton->setToolTip(QStringLiteral("全屏 / 退出全屏（F11，或双击视频）"));
    controls->addWidget(playButton);
    controls->addWidget(m_positionSlider, 1);
    controls->addWidget(m_timeLabel);
    controls->addWidget(openFolder);
    controls->addWidget(m_fullscreenButton);
    layout->addLayout(controls);
    m_videoWidget->installEventFilter(this);

    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoOutput(m_videoWidget);
    m_player->setSource(QUrl::fromLocalFile(filePath));

    connect(playButton, &QPushButton::clicked, this, [this, playButton] {
        togglePlay();
        playButton->setText(m_playing ? QStringLiteral("暂停") : QStringLiteral("播放"));
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, &PreviewDialog::updatePosition);
    connect(m_player, &QMediaPlayer::durationChanged, this, &PreviewDialog::updateDuration);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this, playButton](QMediaPlayer::PlaybackState state) {
        m_playing = state == QMediaPlayer::PlayingState;
        playButton->setText(m_playing ? QStringLiteral("暂停") : QStringLiteral("播放"));
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
        m_statusLabel->setText(QStringLiteral("无法播放：%1").arg(message));
    });
    connect(m_positionSlider, &QSlider::sliderMoved, this, [this](int value) {
        m_player->setPosition(value);
    });
    connect(openFolder, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_filePath).absolutePath()));
    });
    connect(m_fullscreenButton, &QPushButton::clicked, this, &PreviewDialog::toggleFullScreen);

    m_player->play();
}

PreviewDialog::~PreviewDialog()
{
    if (m_player)
        m_player->stop();
}

void PreviewDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (m_player && m_player->playbackState() != QMediaPlayer::PlayingState)
        m_player->play();
}

bool PreviewDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_videoWidget && event->type() == QEvent::MouseButtonDblClick) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            toggleFullScreen();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void PreviewDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F11) {
        toggleFullScreen();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        toggleFullScreen();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void PreviewDialog::toggleFullScreen()
{
    if (isFullScreen()) {
        showNormal();
        if (m_fullscreenButton)
            m_fullscreenButton->setText(QStringLiteral("全屏"));
    } else {
        showFullScreen();
        if (m_fullscreenButton)
            m_fullscreenButton->setText(QStringLiteral("退出全屏"));
    }
}

void PreviewDialog::togglePlay()
{
    if (!m_player)
        return;
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
    else
        m_player->play();
}

void PreviewDialog::updatePosition(qint64 position)
{
    if (!m_positionSlider->isSliderDown())
        m_positionSlider->setValue(static_cast<int>(position));
    m_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatDuration(position),
                                                        formatDuration(m_player->duration())));
}

void PreviewDialog::updateDuration(qint64 duration)
{
    m_positionSlider->setRange(0, static_cast<int>(duration));
    m_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatDuration(m_player->position()),
                                                        formatDuration(duration)));
}

} // namespace lightrec
