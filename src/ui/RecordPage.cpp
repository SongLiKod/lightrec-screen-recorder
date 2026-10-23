#include "ui/RecordPage.h"

#include "audio/WasapiCapture.h"
#include "capture/ScreenCapture.h"
#include "core/AppPaths.h"
#include "core/ConfigStore.h"
#include "overlay/CameraManager.h"
#include "ui/Widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QVBoxLayout>

namespace lightrec {

RecordPage::RecordPage(QWidget *parent)
    : QWidget(parent)
{
    m_video = ConfigStore::instance().videoDefaults();
    m_audio = ConfigStore::instance().audioDefaults();
    m_overlay = ConfigStore::instance().overlayDefaults();
    m_stream = ConfigStore::instance().streamDefaults();
    buildUi();
    refreshMonitors();
    refreshDevices();
    refreshCameras();
    RecordOptions initial;
    initial.video = m_video;
    initial.audio = m_audio;
    initial.overlay = m_overlay;
    initial.stream = m_stream;
    initial.minimizeOnStart = ConfigStore::instance().minimizeOnStart();
    setOptions(initial);
    setState(RecordState::Idle);
    updateFfmpegStatus();
}

void RecordPage::updateFfmpegStatus()
{
    const bool installed = QFileInfo::exists(AppPaths::ffmpegExe());
    m_ffmpegButton->setVisible(!installed);
    if (!installed)
        m_encoderLabel->setText(QStringLiteral("未找到 FFmpeg 运行时"));
}

void RecordPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);

    auto *content = new QWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 20, 24, 24);
    layout->setSpacing(16);

    auto *title = new QLabel(QStringLiteral("录制屏幕"), content);
    title->setObjectName(QStringLiteral("appTitle"));
    layout->addWidget(title);
    layout->addWidget(ui::mutedLabel(
        QStringLiteral("全屏、窗口或自定义选区录制；硬件编码自动优先，Overlay 实时写入视频。"), content));

    // ------------------------------------------------------------------
    // Capture mode
    // ------------------------------------------------------------------
    QFrame *modeCard = ui::card(content);
    auto *modeLayout = new QVBoxLayout(modeCard);
    modeLayout->setContentsMargins(18, 16, 18, 16);
    modeLayout->setSpacing(12);
    modeLayout->addWidget(ui::sectionTitle(QStringLiteral("录制模式"), modeCard));

    auto *modeRow = new QHBoxLayout();
    modeRow->setSpacing(10);
    m_fullScreenButton = new QPushButton(QStringLiteral("全屏录制\n录制整个显示器"), modeCard);
    m_windowButton = new QPushButton(QStringLiteral("窗口录制\n跟随窗口移动"), modeCard);
    m_regionButton = new QPushButton(QStringLiteral("自定义选区\n框选任意区域"), modeCard);
    auto *modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    for (QPushButton *button : { m_fullScreenButton, m_windowButton, m_regionButton }) {
        button->setObjectName(QStringLiteral("modeButton"));
        button->setCheckable(true);
        button->setMinimumHeight(62);
        button->setCursor(Qt::PointingHandCursor);
        modeGroup->addButton(button);
        modeRow->addWidget(button, 1);
    }
    m_fullScreenButton->setChecked(true);
    modeLayout->addLayout(modeRow);

    m_monitorRow = new QWidget(modeCard);
    auto *monitorLayout = new QHBoxLayout(m_monitorRow);
    monitorLayout->setContentsMargins(0, 0, 0, 0);
    m_monitorCombo = new QComboBox(m_monitorRow);
    auto *refreshMonitor = ui::secondaryButton(QStringLiteral("刷新"), m_monitorRow);
    monitorLayout->addWidget(new QLabel(QStringLiteral("显示器"), m_monitorRow));
    monitorLayout->addWidget(m_monitorCombo, 1);
    monitorLayout->addWidget(refreshMonitor);
    modeLayout->addWidget(m_monitorRow);

    m_windowRow = new QWidget(modeCard);
    auto *windowLayout = new QHBoxLayout(m_windowRow);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    m_windowLabel = new QLabel(QStringLiteral("未选择窗口"), m_windowRow);
    m_windowLabel->setObjectName(QStringLiteral("mutedLabel"));
    auto *chooseWindow = ui::secondaryButton(QStringLiteral("选择窗口"), m_windowRow);
    windowLayout->addWidget(m_windowLabel, 1);
    windowLayout->addWidget(chooseWindow);
    modeLayout->addWidget(m_windowRow);

    m_regionRow = new QWidget(modeCard);
    auto *regionLayout = new QHBoxLayout(m_regionRow);
    regionLayout->setContentsMargins(0, 0, 0, 0);
    m_regionLabel = new QLabel(QStringLiteral("未选择区域"), m_regionRow);
    m_regionLabel->setObjectName(QStringLiteral("mutedLabel"));
    auto *chooseRegion = ui::secondaryButton(QStringLiteral("框选区域"), m_regionRow);
    regionLayout->addWidget(m_regionLabel, 1);
    regionLayout->addWidget(chooseRegion);
    modeLayout->addWidget(m_regionRow);

    modeLayout->addWidget(ui::mutedLabel(
        QStringLiteral("提示：录制中可通过悬浮工具栏的“调整选区”按钮拖动选区位置和大小。"), modeCard));
    layout->addWidget(modeCard);

    connect(m_fullScreenButton, &QPushButton::clicked, this, [this] {
        m_video.mode = CaptureMode::FullScreen;
        updateModeVisibility();
        emit optionsChanged();
    });
    connect(m_windowButton, &QPushButton::clicked, this, [this] {
        m_video.mode = CaptureMode::Window;
        updateModeVisibility();
        emit optionsChanged();
    });
    connect(m_regionButton, &QPushButton::clicked, this, [this] {
        m_video.mode = CaptureMode::Region;
        updateModeVisibility();
        emit optionsChanged();
    });
    connect(chooseWindow, &QPushButton::clicked, this, &RecordPage::selectWindowRequested);
    connect(chooseRegion, &QPushButton::clicked, this, &RecordPage::selectRegionRequested);
    connect(refreshMonitor, &QPushButton::clicked, this, &RecordPage::refreshMonitors);
    connect(m_monitorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_video.monitorIndex = m_monitorCombo->itemData(index).toInt();
        emit optionsChanged();
    });

    // ------------------------------------------------------------------
    // Video + audio
    // ------------------------------------------------------------------
    auto *middleRow = new QHBoxLayout();
    middleRow->setSpacing(16);

    QFrame *videoCard = ui::card(content);
    auto *videoLayout = new QVBoxLayout(videoCard);
    videoLayout->setContentsMargins(18, 16, 18, 16);
    videoLayout->setSpacing(10);
    videoLayout->addWidget(ui::sectionTitle(QStringLiteral("视频参数"), videoCard));

    m_codecCombo = new QComboBox(videoCard);
    m_codecCombo->addItem(QStringLiteral("H.264 (兼容性最佳)"), QStringLiteral("h264"));
    m_codecCombo->addItem(QStringLiteral("H.265 (体积更小)"), QStringLiteral("h265"));
    videoLayout->addWidget(ui::labeledRow(QStringLiteral("编码格式"), m_codecCombo));

    m_fpsCombo = new QComboBox(videoCard);
    for (int fps : { 15, 24, 30, 60 })
        m_fpsCombo->addItem(QStringLiteral("%1 fps").arg(fps), fps);
    m_fpsCombo->setCurrentIndex(2);
    videoLayout->addWidget(ui::labeledRow(QStringLiteral("帧率"), m_fpsCombo));

    m_bitrateSlider = new QSlider(Qt::Horizontal, videoCard);
    m_bitrateSlider->setRange(2000, 20000);
    m_bitrateSlider->setSingleStep(500);
    m_bitrateSlider->setPageStep(2000);
    m_bitrateSlider->setValue(8000);
    m_bitrateLabel = ui::valueLabel(QStringLiteral("8 Mbps"), videoCard);
    auto *bitrateRow = new QWidget(videoCard);
    auto *bitrateLayout = new QHBoxLayout(bitrateRow);
    bitrateLayout->setContentsMargins(0, 0, 0, 0);
    bitrateLayout->addWidget(m_bitrateSlider, 1);
    bitrateLayout->addWidget(m_bitrateLabel);
    videoLayout->addWidget(ui::labeledRow(QStringLiteral("视频码率"), bitrateRow));

    m_cursorCheck = new QCheckBox(QStringLiteral("录制鼠标指针"), videoCard);
    m_cursorCheck->setChecked(true);
    videoLayout->addWidget(m_cursorCheck);
    videoLayout->addStretch(1);

    connect(m_codecCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_video.codec = m_codecCombo->itemData(index).toString();
        emit optionsChanged();
    });
    connect(m_fpsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_video.fps = m_fpsCombo->itemData(index).toInt();
        emit optionsChanged();
    });
    connect(m_bitrateSlider, &QSlider::valueChanged, this, [this](int value) {
        m_video.bitrateKbps = value;
        updateBitrateLabel();
        emit optionsChanged();
    });
    connect(m_cursorCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_video.captureCursor = checked;
        emit optionsChanged();
    });

    QFrame *audioCard = ui::card(content);
    auto *audioLayout = new QVBoxLayout(audioCard);
    audioLayout->setContentsMargins(18, 16, 18, 16);
    audioLayout->setSpacing(10);
    audioLayout->addWidget(ui::sectionTitle(QStringLiteral("音频"), audioCard));

    m_systemAudioCheck = new QCheckBox(QStringLiteral("系统声音（独立音轨）"), audioCard);
    m_systemAudioCheck->setChecked(true);
    m_systemDeviceCombo = new QComboBox(audioCard);
    auto *refreshSystem = ui::secondaryButton(QStringLiteral("刷新"), audioCard);
    auto *systemRow = new QWidget(audioCard);
    auto *systemLayout = new QHBoxLayout(systemRow);
    systemLayout->setContentsMargins(0, 0, 0, 0);
    systemLayout->addWidget(m_systemDeviceCombo, 1);
    systemLayout->addWidget(refreshSystem);
    audioLayout->addWidget(m_systemAudioCheck);
    audioLayout->addWidget(systemRow);

    m_micCheck = new QCheckBox(QStringLiteral("麦克风（独立音轨）"), audioCard);
    m_micCheck->setChecked(true);
    m_micDeviceCombo = new QComboBox(audioCard);
    auto *refreshMic = ui::secondaryButton(QStringLiteral("刷新"), audioCard);
    auto *micRow = new QWidget(audioCard);
    auto *micLayout = new QHBoxLayout(micRow);
    micLayout->setContentsMargins(0, 0, 0, 0);
    micLayout->addWidget(m_micDeviceCombo, 1);
    micLayout->addWidget(refreshMic);
    audioLayout->addWidget(m_micCheck);
    audioLayout->addWidget(micRow);

    m_denoiseCheck = new QCheckBox(QStringLiteral("麦克风降噪"), audioCard);
    m_denoiseCheck->setChecked(true);
    audioLayout->addWidget(m_denoiseCheck);

    m_micGainSlider = new QSlider(Qt::Horizontal, audioCard);
    m_micGainSlider->setRange(-20, 20);
    m_micGainSlider->setValue(0);
    m_micGainLabel = ui::valueLabel(QStringLiteral("+0 dB"), audioCard);
    auto *micGainRow = new QWidget(audioCard);
    auto *micGainLayout = new QHBoxLayout(micGainRow);
    micGainLayout->setContentsMargins(0, 0, 0, 0);
    micGainLayout->addWidget(m_micGainSlider, 1);
    micGainLayout->addWidget(m_micGainLabel);
    audioLayout->addWidget(ui::labeledRow(QStringLiteral("麦克风增益"), micGainRow));

    m_systemGainSlider = new QSlider(Qt::Horizontal, audioCard);
    m_systemGainSlider->setRange(-20, 20);
    m_systemGainSlider->setValue(0);
    m_systemGainLabel = ui::valueLabel(QStringLiteral("+0 dB"), audioCard);
    auto *systemGainRow = new QWidget(audioCard);
    auto *systemGainLayout = new QHBoxLayout(systemGainRow);
    systemGainLayout->setContentsMargins(0, 0, 0, 0);
    systemGainLayout->addWidget(m_systemGainSlider, 1);
    systemGainLayout->addWidget(m_systemGainLabel);
    audioLayout->addWidget(ui::labeledRow(QStringLiteral("系统音量"), systemGainRow));
    audioLayout->addStretch(1);

    connect(m_systemAudioCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_audio.systemEnabled = checked;
        m_systemDeviceCombo->setEnabled(checked);
        emit optionsChanged();
    });
    connect(m_micCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_audio.micEnabled = checked;
        m_micDeviceCombo->setEnabled(checked);
        m_denoiseCheck->setEnabled(checked);
        m_micGainSlider->setEnabled(checked);
        emit optionsChanged();
    });
    connect(m_denoiseCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_audio.noiseSuppression = checked;
        emit optionsChanged();
    });
    connect(m_micGainSlider, &QSlider::valueChanged, this, [this](int value) {
        m_audio.micGainDb = value;
        updateGainLabels();
        emit optionsChanged();
    });
    connect(m_systemGainSlider, &QSlider::valueChanged, this, [this](int value) {
        m_audio.systemGainDb = value;
        updateGainLabels();
        emit optionsChanged();
    });
    connect(m_systemDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_audio.systemDeviceId = m_systemDeviceCombo->itemData(index).toString();
        emit optionsChanged();
    });
    connect(m_micDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_audio.micDeviceId = m_micDeviceCombo->itemData(index).toString();
        emit optionsChanged();
    });
    connect(refreshSystem, &QPushButton::clicked, this, &RecordPage::refreshDevices);
    connect(refreshMic, &QPushButton::clicked, this, &RecordPage::refreshDevices);

    middleRow->addWidget(videoCard, 1);
    middleRow->addWidget(audioCard, 1);
    layout->addLayout(middleRow);

    // ------------------------------------------------------------------
    // Overlay + stream
    // ------------------------------------------------------------------
    auto *bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(16);

    QFrame *overlayCard = ui::card(content);
    auto *overlayLayout = new QVBoxLayout(overlayCard);
    overlayLayout->setContentsMargins(18, 16, 18, 16);
    overlayLayout->setSpacing(10);
    overlayLayout->addWidget(ui::sectionTitle(QStringLiteral("叠加与摄像头"), overlayCard));

    m_cameraCheck = new QCheckBox(QStringLiteral("摄像头画中画"), overlayCard);
    overlayLayout->addWidget(m_cameraCheck);

    m_cameraCombo = new QComboBox(overlayCard);
    overlayLayout->addWidget(ui::labeledRow(QStringLiteral("摄像头"), m_cameraCombo));

    m_layoutCombo = new QComboBox(overlayCard);
    m_layoutCombo->addItem(QStringLiteral("单屏 + 画中画"), static_cast<int>(LayoutTemplate::Single));
    m_layoutCombo->addItem(QStringLiteral("二分屏（屏幕 + 摄像头）"), static_cast<int>(LayoutTemplate::Split));
    m_layoutCombo->addItem(QStringLiteral("三分屏（屏幕 + 摄像头 + PPT）"), static_cast<int>(LayoutTemplate::Triple));
    overlayLayout->addWidget(ui::labeledRow(QStringLiteral("布局模板"), m_layoutCombo));

    m_mirrorCheck = new QCheckBox(QStringLiteral("摄像头镜像"), overlayCard);
    m_mirrorCheck->setChecked(true);
    overlayLayout->addWidget(m_mirrorCheck);

    m_cameraOpacitySlider = new QSlider(Qt::Horizontal, overlayCard);
    m_cameraOpacitySlider->setRange(20, 100);
    m_cameraOpacitySlider->setValue(100);
    m_cameraOpacityLabel = ui::valueLabel(QStringLiteral("100%"), overlayCard);
    auto *opacityRow = new QWidget(overlayCard);
    auto *opacityLayout = new QHBoxLayout(opacityRow);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->addWidget(m_cameraOpacitySlider, 1);
    opacityLayout->addWidget(m_cameraOpacityLabel);
    overlayLayout->addWidget(ui::labeledRow(QStringLiteral("透明度"), opacityRow));

    auto *overlayToggles = new QHBoxLayout();
    m_mouseCheck = new QCheckBox(QStringLiteral("鼠标高亮"), overlayCard);
    m_mouseCheck->setChecked(true);
    m_keysCheck = new QCheckBox(QStringLiteral("按键显示"), overlayCard);
    m_keysCheck->setChecked(true);
    overlayToggles->addWidget(m_mouseCheck);
    overlayToggles->addWidget(m_keysCheck);
    overlayToggles->addStretch(1);
    overlayLayout->addLayout(overlayToggles);

    m_pptButton = ui::secondaryButton(QStringLiteral("选择 PPT 辅助图片"), overlayCard);
    overlayLayout->addWidget(m_pptButton);
    overlayLayout->addStretch(1);

    connect(m_cameraCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_overlay.showCamera = checked;
        m_cameraCombo->setEnabled(checked);
        emit optionsChanged();
    });
    connect(m_cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_overlay.cameraDeviceId = m_cameraCombo->itemData(index).toString();
        emit optionsChanged();
    });
    connect(m_layoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_overlay.layout = static_cast<LayoutTemplate>(m_layoutCombo->itemData(index).toInt());
        updateLayoutControls();
        emit optionsChanged();
    });
    connect(m_mirrorCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_overlay.cameraMirror = checked;
        emit optionsChanged();
    });
    connect(m_cameraOpacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_overlay.cameraOpacity = value;
        m_cameraOpacityLabel->setText(QStringLiteral("%1%").arg(value));
        emit optionsChanged();
    });
    connect(m_mouseCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_overlay.showMouseHighlight = checked;
        emit optionsChanged();
    });
    connect(m_keysCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_overlay.showKeys = checked;
        emit optionsChanged();
    });
    connect(m_pptButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 PPT 辅助图片"),
                                                          QString(), QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp)"));
        if (!path.isEmpty()) {
            m_overlay.pptImagePath = path;
            m_pptButton->setText(QFileInfo(path).fileName());
            emit optionsChanged();
        }
    });

    QFrame *streamCard = ui::card(content);
    auto *streamLayout = new QVBoxLayout(streamCard);
    streamLayout->setContentsMargins(18, 16, 18, 16);
    streamLayout->setSpacing(10);
    streamLayout->addWidget(ui::sectionTitle(QStringLiteral("同时推流"), streamCard));

    m_streamCheck = new QCheckBox(QStringLiteral("录制时同步 RTMP 推流"), streamCard);
    streamLayout->addWidget(m_streamCheck);

    m_streamUrlEdit = new QLineEdit(streamCard);
    m_streamUrlEdit->setPlaceholderText(QStringLiteral("rtmp://live.example.com/live"));
    streamLayout->addWidget(ui::labeledRow(QStringLiteral("推流地址"), m_streamUrlEdit));

    m_streamKeyEdit = new QLineEdit(streamCard);
    m_streamKeyEdit->setPlaceholderText(QStringLiteral("推流密钥"));
    m_streamKeyEdit->setEchoMode(QLineEdit::Password);
    streamLayout->addWidget(ui::labeledRow(QStringLiteral("推流密钥"), m_streamKeyEdit));

    m_streamSaveCheck = new QCheckBox(QStringLiteral("同时保存本地录像"), streamCard);
    m_streamSaveCheck->setChecked(true);
    streamLayout->addWidget(m_streamSaveCheck);
    streamLayout->addWidget(ui::mutedLabel(
        QStringLiteral("直播画面会同步叠加画板、鼠标特效与摄像头。H.265 推流会自动切换为 H.264。"),
        streamCard));
    streamLayout->addStretch(1);

    connect(m_streamCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_stream.enabled = checked;
        m_streamUrlEdit->setEnabled(checked);
        m_streamKeyEdit->setEnabled(checked);
        m_streamSaveCheck->setEnabled(checked);
        emit optionsChanged();
    });
    connect(m_streamUrlEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_stream.serverUrl = text.trimmed();
        emit optionsChanged();
    });
    connect(m_streamKeyEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_stream.streamKey = text.trimmed();
        emit optionsChanged();
    });
    connect(m_streamSaveCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_stream.saveLocal = checked;
        emit optionsChanged();
    });

    bottomRow->addWidget(overlayCard, 1);
    bottomRow->addWidget(streamCard, 1);
    layout->addLayout(bottomRow);

    // ------------------------------------------------------------------
    // Output + controls
    // ------------------------------------------------------------------
    QFrame *actionCard = ui::card(content);
    auto *actionLayout = new QVBoxLayout(actionCard);
    actionLayout->setContentsMargins(18, 16, 18, 16);
    actionLayout->setSpacing(12);

    auto *outputRow = new QWidget(actionCard);
    auto *outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    auto *outputLabel = new QLabel(QStringLiteral("保存目录"), outputRow);
    outputLabel->setObjectName(QStringLiteral("mutedLabel"));
    m_outputDirEdit = new QLineEdit(outputRow);
    m_outputDirEdit->setReadOnly(true);
    auto *browseButton = ui::secondaryButton(QStringLiteral("更改"), outputRow);
    outputLayout->addWidget(outputLabel);
    outputLayout->addWidget(m_outputDirEdit, 1);
    outputLayout->addWidget(browseButton);
    actionLayout->addWidget(outputRow);

    auto *controlRow = new QHBoxLayout();
    controlRow->setSpacing(12);
    m_recordButton = new QPushButton(QStringLiteral("●  开始录制"), actionCard);
    m_recordButton->setObjectName(QStringLiteral("recordButton"));
    m_recordButton->setCursor(Qt::PointingHandCursor);
    m_pauseButton = ui::secondaryButton(QStringLiteral("暂停"), actionCard);
    m_stopButton = ui::dangerButton(QStringLiteral("停止"), actionCard);
    m_elapsedLabel = new QLabel(QStringLiteral("00:00:00"), actionCard);
    m_elapsedLabel->setStyleSheet(QStringLiteral("font-family:'Cascadia Mono','Consolas',monospace;"
                                                 "font-size:20px; font-weight:700; color:#2d8cf0;"));
    controlRow->addWidget(m_recordButton);
    controlRow->addWidget(m_pauseButton);
    controlRow->addWidget(m_stopButton);
    controlRow->addSpacing(12);
    controlRow->addWidget(m_elapsedLabel);
    controlRow->addStretch(1);
    actionLayout->addLayout(controlRow);

    auto *statusRow = new QHBoxLayout();
    m_statusLabel = new QLabel(QStringLiteral("就绪"), actionCard);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    m_encoderLabel = ui::mutedLabel(QString(), actionCard);
    m_ffmpegButton = ui::secondaryButton(QStringLiteral("安装 FFmpeg"), actionCard);
    m_ffmpegButton->setVisible(false);
    statusRow->addWidget(m_statusLabel, 1);
    statusRow->addWidget(m_ffmpegButton);
    statusRow->addWidget(m_encoderLabel);
    actionLayout->addLayout(statusRow);
    connect(m_ffmpegButton, &QPushButton::clicked, this, &RecordPage::installFfmpegRequested);

    layout->addWidget(actionCard);
    layout->addStretch(1);

    scroll->setWidget(content);

    connect(browseButton, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择保存目录"),
                                                              m_outputDirEdit->text());
        if (!dir.isEmpty()) {
            m_outputDirEdit->setText(dir);
            emit outputDirChanged(dir);
        }
    });
    connect(m_recordButton, &QPushButton::clicked, this, &RecordPage::startRequested);
    connect(m_pauseButton, &QPushButton::clicked, this, &RecordPage::pauseRequested);
    connect(m_stopButton, &QPushButton::clicked, this, &RecordPage::stopRequested);
}

