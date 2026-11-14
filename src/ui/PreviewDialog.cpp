#include "ui/PreviewDialog.h"

#include "core/Types.h"
#include "media/FFmpegTool.h"
#include "ui/Widgets.h"

#include <QAudioOutput>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
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

namespace {

// QMediaPlayer 只会播放默认音轨，因此“混合 / 仅系统 / 仅麦克风”都先用 ffmpeg
// 生成一个临时预览文件（视频流直接复制，只处理音频），按源文件与选项缓存。
QString previewCacheDir()
{
    const QString dir = QDir::temp().filePath(QStringLiteral("lightrec_preview"));
    QDir().mkpath(dir);
    return dir;
}

} // namespace

PreviewDialog::PreviewDialog(const QString &filePath, QWidget *parent)
    : QDialog(parent)
    , m_filePath(filePath)
{
    setWindowTitle(QStringLiteral("预览 - %1").arg(QFileInfo(filePath).fileName()));
    resize(960, 620);
    prunePreviewCache();

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
    auto *trackLabel = new QLabel(QStringLiteral("音轨"), this);
    trackLabel->setObjectName(QStringLiteral("mutedLabel"));
    m_trackCombo = new QComboBox(this);
    m_trackCombo->setToolTip(QStringLiteral("选择预览时播放的声音"));
    auto *openFolder = ui::secondaryButton(QStringLiteral("打开目录"), this);
    m_fullscreenButton = ui::secondaryButton(QStringLiteral("全屏"), this);
    m_fullscreenButton->setToolTip(QStringLiteral("全屏 / 退出全屏（F11，或双击视频）"));
    controls->addWidget(playButton);
    controls->addWidget(m_positionSlider, 1);
    controls->addWidget(m_timeLabel);
    controls->addWidget(trackLabel);
    controls->addWidget(m_trackCombo);
    controls->addWidget(openFolder);
    controls->addWidget(m_fullscreenButton);
    layout->addLayout(controls);
    m_videoWidget->installEventFilter(this);

    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoOutput(m_videoWidget);
    m_ffmpeg = new FFmpegTool(this);

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
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia && m_resumePosition > 0) {
            const qint64 target = m_resumePosition;
            m_resumePosition = 0;
            m_player->setPosition(target);
        }
    });
    connect(m_positionSlider, &QSlider::sliderMoved, this, [this](int value) {
        m_player->setPosition(value);
    });
    connect(openFolder, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_filePath).absolutePath()));
    });
    connect(m_fullscreenButton, &QPushButton::clicked, this, &PreviewDialog::toggleFullScreen);
    connect(m_ffmpeg, &FFmpegTool::finished, this, &PreviewDialog::handleAudioPrepared);
    connect(m_trackCombo, &QComboBox::activated, this, [this](int) {
        applyAudioChoice(m_trackCombo->currentData().toInt());
    });

    m_media = MediaProbe::probe(filePath);
    setupTrackSelector();
    applyAudioChoice(m_media.audioTracks >= 2 ? ChoiceMixed : ChoiceSystem);
}

PreviewDialog::~PreviewDialog()
{
    m_pendingChoice = -1;
    if (m_ffmpeg)
        m_ffmpeg->cancel();
    if (m_player)
        m_player->stop();
}

void PreviewDialog::setupTrackSelector()
{
    if (!m_trackCombo)
        return;
    m_trackCombo->clear();

    if (m_media.audioTracks <= 0) {
        m_trackCombo->addItem(QStringLiteral("无音轨"));
        m_trackCombo->setEnabled(false);
        return;
    }
    if (m_media.audioTracks == 1) {
        // 录制时只开了一条声音，原文件里就是它，直接播放即可。
        m_trackCombo->addItem(m_media.micAudioIndex == 0 ? QStringLiteral("麦克风")
                                                        : QStringLiteral("系统声音"));
        m_trackCombo->setEnabled(false);
        return;
    }

    m_trackCombo->addItem(QStringLiteral("系统 + 麦克风（录制原声）"), ChoiceMixed);
    // 标出实际读取的源音轨序号，选轨不符时可以直接对照排查。
    if (m_media.systemAudioIndex >= 0)
        m_trackCombo->addItem(QStringLiteral("仅系统声音（第%1条音轨）")
                                  .arg(m_media.systemAudioIndex + 1),
                              ChoiceSystem);
    if (m_media.micAudioIndex >= 0)
        m_trackCombo->addItem(QStringLiteral("仅麦克风（第%1条音轨）")
                                  .arg(m_media.micAudioIndex + 1),
                              ChoiceMic);
    m_trackCombo->setEnabled(m_trackCombo->count() > 1);
}

