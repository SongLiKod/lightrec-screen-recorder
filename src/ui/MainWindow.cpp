#include "ui/MainWindow.h"

#include "core/AppPaths.h"
#include "core/ConfigStore.h"
#include "core/HotkeyManager.h"
#include "core/Logger.h"
#include "core/ThemeManager.h"
#include "core/WindowUtils.h"
#include "capture/ScreenCapture.h"
#include "overlay/OverlayController.h"
#include "preset/PresetManager.h"
#include "record/RecordingController.h"
#include "record/RecordingJournal.h"
#include "record/RecordingLibrary.h"
#include "schedule/ScheduleManager.h"
#include "ui/CountdownWindow.h"
#include "ui/FilesPage.h"
#include "ui/FloatingRecordDot.h"
#include "ui/FloatingToolbar.h"
#include "ui/LivePage.h"
#include "ui/PresetsPage.h"
#include "ui/RecordPage.h"
#include "ui/RegionSelector.h"
#include "ui/SettingsPage.h"
#include "ui/TasksPage.h"
#include "ui/ToolboxPage.h"
#include "ui/Widgets.h"
#include "virtualcam/VirtualCamera.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace lightrec {

namespace {

bool chooseWindow(QWidget *parent, WId *windowId, QString *title)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("选择录制窗口"));
    dialog.resize(620, 460);
    auto *layout = new QVBoxLayout(&dialog);

    layout->addWidget(ui::mutedLabel(
        QStringLiteral("选择目标窗口后，录制画面会跟随窗口移动。最小化的窗口无法录制。"), &dialog));

    auto *list = new QListWidget(&dialog);
    layout->addWidget(list, 1);

    auto *buttons = new QHBoxLayout();
    auto *refresh = ui::secondaryButton(QStringLiteral("刷新"), &dialog);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->addWidget(refresh);
    buttons->addStretch(1);
    buttons->addWidget(box);
    layout->addLayout(buttons);

    auto fill = [list] {
        list->clear();
        for (const WindowInfo &window : ScreenCapture::windows()) {
            auto *item = new QListWidgetItem(
                QStringLiteral("%1\n%2").arg(window.title, window.processName), list);
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(window.id));
            item->setToolTip(QStringLiteral("%1 × %2").arg(window.rect.width()).arg(window.rect.height()));
        }
    };
    fill();

    QObject::connect(refresh, &QPushButton::clicked, &dialog, fill);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return false;
    QListWidgetItem *item = list->currentItem();
    if (!item)
        return false;
    *windowId = static_cast<WId>(item->data(Qt::UserRole).toULongLong());
    *title = item->text().section(QLatin1Char('\n'), 0, 0);
    return true;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("LightRec 轻录 - 屏幕录制"));
    setWindowIcon(WindowUtils::appIcon());
    const QRect available = QGuiApplication::primaryScreen()
                                ? QGuiApplication::primaryScreen()->availableGeometry()
                                : QRect(0, 0, 1180, 780);
    resize(qMin(1180, available.width() - 40), qMin(780, available.height() - 60));
    setMinimumSize(qMin(980, available.width() - 20), qMin(640, available.height() - 20));

    m_overlay = new OverlayController(this);
    m_controller = new RecordingController(m_overlay, this);
    m_presets = new PresetManager(this);
    m_schedule = new ScheduleManager(this);
    m_library = new RecordingLibrary(this);
    m_virtualCamera = new VirtualCamera(this);
    m_toolbar = new FloatingToolbar();
    m_toolbar->setScale(ConfigStore::instance().toolbarScale());
    m_toolbar->setStyle(ConfigStore::instance().toolbarStyle());
    m_toolbar->setOpacity(ConfigStore::instance().toolbarOpacity());
    m_recordDot = new FloatingRecordDot();

    buildUi();
    wireSignals();
    registerHotkeys();

    RecordOptions initial;
    initial.video = ConfigStore::instance().videoDefaults();
    initial.audio = ConfigStore::instance().audioDefaults();
    initial.overlay = ConfigStore::instance().overlayDefaults();
    initial.stream = ConfigStore::instance().streamDefaults();
    initial.minimizeOnStart = ConfigStore::instance().minimizeOnStart();
    m_recordPage->setOptions(initial);
    m_recordPage->setOutputDir(AppPaths::outputDir());
    m_overlay->applyConfig(initial.overlay);

    connect(m_controller, &RecordingController::frameReady,
            m_virtualCamera, &VirtualCamera::pushFrame);
    connect(m_recordDot, &FloatingRecordDot::toggleRequested, this, [this] {
        if (m_controller->state() == RecordState::Idle)
            startRecording();
        else
            stopRecording();
    });
    connect(m_recordDot, &FloatingRecordDot::showMainRequested, this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    if (ConfigStore::instance().floatingDotEnabled())
        m_recordDot->showAtSavedPosition();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](bool) {
        if (m_toolbar) {
            m_toolbar->setStyle(ConfigStore::instance().toolbarStyle());
            m_toolbar->update();
        }
        update();
    });

    QTimer::singleShot(600, this, &MainWindow::checkRecovery);
}