void RecordPage::updateModeVisibility()
{
    m_monitorRow->setVisible(m_video.mode == CaptureMode::FullScreen);
    m_windowRow->setVisible(m_video.mode == CaptureMode::Window);
    m_regionRow->setVisible(m_video.mode == CaptureMode::Region);
    m_fullScreenButton->setChecked(m_video.mode == CaptureMode::FullScreen);
    m_windowButton->setChecked(m_video.mode == CaptureMode::Window);
    m_regionButton->setChecked(m_video.mode == CaptureMode::Region);
}

void RecordPage::updateBitrateLabel()
{
    m_bitrateLabel->setText(QStringLiteral("%1 Mbps").arg(m_bitrateSlider->value() / 1000.0, 0, 'f', 1));
}

void RecordPage::updateGainLabels()
{
    const int mic = m_micGainSlider->value();
    const int system = m_systemGainSlider->value();
    m_micGainLabel->setText(QStringLiteral("%1%2 dB").arg(mic >= 0 ? QStringLiteral("+") : QString())
                                .arg(mic));
    m_systemGainLabel->setText(QStringLiteral("%1%2 dB").arg(system >= 0 ? QStringLiteral("+") : QString())
                                   .arg(system));
}

void RecordPage::updateLayoutControls()
{
    const bool triple = m_overlay.layout == LayoutTemplate::Triple;
    m_pptButton->setVisible(triple);
}