void PreviewDialog::applyAudioChoice(int choice)
{
    if (!m_player)
        return;
    m_resumePosition = m_player->position();

    // 换了选择就丢弃正在进行的准备任务（取消时可能同步发出 finished，先清空 pending）。
    if (m_pendingChoice >= 0) {
        m_pendingChoice = -1;
        m_ffmpeg->cancel();
    }

    if (m_media.audioTracks < 2) {
        playSource(m_filePath);
        return;
    }

    const QString cached = previewCachePath(choice);
    if (QFileInfo::exists(cached) && QFileInfo(cached).size() > 0) {
        // 准备被取消/失败会留下没有 moov 的半截文件（size>0 但播不了），
        // 先验证可读再用；废文件删掉重新生成。
        if (MediaProbe::probe(cached).valid) {
            playSource(cached);
            return;
        }
        QFile::remove(cached);
    }
    startAudioPreparation(choice, cached);
}

void PreviewDialog::startAudioPreparation(int choice, const QString &output)
{
    const bool system = choice != ChoiceMic && m_media.systemAudioIndex >= 0;
    const bool mic = choice != ChoiceSystem && m_media.micAudioIndex >= 0;
    const QStringList arguments =
        FFmpegTool::audioExportArguments(m_filePath, output, system, mic,
                                         m_media.systemAudioIndex, m_media.micAudioIndex,
                                         /*faststart=*/false);
    if (!system && !mic) {
        handlePreparationUnavailable(choice, QStringLiteral("源文件中没有所选音轨"));
        return;
    }
    if (!m_ffmpeg->available()) {
        handlePreparationUnavailable(choice, QStringLiteral("未找到 ffmpeg，无法生成所选音轨的预览"));
        return;
    }

    m_player->pause();
    m_statusLabel->setText(QStringLiteral("正在准备音轨（首次需要几秒，之后会缓存）…"));

    // 先登记本次任务再启动：run() 内部会先 cancel()，且启动失败会同步发出
    // finished；若等 run() 返回再登记，这次失败会被当成上一个任务忽略，
    // 界面会一直卡在“正在准备”。
    m_pendingChoice = choice;
    m_pendingOutput = output;
    m_ffmpeg->run(arguments, QStringLiteral("预览音轨"), m_media.durationMs);
}

void PreviewDialog::handleAudioPrepared(bool success, const QString &message)
{
    if (m_pendingChoice < 0)
        return;
    const QString output = m_pendingOutput;
    const int choice = m_pendingChoice;
    m_pendingChoice = -1;
    m_pendingOutput.clear();

    if (success && QFileInfo::exists(output) && QFileInfo(output).size() > 0) {
        playSource(output);
        return;
    }

    // 失败时清掉半截输出，否则 size>0 会让下次把它当有效缓存播放。
    if (QFileInfo::exists(output))
        QFile::remove(output);
    handlePreparationUnavailable(choice, QStringLiteral("音轨准备失败：%1").arg(message));
}

void PreviewDialog::handlePreparationUnavailable(int choice, const QString &message)
{
    // 原文件的默认音轨就是系统声音，只有“仅系统声音”回退后内容才等价；
    // 混合/仅麦克风回退会播出错误的声源（选麦克风却听到系统声），宁可停下报错。
    if (choice == ChoiceSystem && m_media.systemAudioIndex == 0) {
        playSource(m_filePath);
        m_statusLabel->setText(message + QStringLiteral("，已改用原文件的系统声音播放"));
        return;
    }
    m_player->stop();
    m_player->setSource(QUrl());
    m_statusLabel->setText(message + QStringLiteral("。重新选择一次音轨即可重试"));
}

void PreviewDialog::playSource(const QString &path)
{
    if (!m_player)
        return;
    m_statusLabel->clear();
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->play();
}

QString PreviewDialog::previewCachePath(int choice) const
{
    const QFileInfo info(m_filePath);
    const QByteArray key = (m_filePath + QLatin1Char('|') + QString::number(info.size())
                            + QLatin1Char('|') + info.lastModified().toString(Qt::ISODate)
                            + QLatin1Char('|') + QString::number(choice))
                               .toUtf8();
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(12));
    const QString name = QStringLiteral("%1_%2_%3.mp4")
                             .arg(info.completeBaseName(), QString::number(choice), hash);
    return QDir(previewCacheDir()).filePath(name);
}

void PreviewDialog::prunePreviewCache()
{
    const QDir dir(previewCacheDir());
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-1);
    const QFileInfoList entries = dir.entryInfoList({ QStringLiteral("*.mp4") }, QDir::Files);
    for (const QFileInfo &file : entries) {
        if (file.lastModified() < cutoff)
            QFile::remove(file.absoluteFilePath());
    }
}

void PreviewDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    // 准备期间加载的还是上一次选择的音轨，恢复窗口时不能把它放出去。
    if (m_player && !m_player->source().isEmpty()
        && m_player->playbackState() != QMediaPlayer::PlayingState
        && m_pendingChoice < 0)
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
    if (!m_player || m_player->source().isEmpty())
        return;
    // 正在准备所选音轨时，播放器里还是上一次选择的声音，忽略播放请求。
    if (m_pendingChoice >= 0)
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