MainWindow::~MainWindow()
{
    m_livePage->stopAll();
    m_virtualCamera->stop();
    delete m_toolbar;
    delete m_recordDot;
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("mainWindow"));
    setCentralWidget(central);

    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *navPanel = new QWidget(central);
    navPanel->setObjectName(QStringLiteral("navPanel"));
    navPanel->setFixedWidth(210);
    auto *navLayout = new QVBoxLayout(navPanel);
    navLayout->setContentsMargins(16, 20, 16, 18);
    navLayout->setSpacing(8);

    auto *logoRow = new QHBoxLayout();
    auto *logo = new QLabel(navPanel);
    logo->setPixmap(WindowUtils::appIcon().pixmap(36, 36));
    auto *brand = new QVBoxLayout();
    auto *appTitle = new QLabel(QStringLiteral("LightRec"), navPanel);
    appTitle->setObjectName(QStringLiteral("appTitle"));
    auto *appSubtitle = new QLabel(QStringLiteral("轻录 · 屏幕录制"), navPanel);
    appSubtitle->setObjectName(QStringLiteral("appSubtitle"));
    brand->addWidget(appTitle);
    brand->addWidget(appSubtitle);
    logoRow->addWidget(logo);
    logoRow->addLayout(brand);
    logoRow->addStretch(1);
    navLayout->addLayout(logoRow);
    navLayout->addSpacing(18);

    m_pages = new QStackedWidget(central);

    m_recordPage = new RecordPage(central);
    m_filesPage = new FilesPage(m_library, central);
    m_toolboxPage = new ToolboxPage(central);
    m_livePage = new LivePage(m_controller, m_overlay, central);
    m_tasksPage = new TasksPage(m_schedule, m_presets, central);
    m_presetsPage = new PresetsPage(m_presets, central);
    m_settingsPage = new SettingsPage(central);

    m_pages->addWidget(m_recordPage);
    m_pages->addWidget(m_filesPage);
    m_pages->addWidget(m_toolboxPage);
    m_pages->addWidget(m_livePage);
    m_pages->addWidget(m_tasksPage);
    m_pages->addWidget(m_presetsPage);
    m_pages->addWidget(m_settingsPage);

    buildNavigation();
    navLayout->addStretch(1);
    auto *version = new QLabel(QStringLiteral("LightRec 1.1 · 本地优先"), navPanel);
    version->setObjectName(QStringLiteral("appSubtitle"));
    navLayout->addWidget(version);
    auto *privacy = new QLabel(QStringLiteral("录制内容仅保存在本机"), navPanel);
    privacy->setObjectName(QStringLiteral("appSubtitle"));
    navLayout->addWidget(privacy);

    root->addWidget(navPanel);
    root->addWidget(m_pages, 1);

    m_livePage->setOptionsProvider([this] {
        RecordOptions options = m_recordPage->options();
        options.overlay = mergedOverlay();
        return options;
    });
}

void MainWindow::buildNavigation()
{
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    const QStringList names = {
        QStringLiteral("●   录制"), QStringLiteral("▤   录制文件"),
        QStringLiteral("⚒   工具箱"), QStringLiteral("◉   直播"),
        QStringLiteral("⏱   定时任务"), QStringLiteral("★   预设"),
        QStringLiteral("⚙   设置")
    };

    auto *panel = findChild<QWidget *>(QStringLiteral("navPanel"));
    auto *layout = panel ? qobject_cast<QVBoxLayout *>(panel->layout()) : nullptr;
    if (!layout)
        return;

    for (int i = 0; i < names.size(); ++i) {
        auto *button = new QPushButton(names.at(i), panel);
        button->setObjectName(QStringLiteral("navButton"));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(40);
        group->addButton(button, i);
        layout->addWidget(button);
        m_navButtons.append(button);
        connect(button, &QPushButton::clicked, this, [this, i] { showPage(i); });
    }
    if (!m_navButtons.isEmpty())
        m_navButtons.first()->setChecked(true);
}

