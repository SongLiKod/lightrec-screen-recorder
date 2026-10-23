#include "ui/SettingsPage.h"

#include "core/AppPaths.h"
#include "core/ConfigStore.h"
#include "core/HotkeyManager.h"
#include "core/ThemeManager.h"
#include "media/FFmpegInstaller.h"
#include "ui/Widgets.h"
#include "update/UpdateManager.h"
#include "virtualcam/VirtualCamera.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lightrec {

namespace {
QFrame *settingsCard(QWidget *parent, const QString &title, QVBoxLayout **contentLayout)
{
    QFrame *card = ui::card(parent);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);
    layout->addWidget(ui::sectionTitle(title, card));
    *contentLayout = layout;
    return card;
}
} // namespace

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    m_updateManager = new UpdateManager(this);
    connect(m_updateManager, &UpdateManager::statusChanged, this, [this](const QString &message) {
        if (m_updateStatus)
            m_updateStatus->setText(message);
    });
    connect(m_updateManager, &UpdateManager::progressChanged, this,
            [this](qint64 received, qint64 total) {
        if (!m_updateProgress)
            return;
        m_updateProgress->setVisible(true);
        if (total > 0) {
            m_updateProgress->setRange(0, 100);
            m_updateProgress->setValue(static_cast<int>(received * 100 / total));
        } else {
            m_updateProgress->setRange(0, 0);
        }
    });
    connect(m_updateManager, &UpdateManager::updateAvailable, this,
            [this](const QString &version, const QString &notes) {
        Q_UNUSED(notes)
        if (m_updateStatus)
            m_updateStatus->setText(QStringLiteral("发现新版本 %1，正在后台下载…").arg(version));
    });
    connect(m_updateManager, &UpdateManager::finished, this,
            [this](bool success, const QString &message, const QString &archivePath) {
        if (m_updateButton)
            m_updateButton->setEnabled(!m_recordingActive);
        if (m_updateProgress) {
            if (success && !archivePath.isEmpty()) {
                m_updateProgress->setRange(0, 100);
                m_updateProgress->setValue(100);
            } else {
                m_updateProgress->setVisible(false);
            }
        }
        if (m_updateStatus)
            m_updateStatus->setText(message);
        if (success && !archivePath.isEmpty())
            emit updateReadyToInstall(archivePath);
    });
    loadFromConfig();
}

void SettingsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Another page (or the live page) can also change persisted settings such
    // as the output directory or live port.  Refresh the form before editing
    // so Save cannot overwrite those values with stale widget state.
    loadFromConfig();
}

