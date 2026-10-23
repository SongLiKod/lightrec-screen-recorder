#pragma once

#include "core/Types.h"

#include <QList>
#include <QMainWindow>

class QAction;
class QCloseEvent;
class QMenu;
class QStackedWidget;
class QPushButton;
class QSystemTrayIcon;

namespace lightrec {

class OverlayController;
class RecordingController;
class PresetManager;
class ScheduleManager;
class RecordingLibrary;
class VirtualCamera;
class FloatingToolbar;
class FloatingRecordDot;
class CountdownWindow;
class CaptureBorderWindow;
class RecordPage;
class FilesPage;
class ToolboxPage;
class LivePage;
class TasksPage;
class PresetsPage;
class SettingsPage;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /** Headless diagnostic: record for N seconds, then exit (--record-test). */
    void startDiagnosticRecording(int seconds);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void buildNavigation();
    void wireSignals();
    void registerHotkeys();
    void setupTray();
    void showFromTray();
    void quitFromTray();
    void shutdownApplication();
    void updateTrayState(RecordState state);

    RecordOptions currentOptions() const;
    OverlayConfig mergedOverlay() const;
    void startRecording(const StreamConfig *streamOverride = nullptr);
    void beginRecording(const RecordOptions &options);
    void cancelCountdown();
    void stopRecording();
    void setVirtualCameraTap(bool enabled);
    void applyPreset(const Preset &preset);
    void selectRegion();
    void adjustRegion();
    void selectWindow();
    void showPage(int index);
    void handleStateChanged(RecordState state);
    void handleRecordingFinished(const QString &path, bool success, const QString &message);
    void checkRecovery();
    void updateCaptureBorder();

    OverlayController *m_overlay = nullptr;
    RecordingController *m_controller = nullptr;
    PresetManager *m_presets = nullptr;
    ScheduleManager *m_schedule = nullptr;
    RecordingLibrary *m_library = nullptr;
    VirtualCamera *m_virtualCamera = nullptr;
    FloatingToolbar *m_toolbar = nullptr;
    FloatingRecordDot *m_recordDot = nullptr;
    CountdownWindow *m_countdown = nullptr;
    CaptureBorderWindow *m_captureBorder = nullptr;
    QTimer *m_captureBorderTimer = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_trayRecordAction = nullptr;

    QStackedWidget *m_pages = nullptr;
    RecordPage *m_recordPage = nullptr;
    FilesPage *m_filesPage = nullptr;
    ToolboxPage *m_toolboxPage = nullptr;
    LivePage *m_livePage = nullptr;
    TasksPage *m_tasksPage = nullptr;
    PresetsPage *m_presetsPage = nullptr;
    SettingsPage *m_settingsPage = nullptr;
    QList<QPushButton *> m_navButtons;

    QString m_activeTaskId;
    CaptureMode m_borderMode = CaptureMode::FullScreen;
    bool m_closeAfterStop = false;
    bool m_quitting = false;
    bool m_trayHintShown = false;
    bool m_recoveryChecked = false;
    bool m_diagnosticMode = false;
    QMetaObject::Connection m_virtualCameraTap;
};

} // namespace lightrec