void MainWindow::showPage(int index)
{
    if (!m_pages || index < 0 || index >= m_pages->count())
        return;
    m_pages->setCurrentIndex(index);
    if (index < m_navButtons.size())
        m_navButtons.at(index)->setChecked(true);
}

void MainWindow::wireSignals()
{
    connect(m_recordPage, &RecordPage::startRequested, this, [this] { startRecording(); });
    connect(m_recordPage, &RecordPage::pauseRequested, this, [this] {
        if (m_controller->state() == RecordState::Paused)
            m_controller->resume();
        else
            m_controller->pause();
    });
    connect(m_recordPage, &RecordPage::stopRequested, this, &MainWindow::stopRecording);
    connect(m_recordPage, &RecordPage::selectRegionRequested, this, &MainWindow::selectRegion);
    connect(m_recordPage, &RecordPage::selectWindowRequested, this, &MainWindow::selectWindow);
    connect(m_recordPage, &RecordPage::outputDirChanged, this, [this](const QString &dir) {
        ConfigStore::instance().setOutputDir(dir);
        m_recordPage->setOutputDir(dir);
        m_filesPage->refresh();
    });
    connect(m_recordPage, &RecordPage::installFfmpegRequested, this, [this] {
        showPage(6);
        m_settingsPage->startFfmpegInstall();
    });

    connect(m_toolbar, &FloatingToolbar::startRequested, this, [this] { startRecording(); });
    connect(m_toolbar, &FloatingToolbar::pauseRequested, this, [this] {
        if (m_controller->state() == RecordState::Paused)
            m_controller->resume();
        else
            m_controller->pause();
    });
    connect(m_toolbar, &FloatingToolbar::stopRequested, this, &MainWindow::stopRecording);
    connect(m_toolbar, &FloatingToolbar::drawToggled, this, [this](bool enabled) {
        m_overlay->setDrawingEnabled(enabled);
        m_toolbar->setHint(enabled
                               ? QStringLiteral("标注模式：顶部工具条选择画笔/直线/箭头/矩形/圆形/文字；点击“退出”或按 Esc 结束")
                               : QStringLiteral("标注已关闭 · 高亮/放大：拖拽选框调整 · 再次点击按钮或 Esc 退出"));
    });
    connect(m_toolbar, &FloatingToolbar::overlaysToggled, this, [this](bool visible) {
        m_overlay->setOverlaysVisible(visible);
    });
    connect(m_toolbar, &FloatingToolbar::cameraToggled, this, [this](bool enabled) {
        m_overlay->setCameraEnabled(enabled);
        m_recordPage->setStatusText(enabled ? QStringLiteral("摄像头画中画已开启")
                                            : QStringLiteral("摄像头画中画已关闭"));
    });
    connect(m_toolbar, &FloatingToolbar::spotlightToggled, this, [this](bool enabled) {
        m_overlay->setSpotlightEnabled(enabled);
        if (enabled) {
            m_toolbar->setMagnifierEnabled(false);
            m_toolbar->setDrawEnabled(false);
            m_toolbar->setHint(QStringLiteral(
                "局部高亮已开启：拖动选框调整高亮区域；再次点击“高亮”或按 Esc 退出"));
            m_toolbar->showMessage(QStringLiteral("局部高亮已开启，拖动选框调整高亮区域"));
        } else {
            m_toolbar->setHint(QStringLiteral(
                "标注/高亮/放大：点击开启，再次点击或按 Esc 退出  ·  拖动工具栏可移动"));
        }
    });
    connect(m_toolbar, &FloatingToolbar::magnifierToggled, this, [this](bool enabled) {
        m_overlay->setMagnifierEnabled(enabled);
        if (enabled) {
            m_toolbar->setSpotlightEnabled(false);
            m_toolbar->setDrawEnabled(false);
            m_toolbar->setHint(QStringLiteral(
                "局部放大已开启：拖动选框选择放大区域；再次点击“放大”或按 Esc 退出"));
            m_toolbar->showMessage(QStringLiteral("局部放大已开启，拖动选框选择放大区域"));
        } else {
            m_toolbar->setHint(QStringLiteral(
                "标注/高亮/放大：点击开启，再次点击或按 Esc 退出  ·  拖动工具栏可移动"));
        }
    });
    connect(m_toolbar, &FloatingToolbar::adjustRegionRequested, this, &MainWindow::adjustRegion);
    connect(m_toolbar, &FloatingToolbar::showMainRequested, this, [this] {
        showNormal();
        raise();
        activateWindow();
    });

    connect(m_overlay, &OverlayController::drawingEnabledChanged, this, [this](bool enabled) {
        m_toolbar->setDrawEnabled(enabled);
        if (enabled) {
            m_toolbar->setHint(QStringLiteral(
                "标注模式：顶部工具条选择画笔/直线/箭头/矩形/圆形/文字；点击“退出”或按 Esc 结束"));
        }
        m_toolbar->raise();
        WindowUtils::setTopmost(m_toolbar->winId(), true);
    }, Qt::QueuedConnection);
    connect(m_overlay, &OverlayController::overlaysVisibleChanged, this, [this](bool visible) {
        m_toolbar->setOverlaysVisible(visible);
        m_toolbar->raise();
        WindowUtils::setTopmost(m_toolbar->winId(), true);
    }, Qt::QueuedConnection);
    connect(m_overlay, &OverlayController::spotlightEnabledChanged, this, [this](bool enabled) {
        m_toolbar->setSpotlightEnabled(enabled);
    });
    connect(m_overlay, &OverlayController::magnifierEnabledChanged, this, [this](bool enabled) {
        m_toolbar->setMagnifierEnabled(enabled);
    });
    connect(m_overlay, &OverlayController::cameraError, this, [this](const QString &message) {
        m_recordPage->setStatusText(QStringLiteral("摄像头：%1").arg(message));
        if (m_toolbar->isVisible())
            m_toolbar->showMessage(QStringLiteral("摄像头不可用：%1").arg(message));
    });

    connect(m_controller, &RecordingController::stateChanged, this, &MainWindow::handleStateChanged);
    connect(m_controller, &RecordingController::elapsedChanged, m_toolbar, &FloatingToolbar::setElapsed);
    connect(m_controller, &RecordingController::elapsedChanged, m_recordPage, &RecordPage::setElapsed);
    connect(m_controller, &RecordingController::encoderSelected, this, [this](const QString &name) {
        m_recordPage->setEncoderText(QStringLiteral("编码器：%1").arg(name));
    });
    connect(m_controller, &RecordingController::warning, this, [this](const QString &message) {
        m_recordPage->setStatusText(message);
        if (message.contains(QStringLiteral("硬件编码")))
            ui::warn(this, QStringLiteral("编码器提示"), message);
    });
    connect(m_controller, &RecordingController::errorOccurred, this, [this](const QString &message) {
        m_recordPage->setStatusText(message);
    });
    connect(m_controller, &RecordingController::recordingFinished,
            this, &MainWindow::handleRecordingFinished);

    connect(m_filesPage, &FilesPage::repairRequested, this, [this](const QString &path) {
        m_toolboxPage->setInputFile(path);
        showPage(2);
    });
    connect(m_filesPage, &FilesPage::batchRepairRequested, this, [this](const QStringList &paths) {
        m_toolboxPage->repairFiles(paths);
        showPage(2);
    });
    connect(m_filesPage, &FilesPage::batchWatermarkRequested, this, [this](const QStringList &paths) {
        m_toolboxPage->setBatchWatermarkFiles(paths);
        showPage(2);
    });
    connect(m_toolboxPage, &ToolboxPage::fileCreated, this, [this](const QString &path) {
        m_filesPage->refresh();
        m_filesPage->selectFile(path);
    });

    connect(m_presetsPage, &PresetsPage::applyRequested, this, &MainWindow::applyPreset);
    connect(m_presetsPage, &PresetsPage::saveCurrentRequested, this, [this](const QString &name) {
        const Preset preset = PresetManager::fromOptions(currentOptions(), name);
        QString error;
        if (!m_presets->save(preset, &error)) {
            ui::warn(this, QStringLiteral("保存预设"), error);
            return;
        }
        ConfigStore::instance().setLastPreset(name);
        m_presetsPage->refresh();
        m_presetsPage->selectPreset(name);
        m_recordPage->setStatusText(QStringLiteral("预设“%1”已保存").arg(name));
    });

    connect(m_settingsPage, &SettingsPage::settingsSaved, this, [this] {
        m_recordPage->setOutputDir(AppPaths::outputDir());
        m_recordPage->updateFfmpegStatus();
        m_toolbar->setScale(ConfigStore::instance().toolbarScale());
        m_filesPage->refresh();
        if (m_controller->state() == RecordState::Idle)
            m_overlay->applyConfig(ConfigStore::instance().overlayDefaults());
    });
    connect(m_settingsPage, &SettingsPage::toolbarScaleChanged, this, [this](int percent) {
        m_toolbar->setScale(percent);
        if (m_toolbar->isVisible()) {
            m_toolbar->showToolbar();
            m_toolbar->showMessage(QStringLiteral("工具栏大小：%1%").arg(percent));
        }
    });
    connect(m_settingsPage, &SettingsPage::toolbarStyleChanged, this, [this](int style) {
        m_toolbar->setStyle(style);
    });
    connect(m_settingsPage, &SettingsPage::toolbarOpacityChanged, this, [this](int opacity) {
        m_toolbar->setOpacity(opacity);
        m_recordDot->setOpacity(opacity);
    });
    connect(m_settingsPage, &SettingsPage::showFloatingToolbarChanged, this, [this](bool visible) {
        if (!visible) {
            m_toolbar->hideToolbar();
        } else if (m_controller->isRecording()) {
            m_toolbar->showToolbar();
        }
    });
    connect(m_settingsPage, &SettingsPage::floatingDotChanged, this, [this](bool enabled) {
        if (enabled)
            m_recordDot->showAtSavedPosition();
        else
            m_recordDot->hide();
    });
    connect(m_settingsPage, &SettingsPage::floatingDotSizeChanged, this, [this](int size) {
        m_recordDot->setDotSize(size);
        if (ConfigStore::instance().floatingDotEnabled())
            m_recordDot->showAtSavedPosition();
    });
    connect(m_settingsPage, &SettingsPage::virtualCameraToggled, this, [this](bool enabled) {
        if (enabled && m_controller->isRecording()) {
            QString error;
            if (!m_virtualCamera->start(&error))
                m_recordPage->setStatusText(error);
        } else if (!enabled) {
            m_virtualCamera->stop();
        }
    });

    connect(m_livePage, &LivePage::startStreamRequested, this, [this](const StreamConfig &stream) {
        startRecording(&stream);
    });
    connect(m_livePage, &LivePage::stopStreamRequested, this, &MainWindow::stopRecording);

    connect(m_schedule, &ScheduleManager::taskStartRequested,
            this, [this](const QString &taskId, const QString &presetName, const QString &taskName) {
        if (m_controller->state() != RecordState::Idle)
            return;
        if (!presetName.isEmpty() && m_presets->exists(presetName))
            applyPreset(m_presets->load(presetName));
        m_activeTaskId = taskId;
        m_schedule->setActiveTask(taskId);
        m_recordPage->setStatusText(QStringLiteral("定时任务“%1”启动").arg(taskName));
        startRecording();
        if (m_controller->state() == RecordState::Idle) {
            m_activeTaskId.clear();
            m_schedule->setActiveTask(QString());
        }
    });
    connect(m_schedule, &ScheduleManager::taskStopRequested,
            this, [this](const QString &taskId, const QString &) {
        if (m_activeTaskId == taskId && m_controller->isRecording())
            m_controller->stop();
    });
}