void SettingsPage::buildUi()
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

    auto *title = new QLabel(QStringLiteral("设置"), content);
    title->setObjectName(QStringLiteral("appTitle"));
    layout->addWidget(title);
    layout->addWidget(ui::mutedLabel(
        QStringLiteral("主题、热键、Overlay 样式、录制参数与虚拟摄像头。所有设置保存在本机。"), content));

    QVBoxLayout *cardLayout = nullptr;

    // Appearance ---------------------------------------------------------
    QFrame *appearance = settingsCard(content, QStringLiteral("外观与主题"), &cardLayout);
    auto *appearanceForm = new QFormLayout();
    appearanceForm->setSpacing(10);
    m_themeCombo = new QComboBox(appearance);
    m_themeCombo->addItem(QStringLiteral("跟随系统"), static_cast<int>(ThemeMode::System));
    m_themeCombo->addItem(QStringLiteral("浅色"), static_cast<int>(ThemeMode::Light));
    m_themeCombo->addItem(QStringLiteral("深色"), static_cast<int>(ThemeMode::Dark));
    appearanceForm->addRow(QStringLiteral("主题模式"), m_themeCombo);
    m_toolbarScaleCombo = new QComboBox(appearance);
    for (int scale : { 100, 125, 150, 175, 200 })
        m_toolbarScaleCombo->addItem(QStringLiteral("%1%").arg(scale), scale);
    appearanceForm->addRow(QStringLiteral("悬浮工具栏大小"), m_toolbarScaleCombo);

    m_toolbarStyleCombo = new QComboBox(appearance);
    m_toolbarStyleCombo->addItem(QStringLiteral("深色"), 0);
    m_toolbarStyleCombo->addItem(QStringLiteral("浅色"), 1);
    m_toolbarStyleCombo->addItem(QStringLiteral("跟随主题"), 2);
    appearanceForm->addRow(QStringLiteral("悬浮工具栏样式"), m_toolbarStyleCombo);

    auto *toolbarOpacityRow = new QWidget(appearance);
    auto *toolbarOpacityLayout = new QHBoxLayout(toolbarOpacityRow);
    toolbarOpacityLayout->setContentsMargins(0, 0, 0, 0);
    m_toolbarOpacity = new QSlider(Qt::Horizontal, toolbarOpacityRow);
    m_toolbarOpacity->setRange(40, 100);
    m_toolbarOpacityLabel = ui::valueLabel(QStringLiteral("92%"), toolbarOpacityRow);
    toolbarOpacityLayout->addWidget(m_toolbarOpacity, 1);
    toolbarOpacityLayout->addWidget(m_toolbarOpacityLabel);
    appearanceForm->addRow(QStringLiteral("悬浮工具栏透明度"), toolbarOpacityRow);

    m_showFloatingToolbar = new QCheckBox(QStringLiteral("录制时显示悬浮工具栏"), appearance);
    m_showFloatingToolbar->setChecked(true);
    appearanceForm->addRow(QString(), m_showFloatingToolbar);

    m_captureFloatingToolbar = new QCheckBox(QStringLiteral("录制视频中包含悬浮工具栏"), appearance);
    m_captureFloatingToolbar->setToolTip(
        QStringLiteral("开启后，屏幕/区域录制会包含悬浮工具栏；窗口录制始终只捕获目标窗口内容。"));
    appearanceForm->addRow(QString(), m_captureFloatingToolbar);

    m_floatingDot = new QCheckBox(QStringLiteral("显示悬浮录制圆点（默认开启）"), appearance);
    m_floatingDot->setChecked(true);
    appearanceForm->addRow(QString(), m_floatingDot);

    m_floatingDotSize = new QSpinBox(appearance);
    m_floatingDotSize->setRange(44, 120);
    m_floatingDotSize->setSuffix(QStringLiteral(" px"));
    appearanceForm->addRow(QStringLiteral("圆点大小"), m_floatingDotSize);
    cardLayout->addLayout(appearanceForm);
    cardLayout->addWidget(ui::mutedLabel(
        QStringLiteral("跟随系统模式会监听 Windows 主题变化并即时切换，无需重启。"
                       "界面已适配 100% / 125% / 150% / 175% / 200% 高 DPI 缩放，"
                       "字体优先 Microsoft YaHei UI，备选 Segoe UI。"), appearance));
    layout->addWidget(appearance);

    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const ThemeMode mode = static_cast<ThemeMode>(m_themeCombo->itemData(index).toInt());
        ThemeManager::instance().setMode(mode);
    });
    connect(m_toolbarScaleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                const int scale = m_toolbarScaleCombo->itemData(index).toInt();
                ConfigStore::instance().setToolbarScale(scale);
                emit toolbarScaleChanged(scale);
            });
    connect(m_toolbarStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                const int style = m_toolbarStyleCombo->itemData(index).toInt();
                ConfigStore::instance().setToolbarStyle(style);
                emit toolbarStyleChanged(style);
            });
    connect(m_toolbarOpacity, &QSlider::valueChanged, this, [this](int value) {
        m_toolbarOpacityLabel->setText(QStringLiteral("%1%").arg(value));
        ConfigStore::instance().setToolbarOpacity(value);
        emit toolbarOpacityChanged(value);
    });
    connect(m_showFloatingToolbar, &QCheckBox::toggled, this, [this](bool checked) {
        ConfigStore::instance().setShowFloatingToolbar(checked);
        emit showFloatingToolbarChanged(checked);
    });
    connect(m_captureFloatingToolbar, &QCheckBox::toggled, this, [this](bool checked) {
        ConfigStore::instance().setCaptureFloatingToolbar(checked);
        emit captureFloatingToolbarChanged(checked);
    });
    connect(m_floatingDot, &QCheckBox::toggled, this, [this](bool checked) {
        ConfigStore::instance().setFloatingDotEnabled(checked);
        emit floatingDotChanged(checked);
    });
    connect(m_floatingDotSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int size) {
        ConfigStore::instance().setFloatingDotSize(size);
        emit floatingDotSizeChanged(size);
    });

    // Storage ------------------------------------------------------------
    QFrame *storage = settingsCard(content, QStringLiteral("存储"), &cardLayout);
    auto *storageRow = new QWidget(storage);
    auto *storageLayout = new QHBoxLayout(storageRow);
    storageLayout->setContentsMargins(0, 0, 0, 0);
    m_outputDir = new QLineEdit(storageRow);
    auto *browse = ui::secondaryButton(QStringLiteral("更改"), storageRow);
    storageLayout->addWidget(m_outputDir, 1);
    storageLayout->addWidget(browse);
    cardLayout->addWidget(ui::labeledRow(QStringLiteral("录像保存目录"), storageRow));
    m_minimizeOnStart = new QCheckBox(QStringLiteral("开始录制后最小化主窗口"), storage);
    m_autoRecover = new QCheckBox(QStringLiteral("异常退出后自动检测并恢复录制缓存"), storage);
    m_countdownSpin = new QSpinBox(storage);
    m_countdownSpin->setRange(0, 30);
    m_countdownSpin->setSuffix(QStringLiteral(" 秒"));
    m_countdownSpin->setToolTip(QStringLiteral("0 表示立即开始"));
    cardLayout->addWidget(ui::labeledRow(QStringLiteral("开始录制倒计时"), m_countdownSpin));
    cardLayout->addWidget(m_minimizeOnStart);
    cardLayout->addWidget(m_autoRecover);
    layout->addWidget(storage);
    connect(browse, &QPushButton::clicked, this, &SettingsPage::browseOutput);

    // Hotkeys ------------------------------------------------------------
    QFrame *hotkeys = settingsCard(content, QStringLiteral("全局快捷键"), &cardLayout);
    auto *hotkeyForm = new QFormLayout();
    hotkeyForm->setSpacing(10);
    m_hotkeyStartStop = new QKeySequenceEdit(hotkeys);
    m_hotkeyPause = new QKeySequenceEdit(hotkeys);
    m_hotkeyDraw = new QKeySequenceEdit(hotkeys);
    m_hotkeyHide = new QKeySequenceEdit(hotkeys);
    m_hotkeyCamera = new QKeySequenceEdit(hotkeys);
    m_hotkeySpotlight = new QKeySequenceEdit(hotkeys);
    m_hotkeyMagnifier = new QKeySequenceEdit(hotkeys);
    m_hotkeyRegion = new QKeySequenceEdit(hotkeys);
    for (QKeySequenceEdit *edit : { m_hotkeyStartStop, m_hotkeyPause, m_hotkeyDraw, m_hotkeyHide,
                                    m_hotkeyCamera, m_hotkeySpotlight, m_hotkeyMagnifier,
                                    m_hotkeyRegion })
        edit->setMaximumSequenceLength(1);
    hotkeyForm->addRow(QStringLiteral("开始 / 停止录制"), m_hotkeyStartStop);
    hotkeyForm->addRow(QStringLiteral("暂停 / 继续"), m_hotkeyPause);
    hotkeyForm->addRow(QStringLiteral("开关实时画板"), m_hotkeyDraw);
    hotkeyForm->addRow(QStringLiteral("隐藏 / 显示 Overlay"), m_hotkeyHide);
    hotkeyForm->addRow(QStringLiteral("开关摄像头画中画"), m_hotkeyCamera);
    hotkeyForm->addRow(QStringLiteral("开关局部高亮"), m_hotkeySpotlight);
    hotkeyForm->addRow(QStringLiteral("开关局部放大"), m_hotkeyMagnifier);
    hotkeyForm->addRow(QStringLiteral("调整录制选区"), m_hotkeyRegion);
    cardLayout->addLayout(hotkeyForm);
    cardLayout->addWidget(ui::mutedLabel(
        QStringLiteral("快捷键全局生效；若被其他程序占用会提示失败。"), hotkeys));
    layout->addWidget(hotkeys);

    // Overlay ------------------------------------------------------------
    QFrame *overlay = settingsCard(content, QStringLiteral("Overlay 默认样式"), &cardLayout);
    auto *penRow = new QWidget(overlay);
    auto *penLayout = new QHBoxLayout(penRow);
    penLayout->setContentsMargins(0, 0, 0, 0);
    m_penColorButton = ui::colorButton(m_penColor, penRow);
    m_penWidth = new QSpinBox(penRow);
    m_penWidth->setRange(1, 24);
    m_penWidth->setSuffix(QStringLiteral(" px"));
    penLayout->addWidget(m_penColorButton);
    penLayout->addWidget(new QLabel(QStringLiteral("线宽"), penRow));
    penLayout->addWidget(m_penWidth);
    penLayout->addStretch(1);
    cardLayout->addWidget(ui::labeledRow(QStringLiteral("画笔"), penRow));

    m_mouseSize = new QSpinBox(overlay);
    m_mouseSize->setRange(16, 200);
    m_mouseSize->setSuffix(QStringLiteral(" px"));
    cardLayout->addWidget(ui::labeledRow(QStringLiteral("鼠标高亮大小"), m_mouseSize));

    auto *clickRow = new QWidget(overlay);
    auto *clickLayout = new QHBoxLayout(clickRow);
    clickLayout->setContentsMargins(0, 0, 0, 0);
    m_leftColorButton = ui::colorButton(m_leftColor, clickRow);
    m_rightColorButton = ui::colorButton(m_rightColor, clickRow);
    m_clickDuration = new QSpinBox(clickRow);
    m_clickDuration->setRange(120, 3000);
    m_clickDuration->setSingleStep(100);
    m_clickDuration->setSuffix(QStringLiteral(" ms"));
    clickLayout->addWidget(new QLabel(QStringLiteral("左键"), clickRow));
    clickLayout->addWidget(m_leftColorButton);
    clickLayout->addWidget(new QLabel(QStringLiteral("右键"), clickRow));
    clickLayout->addWidget(m_rightColorButton);
    clickLayout->addWidget(new QLabel(QStringLiteral("时长"), clickRow));
    clickLayout->addWidget(m_clickDuration);
    clickLayout->addStretch(1);
    cardLayout->addWidget(ui::labeledRow(QStringLiteral("点击特效"), clickRow));

    auto *keyRow = new QWidget(overlay);
    auto *keyLayout = new QHBoxLayout(keyRow);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    m_keyFontSize = new QSpinBox(keyRow);
    m_keyFontSize->setRange(10, 72);
    m_keyFontSize->setSuffix(QStringLiteral(" px"));
    m_keyOpacity = new QSlider(Qt::Horizontal, keyRow);
    m_keyOpacity->setRange(0, 100);
    m_keyOpacityLabel = ui::valueLabel(QStringLiteral("85%"), keyRow);
    m_resetKeyPosition = ui::secondaryButton(QStringLiteral("重置位置"), keyRow);
    keyLayout->addWidget(new QLabel(QStringLiteral("字号"), keyRow));
    keyLayout->addWidget(m_keyFontSize);
    keyLayout->addWidget(new QLabel(QStringLiteral("透明度"), keyRow));
    keyLayout->addWidget(m_keyOpacity, 1);
    keyLayout->addWidget(m_keyOpacityLabel);
    keyLayout->addWidget(m_resetKeyPosition);
    cardLayout->addWidget(ui::labeledRow(QStringLiteral("按键显示"), keyRow));
    cardLayout->addWidget(ui::mutedLabel(
        QStringLiteral("按键面板与摄像头画中画均可在屏幕上直接拖动调整位置。"), overlay));
    layout->addWidget(overlay);

    connect(m_penColorButton, &QPushButton::clicked, this, &SettingsPage::choosePenColor);
    connect(m_leftColorButton, &QPushButton::clicked, this, &SettingsPage::chooseLeftColor);
    connect(m_rightColorButton, &QPushButton::clicked, this, &SettingsPage::chooseRightColor);
    connect(m_keyOpacity, &QSlider::valueChanged, this, [this](int value) {
        m_keyOpacityLabel->setText(QStringLiteral("%1%").arg(value));
    });
    connect(m_resetKeyPosition, &QPushButton::clicked, this, [this] {
        m_overlay.keyPosition = QPoint(-1, -1);
    });

    // Camera -------------------------------------------------------------
    QFrame *camera = settingsCard(content, QStringLiteral("摄像头与布局"), &cardLayout);
    m_cameraMirror = new QCheckBox(QStringLiteral("默认镜像摄像头"), camera);
    cardLayout->addWidget(m_cameraMirror);
    m_layoutCombo = new QComboBox(camera);
    m_layoutCombo->addItem(QStringLiteral("单屏 + 画中画"), static_cast<int>(LayoutTemplate::Single));
    m_layoutCombo->addItem(QStringLiteral("二分屏（屏幕 + 摄像头）"), static_cast<int>(LayoutTemplate::Split));
    m_layoutCombo->addItem(QStringLiteral("三分屏（屏幕 + 摄像头 + PPT）"), static_cast<int>(LayoutTemplate::Triple));
    cardLayout->addWidget(ui::labeledRow(QStringLiteral("默认布局"), m_layoutCombo));
    auto *cameraOpacityRow = new QWidget(camera);
    auto *cameraOpacityLayout = new QHBoxLayout(cameraOpacityRow);
    cameraOpacityLayout->setContentsMargins(0, 0, 0, 0);
    m_cameraOpacity = new QSlider(Qt::Horizontal, cameraOpacityRow);
    m_cameraOpacity->setRange(20, 100);
    m_cameraOpacityLabel = ui::valueLabel(QStringLiteral("100%"), cameraOpacityRow);
    cameraOpacityLayout->addWidget(m_cameraOpacity, 1);
    cameraOpacityLayout->addWidget(m_cameraOpacityLabel);
    cardLayout->addWidget(ui::labeledRow(QStringLiteral("摄像头透明度"), cameraOpacityRow));
    layout->addWidget(camera);
    connect(m_cameraOpacity, &QSlider::valueChanged, this, [this](int value) {
        m_cameraOpacityLabel->setText(QStringLiteral("%1%").arg(value));
    });

    // Recording defaults -------------------------------------------------
    QFrame *recording = settingsCard(content, QStringLiteral("录制默认参数"), &cardLayout);
    auto *recordingForm = new QFormLayout();
    recordingForm->setSpacing(10);
    m_codecCombo = new QComboBox(recording);
    m_codecCombo->addItem(QStringLiteral("H.264"), QStringLiteral("h264"));
    m_codecCombo->addItem(QStringLiteral("H.265"), QStringLiteral("h265"));
    m_fpsCombo = new QComboBox(recording);
    for (int fps : { 15, 24, 30, 60 })
        m_fpsCombo->addItem(QStringLiteral("%1 fps").arg(fps), fps);
    recordingForm->addRow(QStringLiteral("默认编码"), m_codecCombo);
    recordingForm->addRow(QStringLiteral("默认帧率"), m_fpsCombo);
    cardLayout->addLayout(recordingForm);

    auto *bitrateRow = new QWidget(recording);
    auto *bitrateLayout = new QHBoxLayout(bitrateRow);
    bitrateLayout->setContentsMargins(0, 0, 0, 0);
    m_bitrateSlider = new QSlider(Qt::Horizontal, bitrateRow);
    m_bitrateSlider->setRange(2000, 20000);
    m_bitrateSlider->setSingleStep(500);
    m_bitrateLabel = ui::valueLabel(QStringLiteral("8 Mbps"), bitrateRow);
    bitrateLayout->addWidget(m_bitrateSlider, 1);
    bitrateLayout->addWidget(m_bitrateLabel);
    cardLayout->addWidget(ui::labeledRow(QStringLiteral("默认码率"), bitrateRow));
    connect(m_bitrateSlider, &QSlider::valueChanged, this, [this](int value) {
        m_bitrateLabel->setText(QStringLiteral("%1 Mbps").arg(value / 1000.0, 0, 'f', 1));
    });

    m_cursorCheck = new QCheckBox(QStringLiteral("默认录制鼠标指针"), recording);
    m_denoiseCheck = new QCheckBox(QStringLiteral("默认开启麦克风降噪"), recording);
    cardLayout->addWidget(m_cursorCheck);
    cardLayout->addWidget(m_denoiseCheck);

    auto *micGainRow = new QWidget(recording);
    auto *micGainLayout = new QHBoxLayout(micGainRow);
    micGainLayout->setContentsMargins(0, 0, 0, 0);
    m_micGainSlider = new QSlider(Qt::Horizontal, micGainRow);
    m_micGainSlider->setRange(-20, 20);
    m_micGainLabel = ui::valueLabel(QStringLiteral("+0 dB"), micGainRow);
    micGainLayout->addWidget(m_micGainSlider, 1);
    micGainLayout->addWidget(m_micGainLabel);
    cardLayout->addWidget(ui::labeledRow(QStringLiteral("麦克风增益"), micGainRow));

    auto *systemGainRow = new QWidget(recording);
    auto *systemGainLayout = new QHBoxLayout(systemGainRow);
    systemGainLayout->setContentsMargins(0, 0, 0, 0);
    m_systemGainSlider = new QSlider(Qt::Horizontal, systemGainRow);
    m_systemGainSlider->setRange(-20, 20);
    m_systemGainLabel = ui::valueLabel(QStringLiteral("+0 dB"), systemGainRow);
    systemGainLayout->addWidget(m_systemGainSlider, 1);
    systemGainLayout->addWidget(m_systemGainLabel);
    cardLayout->addWidget(ui::labeledRow(QStringLiteral("系统音量"), systemGainRow));

    connect(m_micGainSlider, &QSlider::valueChanged, this, [this](int value) {
        m_micGainLabel->setText(QStringLiteral("%1%2 dB").arg(value >= 0 ? QStringLiteral("+") : QString()).arg(value));
    });
    connect(m_systemGainSlider, &QSlider::valueChanged, this, [this](int value) {
        m_systemGainLabel->setText(QStringLiteral("%1%2 dB").arg(value >= 0 ? QStringLiteral("+") : QString()).arg(value));
    });
    layout->addWidget(recording);

    // FFmpeg runtime ------------------------------------------------------
    QFrame *ffmpeg = settingsCard(content, QStringLiteral("FFmpeg 运行时"), &cardLayout);
    m_ffmpegStatus = ui::mutedLabel(QString(), ffmpeg);
    m_ffmpegStatus->setWordWrap(true);
    m_ffmpegProgress = new QProgressBar(ffmpeg);
    m_ffmpegProgress->setRange(0, 100);
    m_ffmpegProgress->setValue(0);
    m_ffmpegProgress->setVisible(false);
    m_ffmpegInstallButton = ui::secondaryButton(QStringLiteral("下载并安装 FFmpeg"), ffmpeg);
    cardLayout->addWidget(m_ffmpegStatus);
    cardLayout->addWidget(m_ffmpegProgress);
    cardLayout->addWidget(m_ffmpegInstallButton, 0, Qt::AlignLeft);
    cardLayout->addWidget(ui::mutedLabel(
        QStringLiteral("官方安装包包含 ffmpeg.exe 与完整编码 DLL。安装过程静默进行，"
                       "视频工具箱可立即使用；重启软件后录制会优先使用完整编解码器。"),
        ffmpeg));
    layout->addWidget(ffmpeg);

    connect(m_ffmpegInstallButton, &QPushButton::clicked, this, &SettingsPage::startFfmpegInstall);

    // Live / virtual camera ---------------------------------------------
    QFrame *live = settingsCard(content, QStringLiteral("直播与虚拟摄像头"), &cardLayout);
    m_livePort = new QSpinBox(live);
    m_livePort->setRange(1024, 65535);
    m_livePort->setValue(8088);
    cardLayout->addWidget(ui::labeledRow(QStringLiteral("局域网直播端口"), m_livePort));

    m_virtualCamera = new QCheckBox(QStringLiteral("录制时输出到虚拟摄像头"), live);
    cardLayout->addWidget(m_virtualCamera);
    m_virtualCameraStatus = ui::mutedLabel(VirtualCamera::backendDescription(), live);
    cardLayout->addWidget(m_virtualCameraStatus);
    cardLayout->addWidget(ui::mutedLabel(
        QStringLiteral("虚拟摄像头通过共享内存帧桥发布画面；需安装配套虚拟摄像头设备后，"
                       "会议软件（Teams / Zoom / 腾讯会议）即可选择 LightRec Camera。"), live));
    layout->addWidget(live);

    // About / update ------------------------------------------------------
    QFrame *about = settingsCard(content, QStringLiteral("关于与更新"), &cardLayout);
    auto *versionRow = new QWidget(about);
    auto *versionLayout = new QHBoxLayout(versionRow);
    versionLayout->setContentsMargins(0, 0, 0, 0);
    m_versionLabel = ui::valueLabel(
        QStringLiteral("当前版本：%1").arg(UpdateManager::currentVersion()), versionRow);
    m_updateButton = ui::secondaryButton(QStringLiteral("检查更新"), versionRow);
    versionLayout->addWidget(m_versionLabel, 1);
    versionLayout->addWidget(m_updateButton);
    cardLayout->addWidget(versionRow);

    m_updateProgress = new QProgressBar(about);
    m_updateProgress->setRange(0, 100);
    m_updateProgress->setValue(0);
    m_updateProgress->setVisible(false);
    cardLayout->addWidget(m_updateProgress);
    m_updateStatus = ui::mutedLabel(QStringLiteral("点击“检查更新”获取最新版本"), about);
    m_updateStatus->setWordWrap(true);
    cardLayout->addWidget(m_updateStatus);
    cardLayout->addWidget(ui::mutedLabel(
        QStringLiteral("更新包从 GitHub Releases 下载；下载完成后由软件静默安装并自动重启，"
                       "不会打开浏览器或其他安装程序。录制期间不能安装更新。"), about));

    QLabel *notice = ui::mutedLabel(
        QStringLiteral("本软件为个人兴趣开发产物，仅供个人学习、娱乐、非商业性质免费使用\n"
                       "作者保留全部知识产权，未经书面许可不得二次开发、修改、复刻、衍生创作或用于任何商业营利用途\n"
                       "使用者需遵守当地法律法规与网络使用规范，违规使用的后果自行承担"),
        about);
    notice->setWordWrap(true);
    notice->setTextInteractionFlags(Qt::TextSelectableByMouse);
    notice->setStyleSheet(QStringLiteral("color:#8A94A6; line-height:1.6; padding:8px 0;"));
    cardLayout->addWidget(notice);
    layout->addWidget(about);

    connect(m_updateButton, &QPushButton::clicked, this, [this] {
        if (!m_updateManager || m_updateManager->isBusy())
            return;
        m_updateButton->setEnabled(false);
        m_updateProgress->setVisible(true);
        m_updateProgress->setRange(0, 0);
        m_updateStatus->setText(QStringLiteral("正在检查更新…"));
        m_updateManager->checkForUpdates();
    });

    auto *saveRow = new QHBoxLayout();
    saveRow->addStretch(1);
    auto *saveButton = ui::primaryButton(QStringLiteral("保存设置"), content);
    saveButton->setMinimumWidth(140);
    saveRow->addWidget(saveButton);
    layout->addLayout(saveRow);
    layout->addStretch(1);
    scroll->setWidget(content);

    connect(saveButton, &QPushButton::clicked, this, &SettingsPage::save);
}

