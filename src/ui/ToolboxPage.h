#pragma once

#include "core/Types.h"
#include "media/MediaProbe.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;

namespace lightrec {

class FFmpegTool;

class ToolboxPage : public QWidget
{
    Q_OBJECT
public:
    explicit ToolboxPage(QWidget *parent = nullptr);

    void setInputFile(const QString &path);
    void repairFiles(const QStringList &paths);
    void setBatchWatermarkFiles(const QStringList &paths);
    void refreshWatermarkPresets();

signals:
    void fileCreated(const QString &path);

protected:
    void showEvent(QShowEvent *event) override;

private:
    enum Operation {
        Repair = 0,
        Speed = 1,
        Trim = 2,
        Compress = 3,
        Gif = 4,
        Watermark = 5
    };

    void buildUi();
    QWidget *buildRepairPage();
    QWidget *buildSpeedPage();
    QWidget *buildTrimPage();
    QWidget *buildCompressPage();
    QWidget *buildGifPage();
    QWidget *buildWatermarkPage();

    void browseInput();
    void browseOutput();
    void updateOutputSuggestion();
    QString suggestedOutput(const QString &input, const QString &suffix, const QString &extension) const;
    void startJob();
    void cancelJob();
    void processNextBatch();
    void handleFinished(bool success, const QString &message);
    void appendLog(const QString &line);
    void setRunning(bool running);
    QStringList jobFiles() const;
    WatermarkConfig currentWatermark() const;
    void applyWatermark(const WatermarkConfig &config);
    void saveWatermarkPreset();
    void deleteWatermarkPreset();
    void updateCropControls();

    QListWidget *m_toolList = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLineEdit *m_inputEdit = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QProgressBar *m_progress = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    QDoubleSpinBox *m_speedSpin = nullptr;
    QDoubleSpinBox *m_trimStartSpin = nullptr;
    QDoubleSpinBox *m_trimDurationSpin = nullptr;
    QCheckBox *m_cropCheck = nullptr;
    QSpinBox *m_cropX = nullptr;
    QSpinBox *m_cropY = nullptr;
    QSpinBox *m_cropW = nullptr;
    QSpinBox *m_cropH = nullptr;
    QSlider *m_crfSlider = nullptr;
    QLabel *m_crfLabel = nullptr;
    QComboBox *m_scaleCombo = nullptr;
    QDoubleSpinBox *m_gifStartSpin = nullptr;
    QDoubleSpinBox *m_gifDurationSpin = nullptr;
    QSpinBox *m_gifFpsSpin = nullptr;
    QSpinBox *m_gifWidthSpin = nullptr;

    QLineEdit *m_watermarkText = nullptr;
    QLineEdit *m_watermarkImage = nullptr;
    QComboBox *m_watermarkPosition = nullptr;
    QSpinBox *m_watermarkMargin = nullptr;
    QSpinBox *m_watermarkFontSize = nullptr;
    QPushButton *m_watermarkColorButton = nullptr;
    QSlider *m_watermarkOpacity = nullptr;
    QSlider *m_watermarkScale = nullptr;
    QLabel *m_watermarkOpacityLabel = nullptr;
    QLabel *m_watermarkScaleLabel = nullptr;
    QListWidget *m_batchList = nullptr;
    QComboBox *m_watermarkPresetCombo = nullptr;
    QColor m_watermarkColor = Qt::white;

    FFmpegTool *m_ffmpeg = nullptr;
    MediaInfo m_media;
    Operation m_operation = Repair;
    QStringList m_batchQueue;
    int m_batchIndex = 0;
    bool m_running = false;
    bool m_batchMode = false;
    QString m_lastOutput;
};

} // namespace lightrec