void MainWindow::registerHotkeys()
{
    HotkeyManager &hotkeys = HotkeyManager::instance();
    hotkeys.initialize();
    hotkeys.registerHotkey(QStringLiteral("startStop"), ConfigStore::instance().hotkeyStartStop());
    hotkeys.registerHotkey(QStringLiteral("pause"), ConfigStore::instance().hotkeyPause());
    hotkeys.registerHotkey(QStringLiteral("draw"), ConfigStore::instance().hotkeyDraw());
    hotkeys.registerHotkey(QStringLiteral("hideOverlay"), ConfigStore::instance().hotkeyHideOverlay());

    connect(&hotkeys, &HotkeyManager::hotkeyPressed, this, [this](const QString &action) {
        if (action == QStringLiteral("startStop")) {
            if (m_countdown)
                cancelCountdown();
            else if (m_controller->state() == RecordState::Idle)
                startRecording();
            else
                stopRecording();
        } else if (action == QStringLiteral("pause")) {
            if (m_controller->state() == RecordState::Recording)
                m_controller->pause();
            else if (m_controller->state() == RecordState::Paused)
                m_controller->resume();
        } else if (action == QStringLiteral("draw")) {
            if (m_controller->isRecording())
                m_overlay->toggleDrawing();
        } else if (action == QStringLiteral("hideOverlay")) {
            if (m_controller->isRecording())
                m_overlay->toggleOverlaysVisible();
        }
    });
}