void SettingsPage::browseOutput()
{
    const QString dir = ui::directoryDialog(this, QStringLiteral("选择录像保存目录"),
                                            m_outputDir->text());
    if (!dir.isEmpty())
        m_outputDir->setText(dir);
}

void SettingsPage::choosePenColor()
{
    const QColor color = QColorDialog::getColor(m_penColor, this, QStringLiteral("画笔颜色"));
    if (color.isValid()) {
        m_penColor = color;
        ui::updateColorButton(m_penColorButton, color);
    }
}

void SettingsPage::chooseLeftColor()
{
    const QColor color = QColorDialog::getColor(m_leftColor, this, QStringLiteral("左键特效颜色"));
    if (color.isValid()) {
        m_leftColor = color;
        ui::updateColorButton(m_leftColorButton, color);
    }
}

void SettingsPage::chooseRightColor()
{
    const QColor color = QColorDialog::getColor(m_rightColor, this, QStringLiteral("右键特效颜色"));
    if (color.isValid()) {
        m_rightColor = color;
        ui::updateColorButton(m_rightColorButton, color);
    }
}

void SettingsPage::loadFromConfig()
{
    ConfigStore &store = ConfigStore::instance();
    const ThemeMode theme = store.themeMode();
    {
        const QSignalBlocker blocker(m_themeCombo);
        m_themeCombo->setCurrentIndex(m_themeCombo->findData(static_cast<int>(theme)));
    }
    {
        const QSignalBlocker blocker(m_toolbarScaleCombo);
        const int index = m_toolbarScaleCombo->findData(store.toolbarScale());
        m_toolbarScaleCombo->setCurrentIndex(index >= 0 ? index : 2);
    }
    {
        const QSignalBlocker blocker(m_toolbarStyleCombo);
        const int index = m_toolbarStyleCombo->findData(store.toolbarStyle());
        m_toolbarStyleCombo->setCurrentIndex(index >= 0 ? index : 2);
    }
    {
        const QSignalBlocker blocker(m_toolbarOpacity);
        m_toolbarOpacity->setValue(store.toolbarOpacity());
        m_toolbarOpacityLabel->setText(QStringLiteral("%1%").arg(store.toolbarOpacity()));
    }
    {
        const QSignalBlocker blocker(m_showFloatingToolbar);
        m_showFloatingToolbar->setChecked(store.showFloatingToolbar());
    }
    {
        const QSignalBlocker blocker(m_captureFloatingToolbar);
        m_captureFloatingToolbar->setChecked(store.captureFloatingToolbar());
    }
    {
        const QSignalBlocker blocker(m_floatingDot);
        m_floatingDot->setChecked(store.floatingDotEnabled());
    }
    {
        const QSignalBlocker blocker(m_floatingDotSize);
        m_floatingDotSize->setValue(store.floatingDotSize());
    }
    m_outputDir->setText(store.outputDir());
    m_countdownSpin->setValue(store.countdownSeconds());
    m_minimizeOnStart->setChecked(store.minimizeOnStart());
    m_autoRecover->setChecked(store.autoRecoverEnabled());

    m_overlay = store.overlayDefaults();
    m_penColor = m_overlay.penColor;
    m_leftColor = m_overlay.leftClickColor;
    m_rightColor = m_overlay.rightClickColor;
    ui::updateColorButton(m_penColorButton, m_penColor);
    ui::updateColorButton(m_leftColorButton, m_leftColor);
    ui::updateColorButton(m_rightColorButton, m_rightColor);
    m_penWidth->setValue(m_overlay.penWidth);
    m_mouseSize->setValue(m_overlay.mouseSize);
    m_clickDuration->setValue(m_overlay.clickDurationMs);
    m_keyFontSize->setValue(m_overlay.keyFontSize);
    m_keyOpacity->setValue(m_overlay.keyOpacity);
    m_keyOpacityLabel->setText(QStringLiteral("%1%").arg(m_overlay.keyOpacity));
    m_cameraMirror->setChecked(m_overlay.cameraMirror);
    m_cameraOpacity->setValue(m_overlay.cameraOpacity);
    m_cameraOpacityLabel->setText(QStringLiteral("%1%").arg(m_overlay.cameraOpacity));
    const int layoutIndex = m_layoutCombo->findData(static_cast<int>(m_overlay.layout));
    if (layoutIndex >= 0)
        m_layoutCombo->setCurrentIndex(layoutIndex);

    const VideoConfig video = store.videoDefaults();
    const int codecIndex = m_codecCombo->findData(video.codec);
    if (codecIndex >= 0)
        m_codecCombo->setCurrentIndex(codecIndex);
    const int fpsIndex = m_fpsCombo->findData(video.fps);
    if (fpsIndex >= 0)
        m_fpsCombo->setCurrentIndex(fpsIndex);
    m_bitrateSlider->setValue(qBound(2000, video.bitrateKbps, 20000));
    m_bitrateLabel->setText(QStringLiteral("%1 Mbps").arg(m_bitrateSlider->value() / 1000.0, 0, 'f', 1));
    m_cursorCheck->setChecked(video.captureCursor);

    const AudioConfig audio = store.audioDefaults();
    m_denoiseCheck->setChecked(audio.noiseSuppression);
    m_micGainSlider->setValue(audio.micGainDb);
    m_systemGainSlider->setValue(audio.systemGainDb);
    m_micGainLabel->setText(QStringLiteral("%1%2 dB").arg(audio.micGainDb >= 0 ? QStringLiteral("+") : QString())
                                .arg(audio.micGainDb));
    m_systemGainLabel->setText(QStringLiteral("%1%2 dB").arg(audio.systemGainDb >= 0 ? QStringLiteral("+") : QString())
                                   .arg(audio.systemGainDb));

    m_hotkeyStartStop->setKeySequence(QKeySequence(store.hotkeyStartStop()));
    m_hotkeyPause->setKeySequence(QKeySequence(store.hotkeyPause()));
    m_hotkeyDraw->setKeySequence(QKeySequence(store.hotkeyDraw()));
    m_hotkeyHide->setKeySequence(QKeySequence(store.hotkeyHideOverlay()));
    m_hotkeyCamera->setKeySequence(QKeySequence(store.hotkeyCamera()));
    m_hotkeySpotlight->setKeySequence(QKeySequence(store.hotkeySpotlight()));
    m_hotkeyMagnifier->setKeySequence(QKeySequence(store.hotkeyMagnifier()));
    m_hotkeyRegion->setKeySequence(QKeySequence(store.hotkeyRegion()));

    m_livePort->setValue(store.liveServerPort());
    m_virtualCamera->setChecked(store.virtualCameraEnabled());
    m_virtualCameraStatus->setText(VirtualCamera::backendDescription());

    const QString ffmpegExe = AppPaths::ffmpegExe();
    if (QFileInfo::exists(ffmpegExe)) {
        m_ffmpegStatus->setText(QStringLiteral("已安装：%1").arg(ffmpegExe));
        m_ffmpegInstallButton->setText(QStringLiteral("重新安装 / 更新 FFmpeg"));
    } else {
        m_ffmpegStatus->setText(QStringLiteral("未找到 ffmpeg.exe。视频工具箱需要 FFmpeg 运行时，"
                                               "可点击下方按钮静默下载安装。"));
    }
}