void RecordPage::refreshMonitors()
{
    const int current = m_video.monitorIndex;
    const QSignalBlocker blocker(m_monitorCombo);
    m_monitorCombo->clear();
    for (const MonitorInfo &monitor : ScreenCapture::monitors()) {
        const QString label = QStringLiteral("显示器 %1  ·  %2 × %3%4")
                                  .arg(monitor.index + 1)
                                  .arg(monitor.geometry.width())
                                  .arg(monitor.geometry.height())
                                  .arg(monitor.primary ? QStringLiteral("  (主屏)") : QString());
        m_monitorCombo->addItem(label, monitor.index);
    }
    const int index = m_monitorCombo->findData(current);
    m_monitorCombo->setCurrentIndex(index >= 0 ? index : 0);
    if (m_monitorCombo->count() > 0)
        m_video.monitorIndex = m_monitorCombo->currentData().toInt();
}

void RecordPage::refreshDevices()
{
    const QString systemId = m_systemDeviceCombo->currentData().toString();
    const QString micId = m_micDeviceCombo->currentData().toString();

    {
        const QSignalBlocker blocker(m_systemDeviceCombo);
        m_systemDeviceCombo->clear();
        m_systemDeviceCombo->addItem(QStringLiteral("默认播放设备"), QString());
        for (const AudioDeviceInfo &device : WasapiCapture::renderDevices()) {
            m_systemDeviceCombo->addItem(device.isDefault ? device.name + QStringLiteral("  (默认)")
                                                          : device.name,
                                         device.id);
        }
        const int index = m_systemDeviceCombo->findData(systemId);
        m_systemDeviceCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    {
        const QSignalBlocker blocker(m_micDeviceCombo);
        m_micDeviceCombo->clear();
        m_micDeviceCombo->addItem(QStringLiteral("默认录音设备"), QString());
        for (const AudioDeviceInfo &device : WasapiCapture::captureDevices()) {
            m_micDeviceCombo->addItem(device.isDefault ? device.name + QStringLiteral("  (默认)")
                                                       : device.name,
                                      device.id);
        }
        const int index = m_micDeviceCombo->findData(micId);
        m_micDeviceCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
}

void RecordPage::refreshCameras()
{
    const QString current = m_cameraCombo->currentData().toString();
    const QSignalBlocker blocker(m_cameraCombo);
    m_cameraCombo->clear();
    m_cameraCombo->addItem(QStringLiteral("默认摄像头"), QString());
    for (const CameraDeviceInfo &device : CameraManager::devices())
        m_cameraCombo->addItem(device.name, device.id);
    const int index = m_cameraCombo->findData(current);
    m_cameraCombo->setCurrentIndex(index >= 0 ? index : 0);
}

CaptureMode RecordPage::currentMode() const
{
    return m_video.mode;
}

RecordOptions RecordPage::options() const
{
    RecordOptions options;
    options.video = m_video;
    options.audio = m_audio;
    options.overlay = m_overlay;
    options.stream = m_stream;
    options.outputPath = QString();
    options.minimizeOnStart = ConfigStore::instance().minimizeOnStart();
    return options;
}

void RecordPage::setOptions(const RecordOptions &options)
{
    m_video = options.video;
    m_audio = options.audio;
    m_overlay = options.overlay;
    m_stream = options.stream;

    {
        const QSignalBlocker blocker(m_monitorCombo);
        const int index = m_monitorCombo->findData(m_video.monitorIndex);
        if (index >= 0)
            m_monitorCombo->setCurrentIndex(index);
    }
    {
        const QSignalBlocker blocker(m_codecCombo);
        m_codecCombo->setCurrentIndex(m_video.codec.compare(QStringLiteral("h265"), Qt::CaseInsensitive) == 0 ? 1 : 0);
    }
    {
        const QSignalBlocker blocker(m_fpsCombo);
        const int index = m_fpsCombo->findData(m_video.fps);
        m_fpsCombo->setCurrentIndex(index >= 0 ? index : 2);
    }
    {
        const QSignalBlocker blocker(m_bitrateSlider);
        m_bitrateSlider->setValue(qBound(2000, m_video.bitrateKbps, 20000));
    }
    m_cursorCheck->setChecked(m_video.captureCursor);

    m_systemAudioCheck->setChecked(m_audio.systemEnabled);
    m_micCheck->setChecked(m_audio.micEnabled);
    m_denoiseCheck->setChecked(m_audio.noiseSuppression);
    m_micGainSlider->setValue(m_audio.micGainDb);
    m_systemGainSlider->setValue(m_audio.systemGainDb);
    {
        const QSignalBlocker blocker(m_systemDeviceCombo);
        const int index = m_systemDeviceCombo->findData(m_audio.systemDeviceId);
        if (index >= 0)
            m_systemDeviceCombo->setCurrentIndex(index);
    }
    {
        const QSignalBlocker blocker(m_micDeviceCombo);
        const int index = m_micDeviceCombo->findData(m_audio.micDeviceId);
        if (index >= 0)
            m_micDeviceCombo->setCurrentIndex(index);
    }

    m_cameraCheck->setChecked(m_overlay.showCamera);
    m_mirrorCheck->setChecked(m_overlay.cameraMirror);
    m_mouseCheck->setChecked(m_overlay.showMouseHighlight);
    m_keysCheck->setChecked(m_overlay.showKeys);
    {
        const QSignalBlocker blocker(m_layoutCombo);
        const int index = m_layoutCombo->findData(static_cast<int>(m_overlay.layout));
        if (index >= 0)
            m_layoutCombo->setCurrentIndex(index);
    }
    {
        const QSignalBlocker blocker(m_cameraOpacitySlider);
        m_cameraOpacitySlider->setValue(qBound(20, m_overlay.cameraOpacity, 100));
    }
    m_cameraOpacityLabel->setText(QStringLiteral("%1%").arg(m_overlay.cameraOpacity));
    if (!m_video.region.isValid()) {
        m_regionLabel->setText(QStringLiteral("未选择区域"));
    } else {
        m_regionLabel->setText(QStringLiteral("%1, %2  ·  %3 × %4")
                                   .arg(m_video.region.x()).arg(m_video.region.y())
                                   .arg(m_video.region.width()).arg(m_video.region.height()));
    }
    m_windowLabel->setText(m_video.windowTitle.isEmpty() ? QStringLiteral("未选择窗口")
                                                         : m_video.windowTitle);
    m_pptButton->setText(m_overlay.pptImagePath.isEmpty()
                             ? QStringLiteral("选择 PPT 辅助图片")
                             : QFileInfo(m_overlay.pptImagePath).fileName());
    {
        const QSignalBlocker blocker(m_cameraCombo);
        const int index = m_cameraCombo->findData(m_overlay.cameraDeviceId);
        if (index >= 0)
            m_cameraCombo->setCurrentIndex(index);
    }

    m_streamCheck->setChecked(m_stream.enabled);
    {
        const QSignalBlocker blocker(m_streamUrlEdit);
        m_streamUrlEdit->setText(m_stream.serverUrl);
    }
    {
        const QSignalBlocker blocker(m_streamKeyEdit);
        m_streamKeyEdit->setText(m_stream.streamKey);
    }
    m_streamSaveCheck->setChecked(m_stream.saveLocal);

    updateModeVisibility();
    updateBitrateLabel();
    updateGainLabels();
    updateLayoutControls();
    updateStateControls();
}

void RecordPage::updateStateControls()
{
    const bool idle = m_state == RecordState::Idle;
    const bool recording = m_state == RecordState::Recording || m_state == RecordState::Paused;
    m_recordButton->setEnabled(idle);
    m_pauseButton->setEnabled(recording);
    m_stopButton->setEnabled(recording || m_state == RecordState::Starting);
    m_recordButton->setProperty("recording", recording);
    m_recordButton->style()->unpolish(m_recordButton);
    m_recordButton->style()->polish(m_recordButton);
    m_recordButton->setText(recording ? QStringLiteral("●  录制中") : QStringLiteral("●  开始录制"));
    m_pauseButton->setText(m_state == RecordState::Paused ? QStringLiteral("继续")
                                                          : QStringLiteral("暂停"));
}

void RecordPage::reloadDefaults()
{
    ConfigStore &store = ConfigStore::instance();

    const VideoConfig video = store.videoDefaults();
    m_video.codec = video.codec;
    m_video.fps = video.fps;
    m_video.bitrateKbps = video.bitrateKbps;
    m_video.captureCursor = video.captureCursor;
    {
        const QSignalBlocker blocker(m_codecCombo);
        m_codecCombo->setCurrentIndex(
            m_video.codec.compare(QStringLiteral("h265"), Qt::CaseInsensitive) == 0 ? 1 : 0);
    }
    {
        const QSignalBlocker blocker(m_fpsCombo);
        const int index = m_fpsCombo->findData(m_video.fps);
        m_fpsCombo->setCurrentIndex(index >= 0 ? index : 2);
        m_video.fps = m_fpsCombo->currentData().toInt();
    }
    {
        const QSignalBlocker blocker(m_bitrateSlider);
        m_bitrateSlider->setValue(qBound(2000, m_video.bitrateKbps, 20000));
        m_video.bitrateKbps = m_bitrateSlider->value();
    }
    {
        const QSignalBlocker blocker(m_cursorCheck);
        m_cursorCheck->setChecked(m_video.captureCursor);
    }

    const AudioConfig audio = store.audioDefaults();
    m_audio.noiseSuppression = audio.noiseSuppression;
    m_audio.micGainDb = audio.micGainDb;
    m_audio.systemGainDb = audio.systemGainDb;
    {
        const QSignalBlocker blocker(m_denoiseCheck);
        m_denoiseCheck->setChecked(audio.noiseSuppression);
    }
    {
        const QSignalBlocker blocker(m_micGainSlider);
        m_micGainSlider->setValue(audio.micGainDb);
        m_audio.micGainDb = m_micGainSlider->value();
    }
    {
        const QSignalBlocker blocker(m_systemGainSlider);
        m_systemGainSlider->setValue(audio.systemGainDb);
        m_audio.systemGainDb = m_systemGainSlider->value();
    }

    // Apply the visual defaults without discarding the user's current camera
    // device, camera on/off choice, PPT image, capture mode or region.
    const OverlayConfig overlay = store.overlayDefaults();
    m_overlay.penColor = overlay.penColor;
    m_overlay.penWidth = overlay.penWidth;
    m_overlay.mouseSize = overlay.mouseSize;
    m_overlay.leftClickColor = overlay.leftClickColor;
    m_overlay.rightClickColor = overlay.rightClickColor;
    m_overlay.clickDurationMs = overlay.clickDurationMs;
    m_overlay.keyFontSize = overlay.keyFontSize;
    m_overlay.keyOpacity = overlay.keyOpacity;
    m_overlay.showMouseHighlight = overlay.showMouseHighlight;
    m_overlay.showKeys = overlay.showKeys;
    m_overlay.cameraMirror = overlay.cameraMirror;
    m_overlay.cameraOpacity = overlay.cameraOpacity;
    m_overlay.layout = overlay.layout;
    {
        const QSignalBlocker blocker(m_mirrorCheck);
        m_mirrorCheck->setChecked(overlay.cameraMirror);
    }
    {
        const QSignalBlocker blocker(m_mouseCheck);
        m_mouseCheck->setChecked(overlay.showMouseHighlight);
    }
    {
        const QSignalBlocker blocker(m_keysCheck);
        m_keysCheck->setChecked(overlay.showKeys);
    }
    {
        const QSignalBlocker blocker(m_cameraOpacitySlider);
        m_cameraOpacitySlider->setValue(qBound(20, overlay.cameraOpacity, 100));
        m_overlay.cameraOpacity = m_cameraOpacitySlider->value();
    }
    {
        const QSignalBlocker blocker(m_layoutCombo);
        const int index = m_layoutCombo->findData(static_cast<int>(overlay.layout));
        m_layoutCombo->setCurrentIndex(index >= 0 ? index : 0);
        m_overlay.layout = static_cast<LayoutTemplate>(m_layoutCombo->currentData().toInt());
    }

    updateBitrateLabel();
    updateGainLabels();
    updateLayoutControls();
    m_cameraOpacityLabel->setText(QStringLiteral("%1%").arg(m_overlay.cameraOpacity));
}

void RecordPage::setState(RecordState state)
{
    m_state = state;
    updateStateControls();
    if (state == RecordState::Recording)
        m_statusLabel->setText(QStringLiteral("正在录制…"));
    else if (state == RecordState::Paused)
        m_statusLabel->setText(QStringLiteral("已暂停"));
    else if (state == RecordState::Starting)
        m_statusLabel->setText(QStringLiteral("正在准备录制…"));
    else if (state == RecordState::Stopping)
        m_statusLabel->setText(QStringLiteral("正在保存录像…"));
    else
        m_statusLabel->setText(QStringLiteral("就绪"));
}

void RecordPage::setElapsed(qint64 elapsedMs)
{
    m_elapsedLabel->setText(formatDuration(elapsedMs));
}

void RecordPage::setEncoderText(const QString &text)
{
    m_encoderLabel->setText(text);
}

void RecordPage::setStatusText(const QString &text)
{
    m_statusLabel->setText(text);
}

void RecordPage::setOutputDir(const QString &dir)
{
    m_outputDirEdit->setText(dir);
    m_outputDirEdit->setToolTip(dir);
}

QString RecordPage::outputDir() const
{
    return m_outputDirEdit->text();
}

void RecordPage::setRegion(const QRect &region)
{
    m_video.region = region;
    m_video.mode = CaptureMode::Region;
    m_regionLabel->setText(QStringLiteral("%1, %2  ·  %3 × %4")
                               .arg(region.x()).arg(region.y())
                               .arg(region.width()).arg(region.height()));
    updateModeVisibility();
    emit optionsChanged();
}

void RecordPage::setWindow(WId windowId, const QString &title)
{
    m_video.windowId = windowId;
    m_video.windowTitle = title;
    m_video.mode = CaptureMode::Window;
    m_windowLabel->setText(title.isEmpty() ? QStringLiteral("未选择窗口") : title);
    updateModeVisibility();
    emit optionsChanged();
}

} // namespace lightrec