OverlayConfig MainWindow::mergedOverlay() const
{
    OverlayConfig live = m_overlay->config();
    const OverlayConfig page = m_recordPage->options().overlay;
    live.showCamera = page.showCamera;
    live.cameraDeviceId = page.cameraDeviceId;
    live.layout = page.layout;
    live.cameraMirror = page.cameraMirror;
    live.cameraOpacity = page.cameraOpacity;
    live.pptImagePath = page.pptImagePath;
    live.showMouseHighlight = page.showMouseHighlight;
    live.showKeys = page.showKeys;
    return live;
}

RecordOptions MainWindow::currentOptions() const
{
    RecordOptions options = m_recordPage->options();
    options.overlay = mergedOverlay();
    options.minimizeOnStart = ConfigStore::instance().minimizeOnStart();

    QString outputDir = m_recordPage->outputDir().trimmed();
    if (outputDir.isEmpty())
        outputDir = AppPaths::outputDir();
    if (options.stream.enabled && !options.stream.saveLocal)
        options.outputPath.clear();
    else
        options.outputPath = AppPaths::uniqueOutputPath(outputDir);
    return options;
}

void MainWindow::startRecording(const StreamConfig *streamOverride)
{
    // UI Automation and some integrations can invoke this slot from a helper
    // thread. All recording windows and timers must be created on the GUI thread.
    if (QThread::currentThread() != thread()) {
        const bool hasStream = streamOverride != nullptr;
        const StreamConfig stream = hasStream ? *streamOverride : StreamConfig();
        QMetaObject::invokeMethod(this, [this, hasStream, stream] {
            startRecording(hasStream ? &stream : nullptr);
        }, Qt::QueuedConnection);
        return;
    }
    if (m_controller->state() != RecordState::Idle) {
        m_recordPage->setStatusText(QStringLiteral("当前已有录制任务在进行"));
        return;
    }
    if (m_countdown)
        return;

    RecordOptions options = currentOptions();
    if (streamOverride) {
        options.stream = *streamOverride;
        options.stream.enabled = true;
        if (options.stream.saveLocal) {
            const QString dir = m_recordPage->outputDir().isEmpty() ? AppPaths::outputDir()
                                                                    : m_recordPage->outputDir();
            options.outputPath = AppPaths::uniqueOutputPath(dir);
        } else {
            options.outputPath.clear();
        }
    }

    if (options.stream.enabled && options.stream.fullUrl().isEmpty()) {
        ui::warn(this, QStringLiteral("推流设置"), QStringLiteral("请先填写 RTMP 推流地址和密钥。"));
        return;
    }
    if (options.video.mode == CaptureMode::Region
        && (options.video.region.width() < 2 || options.video.region.height() < 2)) {
        ui::warn(this, QStringLiteral("录制选区"), QStringLiteral("请先框选录制区域。"));
        return;
    }
    if (options.video.mode == CaptureMode::Window && options.video.windowId == 0) {
        ui::warn(this, QStringLiteral("窗口录制"), QStringLiteral("请先选择要录制的窗口。"));
        return;
    }

    const int countdown = m_diagnosticMode ? 0 : ConfigStore::instance().countdownSeconds();
    if (countdown > 0) {
        m_countdown = new CountdownWindow(countdown);
        connect(m_countdown, &CountdownWindow::finished, this, [this, options] {
            CountdownWindow *window = m_countdown;
            m_countdown = nullptr;
            if (window)
                window->deleteLater();
            beginRecording(options);
        });
        connect(m_countdown, &CountdownWindow::canceled, this, [this] {
            CountdownWindow *window = m_countdown;
            m_countdown = nullptr;
            if (window)
                window->deleteLater();
            m_recordPage->setStatusText(QStringLiteral("已取消录制倒计时"));
        });
        m_recordPage->setStatusText(QStringLiteral("录制将在 %1 秒后开始").arg(countdown));
        m_countdown->start();
        return;
    }

    beginRecording(options);
}