void SettingsPage::startFfmpegInstall()
{
    if (!m_ffmpegInstaller) {
        m_ffmpegInstaller = new FFmpegInstaller(this);
        connect(m_ffmpegInstaller, &FFmpegInstaller::statusChanged,
                m_ffmpegStatus, &QLabel::setText);
        connect(m_ffmpegInstaller, &FFmpegInstaller::progressChanged, this,
                [this](qint64 received, qint64 total) {
                    m_ffmpegProgress->setVisible(true);
                    if (total > 0) {
                        m_ffmpegProgress->setRange(0, 100);
                        m_ffmpegProgress->setValue(static_cast<int>(received * 100 / total));
                    } else {
                        m_ffmpegProgress->setRange(0, 0);
                    }
                });
        connect(m_ffmpegInstaller, &FFmpegInstaller::finished, this,
                [this](bool success, const QString &message) {
                    m_ffmpegProgress->setVisible(false);
                    m_ffmpegInstallButton->setEnabled(true);
                    if (success) {
                        m_ffmpegStatus->setText(QStringLiteral("已安装：%1")
                                                    .arg(FFmpegInstaller::installedExecutable()));
                        m_ffmpegInstallButton->setText(QStringLiteral("重新安装 / 更新 FFmpeg"));
                        ui::info(this, QStringLiteral("FFmpeg"), message);
                    } else {
                        ui::warn(this, QStringLiteral("安装 FFmpeg 失败"), message);
                    }
                });
    }
    m_ffmpegInstallButton->setEnabled(false);
    m_ffmpegProgress->setVisible(true);
    m_ffmpegProgress->setRange(0, 0);
    m_ffmpegInstaller->install();
}