void MainWindow::cancelCountdown()
{
    if (!m_countdown)
        return;
    CountdownWindow *window = m_countdown;
    m_countdown = nullptr;
    window->close();
    window->deleteLater();
    m_recordPage->setStatusText(QStringLiteral("已取消录制倒计时"));
}

void MainWindow::beginRecording(const RecordOptions &options)
{
    m_livePage->prepareForRecording();

    QString error;
    if (!m_controller->start(options, &error)) {
        m_recordPage->setStatusText(error);
        ui::warn(this, QStringLiteral("无法开始录制"), error);
        m_livePage->setRecordingState(RecordState::Idle);
        return;
    }

    m_toolbar->setDrawEnabled(false);
    m_toolbar->setOverlaysVisible(true);
    m_toolbar->setCameraEnabled(options.overlay.showCamera);
    m_toolbar->setSpotlightEnabled(false);
    m_toolbar->setMagnifierEnabled(false);
    m_toolbar->setRegionMode(options.video.mode == CaptureMode::Region);
    m_toolbar->setElapsed(0);
    m_recordPage->setStatusText(QStringLiteral("正在录制"));
    m_filesPage->refresh();

    if (ConfigStore::instance().virtualCameraEnabled()) {
        QString virtualError;
        if (!m_virtualCamera->start(&virtualError))
            m_recordPage->setStatusText(virtualError);
    }

    if (options.minimizeOnStart && !options.stream.enabled)
        showMinimized();

    // Show the toolbar after minimizing so the OS cannot hide it together with
    // the main window.
    if (ConfigStore::instance().showFloatingToolbar()) {
        m_toolbar->showToolbar();
        m_toolbar->setHint(QStringLiteral(
            "标注：切换画板工具进行绘制  ·  高亮/放大：拖拽选框调整  ·  "
            "再次点击工具按钮或按 Esc 退出  ·  拖动工具栏可移动"));
        m_toolbar->showMessage(QStringLiteral("录制已开始 · 可拖动工具栏调整位置"));
    }
    if (ConfigStore::instance().floatingDotEnabled())
        m_recordDot->showAtSavedPosition();
}

void MainWindow::stopRecording()
{
    if (m_countdown) {
        cancelCountdown();
        return;
    }
    if (m_controller->isRecording())
        m_controller->stop();
}

void MainWindow::startDiagnosticRecording(int seconds)
{
    if (m_controller->state() != RecordState::Idle)
        return;
    m_diagnosticMode = true;
    startRecording();
    if (m_controller->state() == RecordState::Idle) {
        m_diagnosticMode = false;
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        return;
    }
    QTimer::singleShot(qMax(1, seconds) * 1000, this, [this] { stopRecording(); });
}

void MainWindow::handleStateChanged(RecordState state)
{
    m_recordPage->setState(state);
    m_toolbar->setState(state);
    if (m_recordDot)
        m_recordDot->setState(state);

    if (state == RecordState::Starting || state == RecordState::Recording
        || state == RecordState::Paused) {
        if (ConfigStore::instance().showFloatingToolbar()) {
            m_toolbar->showToolbar();
            m_toolbar->raise();
            WindowUtils::setTopmost(m_toolbar->winId(), true);
        }
    }
    if (state == RecordState::Idle) {
        m_toolbar->hideToolbar();
        if (m_virtualCamera->isRunning())
            m_virtualCamera->stop();
    }
}

void MainWindow::handleRecordingFinished(const QString &path, bool success, const QString &message)
{
    if (m_diagnosticMode) {
        qInfo() << "Diagnostic recording finished:" << path << success << message;
        m_diagnosticMode = false;
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        return;
    }
    m_toolbar->hideToolbar();
    m_virtualCamera->stop();
    m_activeTaskId.clear();
    m_schedule->setActiveTask(QString());

    if (success) {
        if (!path.isEmpty()) {
            m_filesPage->refresh();
            m_filesPage->selectFile(path);
            m_recordPage->setStatusText(QStringLiteral("录制完成：%1").arg(ui::elidedPath(path, 48)));
        } else {
            m_recordPage->setStatusText(QStringLiteral("直播已结束"));
        }
    } else {
        m_recordPage->setStatusText(message);
        QMessageBox::warning(this, QStringLiteral("录制结束"),
                             message.isEmpty() ? QStringLiteral("录制未能正常保存。") : message);
    }

    if (m_closeAfterStop) {
        m_closeAfterStop = false;
        QTimer::singleShot(0, this, &QWidget::close);
    }
}