void SettingsPage::setRecordingActive(bool active)
{
    m_recordingActive = active;
    if (!m_updateButton)
        return;
    const bool busy = m_updateManager && m_updateManager->isBusy();
    m_updateButton->setEnabled(!active && !busy);
    m_updateButton->setToolTip(active ? QStringLiteral("录制期间不能安装更新")
                                      : QStringLiteral("检查并静默安装新版本"));
}

void SettingsPage::save()
{
    ConfigStore &store = ConfigStore::instance();
    store.setToolbarScale(m_toolbarScaleCombo->currentData().toInt());
    store.setToolbarStyle(m_toolbarStyleCombo->currentData().toInt());
    store.setToolbarOpacity(m_toolbarOpacity->value());
    store.setShowFloatingToolbar(m_showFloatingToolbar->isChecked());
    store.setCaptureFloatingToolbar(m_captureFloatingToolbar->isChecked());
    store.setFloatingDotEnabled(m_floatingDot->isChecked());
    store.setFloatingDotSize(m_floatingDotSize->value());
    store.setOutputDir(m_outputDir->text().trimmed());
    store.setCountdownSeconds(m_countdownSpin->value());
    store.setMinimizeOnStart(m_minimizeOnStart->isChecked());
    store.setAutoRecoverEnabled(m_autoRecover->isChecked());

    m_overlay.penColor = m_penColor;
    m_overlay.penWidth = m_penWidth->value();
    m_overlay.mouseSize = m_mouseSize->value();
    m_overlay.leftClickColor = m_leftColor;
    m_overlay.rightClickColor = m_rightColor;
    m_overlay.clickDurationMs = m_clickDuration->value();
    m_overlay.keyFontSize = m_keyFontSize->value();
    m_overlay.keyOpacity = m_keyOpacity->value();
    m_overlay.cameraMirror = m_cameraMirror->isChecked();
    m_overlay.cameraOpacity = m_cameraOpacity->value();
    m_overlay.layout = static_cast<LayoutTemplate>(m_layoutCombo->currentData().toInt());
    store.setOverlayDefaults(m_overlay);

    VideoConfig video = store.videoDefaults();
    video.codec = m_codecCombo->currentData().toString();
    video.fps = m_fpsCombo->currentData().toInt();
    video.bitrateKbps = m_bitrateSlider->value();
    video.captureCursor = m_cursorCheck->isChecked();
    store.setVideoDefaults(video);

    AudioConfig audio = store.audioDefaults();
    audio.noiseSuppression = m_denoiseCheck->isChecked();
    audio.micGainDb = m_micGainSlider->value();
    audio.systemGainDb = m_systemGainSlider->value();
    store.setAudioDefaults(audio);

    store.setLiveServerPort(m_livePort->value());
    store.setVirtualCameraEnabled(m_virtualCamera->isChecked());

    struct HotkeyEntry {
        QString action;
        QKeySequenceEdit *edit;
    };
    const HotkeyEntry hotkeys[] = {
        { QStringLiteral("startStop"), m_hotkeyStartStop },
        { QStringLiteral("pause"), m_hotkeyPause },
        { QStringLiteral("draw"), m_hotkeyDraw },
        { QStringLiteral("hideOverlay"), m_hotkeyHide },
        { QStringLiteral("camera"), m_hotkeyCamera },
        { QStringLiteral("spotlight"), m_hotkeySpotlight },
        { QStringLiteral("magnifier"), m_hotkeyMagnifier },
        { QStringLiteral("region"), m_hotkeyRegion }
    };
    QStringList failures;
    for (const HotkeyEntry &entry : hotkeys) {
        const QString sequence = entry.edit->keySequence().toString(QKeySequence::PortableText);
        const QString previous = store.hotkey(entry.action);
        if (sequence.isEmpty()) {
            // Clearing a shortcut disables it instead of silently keeping the
            // previously registered global hotkey alive.
            HotkeyManager::instance().unregisterHotkey(entry.action);
            store.setHotkey(entry.action, QString());
            continue;
        }
        if (HotkeyManager::instance().registerHotkey(entry.action, sequence)) {
            store.setHotkey(entry.action, sequence);
        } else {
            failures << QStringLiteral("%1：%2").arg(sequence, HotkeyManager::instance().lastError());
            // registerHotkey() unregisters the old binding first; restore it so
            // a failed edit does not leave the user without any hotkey.
            if (!previous.isEmpty())
                HotkeyManager::instance().registerHotkey(entry.action, previous);
        }
    }
    if (!failures.isEmpty())
        ui::warn(this, QStringLiteral("部分快捷键注册失败"), failures.join(QLatin1Char('\n')));

    emit virtualCameraToggled(m_virtualCamera->isChecked());
    emit settingsSaved();
    ui::info(this, QStringLiteral("设置"), QStringLiteral("设置已保存。"));
}

} // namespace lightrec