void MainWindow::applyPreset(const Preset &preset)
{
    if (m_controller->isRecording()) {
        ui::info(this, QStringLiteral("应用预设"), QStringLiteral("录制过程中无法切换预设。"));
        return;
    }
    const RecordOptions options = PresetManager::toOptions(preset);
    m_recordPage->setOptions(options);
    m_overlay->applyConfig(preset.overlay);
    ConfigStore::instance().setLastPreset(preset.name);
    m_recordPage->setStatusText(QStringLiteral("已应用预设“%1”").arg(preset.name));
}

void MainWindow::selectRegion()
{
    auto *selector = new RegionSelector(m_recordPage->options().video.region, false);
    connect(selector, &RegionSelector::regionSelected, this, [this](const QRect &region) {
        m_recordPage->setRegion(region);
        m_recordPage->setStatusText(QStringLiteral("选区：%1 × %2").arg(region.width()).arg(region.height()));
    });
    selector->show();
    selector->raise();
    selector->activateWindow();
    selector->setFocus();
}

void MainWindow::adjustRegion()
{
    if (!m_controller->isRecording()) {
        selectRegion();
        return;
    }
    const QRect current = m_controller->captureRect();
    auto *selector = new RegionSelector(current, true);
    connect(selector, &RegionSelector::regionSelected, this, [this](const QRect &region) {
        QString error;
        if (!m_controller->updateRegion(region, &error))
            ui::warn(this, QStringLiteral("调整选区"), error);
        else
            m_recordPage->setRegion(region);
    });
    selector->show();
    selector->raise();
    selector->activateWindow();
    selector->setFocus();
}

void MainWindow::selectWindow()
{
    WId windowId = 0;
    QString title;
    if (!chooseWindow(this, &windowId, &title))
        return;
    m_recordPage->setWindow(windowId, title);
    m_recordPage->setStatusText(QStringLiteral("窗口录制：%1").arg(title));
}

void MainWindow::checkRecovery()
{
    if (m_recoveryChecked)
        return;
    m_recoveryChecked = true;
    if (!ConfigStore::instance().autoRecoverEnabled())
        return;

    const QList<JournalEntry> entries = RecordingJournal::pendingSessions();
    if (entries.isEmpty())
        return;

    QStringList lines;
    for (const JournalEntry &entry : entries) {
        lines << QStringLiteral("· %1（%2）")
                     .arg(QFileInfo(entry.outputPath).fileName().isEmpty()
                              ? QStringLiteral("未命名录制")
                              : QFileInfo(entry.outputPath).fileName(),
                          entry.startedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    }

    const auto answer = QMessageBox::question(
        this, QStringLiteral("发现未完成的录制"),
        QStringLiteral("上次软件异常退出，检测到 %1 个可恢复的录制缓存：\n\n%2\n\n是否立即恢复？")
            .arg(entries.size())
            .arg(lines.join(QLatin1Char('\n'))),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
        return;

    QStringList recovered;
    QStringList failed;
    for (const JournalEntry &entry : entries) {
        QString path;
        QString error;
        if (RecordingJournal::recover(entry, &path, &error))
            recovered << path;
        else
            failed << error;
    }
    m_filesPage->refresh();
    if (!recovered.isEmpty()) {
        m_filesPage->selectFile(recovered.first());
        ui::info(this, QStringLiteral("恢复完成"),
                 QStringLiteral("已恢复 %1 个录像：\n%2")
                     .arg(recovered.size())
                     .arg(recovered.join(QLatin1Char('\n'))));
    }
    if (!failed.isEmpty())
        ui::warn(this, QStringLiteral("部分录像恢复失败"), failed.join(QLatin1Char('\n')));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_countdown) {
        cancelCountdown();
        event->accept();
        return;
    }
    if (m_controller->state() == RecordState::Stopping) {
        m_closeAfterStop = true;
        event->ignore();
        return;
    }
    if (m_controller->isRecording() || m_controller->state() == RecordState::Starting) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("正在录制"),
            QStringLiteral("当前正在录制，停止并退出吗？录像会先保存完成。"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_closeAfterStop = true;
        m_controller->stop();
        event->ignore();
        return;
    }

    m_livePage->stopAll();
    m_virtualCamera->stop();
    if (m_recordDot)
        m_recordDot->hide();
    event->accept();
}

} // namespace lightrec
