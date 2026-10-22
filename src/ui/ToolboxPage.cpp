#include "ui/ToolboxPage.h"

#include "core/ConfigStore.h"
#include "media/FFmpegTool.h"
#include "ui/Widgets.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace lightrec {

namespace {
QDoubleSpinBox *secondsSpin(double maximum, QWidget *parent)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setRange(0.0, maximum);
    spin->setDecimals(2);
    spin->setSuffix(QStringLiteral(" 秒"));
    spin->setSingleStep(1.0);
    return spin;
}

QLabel *description(const QString &text, QWidget *parent)
{
    auto *label = ui::mutedLabel(text, parent);
    label->setWordWrap(true);
    return label;
}
} // namespace

ToolboxPage::ToolboxPage(QWidget *parent)
    : QWidget(parent)
    , m_ffmpeg(new FFmpegTool(this))
{
    buildUi();

    connect(m_ffmpeg, &FFmpegTool::logLine, this, &ToolboxPage::appendLog);
    connect(m_ffmpeg, &FFmpegTool::progressChanged, this, [this](int percent, qint64) {
        if (percent < 0) {
            m_progress->setRange(0, 0);
        } else {
            m_progress->setRange(0, 100);
            m_progress->setValue(percent);
        }
    });
    connect(m_ffmpeg, &FFmpegTool::finished, this, &ToolboxPage::handleFinished);
    refreshWatermarkPresets();
}

void ToolboxPage::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 24);
    layout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("视频工具箱"), this);
    title->setObjectName(QStringLiteral("appTitle"));
    layout->addWidget(title);
    layout->addWidget(ui::mutedLabel(
        QStringLiteral("内置修复、变速、裁剪、压缩、GIF 与水印，无需第三方剪辑软件。"), this));

    auto *body = new QHBoxLayout();
    body->setSpacing(16);

    m_toolList = new QListWidget(this);
    m_toolList->setObjectName(QStringLiteral("toolList"));
    m_toolList->setFixedWidth(190);
    const QStringList tools = {
        QStringLiteral("视频修复"), QStringLiteral("视频变速"), QStringLiteral("裁剪 / 画面裁切"),
        QStringLiteral("视频压缩"), QStringLiteral("导出 GIF"), QStringLiteral("水印 / 批量水印")
    };
    for (const QString &name : tools)
        m_toolList->addItem(name);
    m_toolList->setCurrentRow(0);
    body->addWidget(m_toolList);

    auto *right = new QVBoxLayout();
    right->setSpacing(12);

    QFrame *ioCard = ui::card(this);
    auto *ioLayout = new QVBoxLayout(ioCard);
    ioLayout->setContentsMargins(16, 14, 16, 14);
    ioLayout->setSpacing(8);

    auto *inputRow = new QWidget(ioCard);
    auto *inputLayout = new QHBoxLayout(inputRow);
    inputLayout->setContentsMargins(0, 0, 0, 0);
    auto *inputLabel = new QLabel(QStringLiteral("源文件"), inputRow);
    inputLabel->setObjectName(QStringLiteral("mutedLabel"));
    m_inputEdit = new QLineEdit(inputRow);
    m_inputEdit->setPlaceholderText(QStringLiteral("选择需要处理的 MP4 文件"));
    auto *inputBrowse = ui::secondaryButton(QStringLiteral("浏览"), inputRow);
    inputLayout->addWidget(inputLabel);
    inputLayout->addWidget(m_inputEdit, 1);
    inputLayout->addWidget(inputBrowse);

    auto *outputRow = new QWidget(ioCard);
    auto *outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    auto *outputLabel = new QLabel(QStringLiteral("输出到"), outputRow);
    outputLabel->setObjectName(QStringLiteral("mutedLabel"));
    m_outputEdit = new QLineEdit(outputRow);
    m_outputEdit->setPlaceholderText(QStringLiteral("留空则自动生成"));
    auto *outputBrowse = ui::secondaryButton(QStringLiteral("另存为"), outputRow);
    outputLayout->addWidget(outputLabel);
    outputLayout->addWidget(m_outputEdit, 1);
    outputLayout->addWidget(outputBrowse);

    ioLayout->addWidget(inputRow);
    ioLayout->addWidget(outputRow);
    right->addWidget(ioCard);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildRepairPage());
    m_stack->addWidget(buildSpeedPage());
    m_stack->addWidget(buildTrimPage());
    m_stack->addWidget(buildCompressPage());
    m_stack->addWidget(buildGifPage());
    m_stack->addWidget(buildWatermarkPage());
    right->addWidget(m_stack, 1);

    auto *actionRow = new QHBoxLayout();
    m_startButton = ui::primaryButton(QStringLiteral("开始处理"), this);
    m_cancelButton = ui::secondaryButton(QStringLiteral("取消"), this);
    m_cancelButton->setEnabled(false);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setFixedWidth(240);
    actionRow->addWidget(m_startButton);
    actionRow->addWidget(m_cancelButton);
    actionRow->addStretch(1);
    actionRow->addWidget(m_progress);
    right->addLayout(actionRow);

    m_log = new QPlainTextEdit(this);
    m_log->setObjectName(QStringLiteral("logView"));
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(130);
    m_log->setPlaceholderText(QStringLiteral("处理日志…"));
    right->addWidget(m_log);

    body->addLayout(right, 1);
    layout->addLayout(body, 1);

    connect(inputBrowse, &QPushButton::clicked, this, &ToolboxPage::browseInput);
    connect(outputBrowse, &QPushButton::clicked, this, &ToolboxPage::browseOutput);
    connect(m_startButton, &QPushButton::clicked, this, &ToolboxPage::startJob);
    connect(m_cancelButton, &QPushButton::clicked, this, &ToolboxPage::cancelJob);
    connect(m_inputEdit, &QLineEdit::textChanged, this, [this] { updateOutputSuggestion(); });
    connect(m_toolList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0)
            return;
        m_operation = static_cast<Operation>(row);
        m_stack->setCurrentIndex(row);
        m_outputEdit->clear();
        updateOutputSuggestion();
    });
}

QWidget *ToolboxPage::buildRepairPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(ui::sectionTitle(QStringLiteral("损坏录屏修复"), page));
    layout->addWidget(description(
        QStringLiteral("修复异常中断、索引损坏的 MP4。LightRec 录制时使用分片 MP4 缓存，"
                       "崩溃后通常可以直接恢复；选择 .part 缓存或损坏的 MP4 后点击开始处理。"),
        page));
    layout->addStretch(1);
    return page;
}

QWidget *ToolboxPage::buildSpeedPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(ui::sectionTitle(QStringLiteral("视频变速导出"), page));
    m_speedSpin = new QDoubleSpinBox(page);
    m_speedSpin->setRange(0.5, 2.0);
    m_speedSpin->setSingleStep(0.05);
    m_speedSpin->setValue(1.5);
    m_speedSpin->setSuffix(QStringLiteral(" x"));
    layout->addWidget(ui::labeledRow(QStringLiteral("播放速度"), m_speedSpin));
    layout->addWidget(description(QStringLiteral("支持 0.5x ~ 2.0x；音频会同步变速并保持音调。"), page));
    layout->addStretch(1);
    return page;
}

QWidget *ToolboxPage::buildTrimPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(ui::sectionTitle(QStringLiteral("时间裁剪与画面裁切"), page));

    m_trimStartSpin = secondsSpin(86400, page);
    m_trimDurationSpin = secondsSpin(86400, page);
    layout->addWidget(ui::labeledRow(QStringLiteral("开始时间"), m_trimStartSpin));
    layout->addWidget(ui::labeledRow(QStringLiteral("持续时长"), m_trimDurationSpin));

    m_cropCheck = new QCheckBox(QStringLiteral("裁切画面"), page);
    layout->addWidget(m_cropCheck);

    auto *cropRow = new QWidget(page);
    auto *cropLayout = new QHBoxLayout(cropRow);
    cropLayout->setContentsMargins(0, 0, 0, 0);
    cropLayout->setSpacing(8);
    m_cropX = new QSpinBox(cropRow);
    m_cropY = new QSpinBox(cropRow);
    m_cropW = new QSpinBox(cropRow);
    m_cropH = new QSpinBox(cropRow);
    for (QSpinBox *spin : { m_cropX, m_cropY, m_cropW, m_cropH })
        spin->setRange(0, 8192);
    cropLayout->addWidget(new QLabel(QStringLiteral("X"), cropRow));
    cropLayout->addWidget(m_cropX);
    cropLayout->addWidget(new QLabel(QStringLiteral("Y"), cropRow));
    cropLayout->addWidget(m_cropY);
    cropLayout->addWidget(new QLabel(QStringLiteral("宽"), cropRow));
    cropLayout->addWidget(m_cropW);
    cropLayout->addWidget(new QLabel(QStringLiteral("高"), cropRow));
    cropLayout->addWidget(m_cropH);
    layout->addWidget(cropRow);
    layout->addWidget(description(QStringLiteral("时长为 0 表示直到视频结尾；宽高为 0 表示保持原尺寸。"), page));
    layout->addStretch(1);

    connect(m_cropCheck, &QCheckBox::toggled, this, &ToolboxPage::updateCropControls);
    updateCropControls();
    return page;
}

QWidget *ToolboxPage::buildCompressPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(ui::sectionTitle(QStringLiteral("视频压缩转码"), page));

    m_crfSlider = new QSlider(Qt::Horizontal, page);
    m_crfSlider->setRange(18, 34);
    m_crfSlider->setValue(26);
    m_crfLabel = ui::valueLabel(QStringLiteral("CRF 26"), page);
    auto *crfRow = new QWidget(page);
    auto *crfLayout = new QHBoxLayout(crfRow);
    crfLayout->setContentsMargins(0, 0, 0, 0);
    crfLayout->addWidget(m_crfSlider, 1);
    crfLayout->addWidget(m_crfLabel);
    layout->addWidget(ui::labeledRow(QStringLiteral("画质 (CRF)"), crfRow));

    m_scaleCombo = new QComboBox(page);
    m_scaleCombo->addItem(QStringLiteral("原始分辨率"), QSize());
    m_scaleCombo->addItem(QStringLiteral("1920 宽"), QSize(1920, 0));
    m_scaleCombo->addItem(QStringLiteral("1280 宽"), QSize(1280, 0));
    m_scaleCombo->addItem(QStringLiteral("854 宽"), QSize(854, 0));
    m_scaleCombo->addItem(QStringLiteral("640 宽"), QSize(640, 0));
    layout->addWidget(ui::labeledRow(QStringLiteral("输出分辨率"), m_scaleCombo));
    layout->addWidget(description(QStringLiteral("CRF 越大体积越小、画质越低；18 接近无损，28 左右适合网课存档。"), page));
    layout->addStretch(1);

    connect(m_crfSlider, &QSlider::valueChanged, this, [this](int value) {
        m_crfLabel->setText(QStringLiteral("CRF %1").arg(value));
    });
    return page;
}

QWidget *ToolboxPage::buildGifPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(ui::sectionTitle(QStringLiteral("选中片段导出 GIF"), page));

    m_gifStartSpin = secondsSpin(86400, page);
    m_gifDurationSpin = secondsSpin(86400, page);
    m_gifFpsSpin = new QSpinBox(page);
    m_gifFpsSpin->setRange(5, 30);
    m_gifFpsSpin->setValue(15);
    m_gifFpsSpin->setSuffix(QStringLiteral(" fps"));
    m_gifWidthSpin = new QSpinBox(page);
    m_gifWidthSpin->setRange(80, 1920);
    m_gifWidthSpin->setValue(480);
    m_gifWidthSpin->setSingleStep(40);
    m_gifWidthSpin->setSuffix(QStringLiteral(" px"));

    layout->addWidget(ui::labeledRow(QStringLiteral("开始时间"), m_gifStartSpin));
    layout->addWidget(ui::labeledRow(QStringLiteral("持续时长"), m_gifDurationSpin));
    layout->addWidget(ui::labeledRow(QStringLiteral("帧率"), m_gifFpsSpin));
    layout->addWidget(ui::labeledRow(QStringLiteral("宽度"), m_gifWidthSpin));
    layout->addWidget(description(QStringLiteral("使用调色板优化，导出的 GIF 可在浏览器、聊天工具中直接播放。"), page));
    layout->addStretch(1);
    return page;
}

QWidget *ToolboxPage::buildWatermarkPage()
{
    auto *page = new QWidget(this);
    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 8, 0);
    layout->setSpacing(9);

    layout->addWidget(ui::sectionTitle(QStringLiteral("水印与批量水印"), content));

    m_watermarkText = new QLineEdit(content);
    m_watermarkText->setPlaceholderText(QStringLiteral("文字水印内容，例如：© LightRec"));
    layout->addWidget(ui::labeledRow(QStringLiteral("文字水印"), m_watermarkText));

    auto *imageRow = new QWidget(content);
    auto *imageLayout = new QHBoxLayout(imageRow);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    m_watermarkImage = new QLineEdit(imageRow);
    m_watermarkImage->setPlaceholderText(QStringLiteral("图片水印（可选）"));
    auto *imageBrowse = ui::secondaryButton(QStringLiteral("浏览"), imageRow);
    imageLayout->addWidget(m_watermarkImage, 1);
    imageLayout->addWidget(imageBrowse);
    layout->addWidget(ui::labeledRow(QStringLiteral("图片水印"), imageRow));

    m_watermarkPosition = new QComboBox(content);
    m_watermarkPosition->addItem(QStringLiteral("右下角"), QStringLiteral("bottom-right"));
    m_watermarkPosition->addItem(QStringLiteral("左下角"), QStringLiteral("bottom-left"));
    m_watermarkPosition->addItem(QStringLiteral("右上角"), QStringLiteral("top-right"));
    m_watermarkPosition->addItem(QStringLiteral("左上角"), QStringLiteral("top-left"));
    m_watermarkPosition->addItem(QStringLiteral("居中"), QStringLiteral("center"));
    layout->addWidget(ui::labeledRow(QStringLiteral("位置"), m_watermarkPosition));

    m_watermarkMargin = new QSpinBox(content);
    m_watermarkMargin->setRange(0, 400);
    m_watermarkMargin->setValue(24);
    m_watermarkMargin->setSuffix(QStringLiteral(" px"));
    layout->addWidget(ui::labeledRow(QStringLiteral("边距"), m_watermarkMargin));

    m_watermarkFontSize = new QSpinBox(content);
    m_watermarkFontSize->setRange(8, 160);
    m_watermarkFontSize->setValue(28);
    layout->addWidget(ui::labeledRow(QStringLiteral("字号"), m_watermarkFontSize));

    auto *colorRow = new QWidget(content);
    auto *colorLayout = new QHBoxLayout(colorRow);
    colorLayout->setContentsMargins(0, 0, 0, 0);
    m_watermarkColorButton = ui::colorButton(m_watermarkColor, colorRow);
    colorLayout->addWidget(m_watermarkColorButton);
    colorLayout->addStretch(1);
    layout->addWidget(ui::labeledRow(QStringLiteral("颜色"), colorRow));

    m_watermarkOpacity = new QSlider(Qt::Horizontal, content);
    m_watermarkOpacity->setRange(0, 100);
    m_watermarkOpacity->setValue(85);
    m_watermarkOpacityLabel = ui::valueLabel(QStringLiteral("85%"), content);
    auto *opacityRow = new QWidget(content);
    auto *opacityLayout = new QHBoxLayout(opacityRow);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->addWidget(m_watermarkOpacity, 1);
    opacityLayout->addWidget(m_watermarkOpacityLabel);
    layout->addWidget(ui::labeledRow(QStringLiteral("透明度"), opacityRow));

    m_watermarkScale = new QSlider(Qt::Horizontal, content);
    m_watermarkScale->setRange(2, 100);
    m_watermarkScale->setValue(18);
    m_watermarkScaleLabel = ui::valueLabel(QStringLiteral("18%"), content);
    auto *scaleRow = new QWidget(content);
    auto *scaleLayout = new QHBoxLayout(scaleRow);
    scaleLayout->setContentsMargins(0, 0, 0, 0);
    scaleLayout->addWidget(m_watermarkScale, 1);
    scaleLayout->addWidget(m_watermarkScaleLabel);
    layout->addWidget(ui::labeledRow(QStringLiteral("图片大小"), scaleRow));

    auto *presetRow = new QWidget(content);
    auto *presetLayout = new QHBoxLayout(presetRow);
    presetLayout->setContentsMargins(0, 0, 0, 0);
    m_watermarkPresetCombo = new QComboBox(presetRow);
    auto *applyPreset = ui::secondaryButton(QStringLiteral("应用"), presetRow);
    auto *savePreset = ui::secondaryButton(QStringLiteral("保存方案"), presetRow);
    auto *deletePreset = ui::secondaryButton(QStringLiteral("删除"), presetRow);
    presetLayout->addWidget(m_watermarkPresetCombo, 1);
    presetLayout->addWidget(applyPreset);
    presetLayout->addWidget(savePreset);
    presetLayout->addWidget(deletePreset);
    layout->addWidget(ui::labeledRow(QStringLiteral("水印方案"), presetRow));

    layout->addWidget(ui::sectionTitle(QStringLiteral("批量处理"), content));
    m_batchList = new QListWidget(content);
    m_batchList->setMaximumHeight(110);
    layout->addWidget(m_batchList);
    auto *batchButtons = new QHBoxLayout();
    auto *addBatch = ui::secondaryButton(QStringLiteral("添加文件"), content);
    auto *removeBatch = ui::secondaryButton(QStringLiteral("移除选中"), content);
    batchButtons->addWidget(addBatch);
    batchButtons->addWidget(removeBatch);
    batchButtons->addStretch(1);
    layout->addLayout(batchButtons);
    layout->addWidget(description(
        QStringLiteral("批量列表非空时，将对列表中的文件依次添加水印；否则处理上方源文件。"), content));
    layout->addStretch(1);
    scroll->setWidget(content);

    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(scroll);

    connect(imageBrowse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择水印图片"),
                                                          QString(), QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp)"));
        if (!path.isEmpty())
            m_watermarkImage->setText(path);
    });
    connect(m_watermarkColorButton, &QPushButton::clicked, this, [this] {
        const QColor color = QColorDialog::getColor(m_watermarkColor, this, QStringLiteral("水印颜色"));
        if (color.isValid()) {
            m_watermarkColor = color;
            ui::updateColorButton(m_watermarkColorButton, color);
        }
    });
    connect(m_watermarkOpacity, &QSlider::valueChanged, this, [this](int value) {
        m_watermarkOpacityLabel->setText(QStringLiteral("%1%").arg(value));
    });
    connect(m_watermarkScale, &QSlider::valueChanged, this, [this](int value) {
        m_watermarkScaleLabel->setText(QStringLiteral("%1%").arg(value));
    });
    connect(addBatch, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("添加批量水印文件"),
                                                                QString(), QStringLiteral("视频 (*.mp4 *.mov *.mkv)"));
        for (const QString &file : files)
            m_batchList->addItem(file);
    });
    connect(removeBatch, &QPushButton::clicked, this, [this] {
        delete m_batchList->takeItem(m_batchList->currentRow());
    });
    connect(savePreset, &QPushButton::clicked, this, &ToolboxPage::saveWatermarkPreset);
    connect(deletePreset, &QPushButton::clicked, this, &ToolboxPage::deleteWatermarkPreset);
    connect(applyPreset, &QPushButton::clicked, this, [this] {
        const QString name = m_watermarkPresetCombo->currentText();
        if (name.isEmpty())
            return;
        QSettings settings;
        const QJsonObject presets = QJsonDocument::fromJson(
            settings.value(QStringLiteral("watermark/presets")).toString().toUtf8()).object();
        if (presets.contains(name))
            applyWatermark(ConfigStore::watermarkFromJson(presets.value(name).toObject()));
    });
    return page;
}

void ToolboxPage::updateCropControls()
{
    const bool enabled = m_cropCheck->isChecked();
    for (QSpinBox *spin : { m_cropX, m_cropY, m_cropW, m_cropH })
        spin->setEnabled(enabled);
}

void ToolboxPage::browseInput()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择源文件"), QString(),
        QStringLiteral("媒体文件 (*.mp4 *.mov *.mkv *.part *.flv);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    m_inputEdit->setText(path);
    updateOutputSuggestion();
}

void ToolboxPage::browseOutput()
{
    const QString extension = m_operation == Gif ? QStringLiteral("gif") : QStringLiteral("mp4");
    const QString filter = m_operation == Gif ? QStringLiteral("GIF (*.gif)")
                                              : QStringLiteral("MP4 (*.mp4)");
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("选择输出文件"),
                                                m_outputEdit->text(), filter);
    if (!path.isEmpty()) {
        if (!path.endsWith(QLatin1Char('.') + extension, Qt::CaseInsensitive))
            path += QLatin1Char('.') + extension;
        m_outputEdit->setText(path);
    }
}

void ToolboxPage::updateOutputSuggestion()
{
    if (!m_outputEdit->text().isEmpty())
        return;
    const QString input = m_inputEdit->text().trimmed();
    if (input.isEmpty())
        return;
    switch (m_operation) {
    case Repair: m_outputEdit->setText(suggestedOutput(input, QStringLiteral("_fixed"), QStringLiteral("mp4"))); break;
    case Speed: m_outputEdit->setText(suggestedOutput(input, QStringLiteral("_speed"), QStringLiteral("mp4"))); break;
    case Trim: m_outputEdit->setText(suggestedOutput(input, QStringLiteral("_trim"), QStringLiteral("mp4"))); break;
    case Compress: m_outputEdit->setText(suggestedOutput(input, QStringLiteral("_compressed"), QStringLiteral("mp4"))); break;
    case Gif: m_outputEdit->setText(suggestedOutput(input, QStringLiteral("_clip"), QStringLiteral("gif"))); break;
    case Watermark: m_outputEdit->setText(suggestedOutput(input, QStringLiteral("_watermark"), QStringLiteral("mp4"))); break;
    }
}

QString ToolboxPage::suggestedOutput(const QString &input, const QString &suffix,
                                     const QString &extension) const
{
    const QFileInfo info(input);
    QString base = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + suffix;
    QString path = base + QLatin1Char('.') + extension;
    int counter = 2;
    while (QFileInfo::exists(path))
        path = QStringLiteral("%1_%2.%3").arg(base).arg(counter++).arg(extension);
    return path;
}

void ToolboxPage::setInputFile(const QString &path)
{
    if (path.isEmpty())
        return;
    m_inputEdit->setText(path);
    m_outputEdit->clear();
    if (path.endsWith(QStringLiteral(".part"), Qt::CaseInsensitive))
        m_toolList->setCurrentRow(Repair);
    updateOutputSuggestion();
}

void ToolboxPage::repairFiles(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    if (m_running) {
        ui::warn(this, QStringLiteral("视频修复"), QStringLiteral("当前已有处理任务正在进行"));
        return;
    }
    m_operation = Repair;
    m_toolList->setCurrentRow(Repair);
    m_stack->setCurrentIndex(Repair);
    m_batchQueue = paths;
    m_batchIndex = 0;
    m_batchMode = paths.size() > 1;
    m_lastOutput.clear();
    m_outputEdit->clear();
    m_log->clear();
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    setRunning(true);
    processNextBatch();
}

void ToolboxPage::setBatchWatermarkFiles(const QStringList &paths)
{
    m_batchList->clear();
    for (const QString &path : paths)
        m_batchList->addItem(path);
    m_operation = Watermark;
    m_toolList->setCurrentRow(Watermark);
    m_stack->setCurrentIndex(Watermark);
    m_outputEdit->clear();
    appendLog(QStringLiteral("已加入 %1 个文件，配置水印后点击“开始处理”").arg(paths.size()));
}

void ToolboxPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshWatermarkPresets();
}

QStringList ToolboxPage::jobFiles() const
{
    if (m_operation == Watermark && m_batchList->count() > 0) {
        QStringList files;
        for (int i = 0; i < m_batchList->count(); ++i)
            files.append(m_batchList->item(i)->text());
        return files;
    }
    const QString input = m_inputEdit->text().trimmed();
    return input.isEmpty() ? QStringList() : QStringList{ input };
}

void ToolboxPage::startJob()
{
    if (m_running)
        return;
    if (!m_ffmpeg->available()) {
        ui::warn(this, QStringLiteral("缺少 FFmpeg"),
                 QStringLiteral("未找到 ffmpeg.exe。请将 FFmpeg 可执行文件放到程序目录后重试。"));
        return;
    }

    m_batchQueue = jobFiles();
    if (m_batchQueue.isEmpty()) {
        ui::warn(this, QStringLiteral("未选择文件"), QStringLiteral("请先选择需要处理的源文件。"));
        return;
    }
    for (const QString &file : m_batchQueue) {
        if (!QFileInfo::exists(file)) {
            ui::warn(this, QStringLiteral("文件不存在"), file);
            return;
        }
    }

    if (m_operation == Watermark) {
        const WatermarkConfig config = currentWatermark();
        if (config.text.trimmed().isEmpty() && config.imagePath.trimmed().isEmpty()) {
            ui::warn(this, QStringLiteral("水印为空"),
                     QStringLiteral("请填写文字水印或选择图片水印。"));
            return;
        }
    }

    m_batchIndex = 0;
    m_batchMode = m_batchQueue.size() > 1;
    m_lastOutput.clear();
    m_log->clear();
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    setRunning(true);
    processNextBatch();
}

void ToolboxPage::processNextBatch()
{
    if (m_batchIndex >= m_batchQueue.size()) {
        setRunning(false);
        return;
    }

    const QString input = m_batchQueue.at(m_batchIndex++);
    m_media = MediaProbe::probe(input);

    QString output;
    if (m_batchMode) {
        const QString suffix = m_operation == Repair ? QStringLiteral("_fixed")
                               : m_operation == Watermark ? QStringLiteral("_watermark")
                                                          : QStringLiteral("_out");
        output = suggestedOutput(input, suffix, QStringLiteral("mp4"));
    } else {
        output = m_outputEdit->text().trimmed();
        if (output.isEmpty()) {
            const QString extension = m_operation == Gif ? QStringLiteral("gif") : QStringLiteral("mp4");
            const QString suffix = m_operation == Repair ? QStringLiteral("_fixed")
                                   : m_operation == Speed ? QStringLiteral("_speed")
                                   : m_operation == Trim ? QStringLiteral("_trim")
                                   : m_operation == Compress ? QStringLiteral("_compressed")
                                   : m_operation == Gif ? QStringLiteral("_clip")
                                                        : QStringLiteral("_watermark");
            output = suggestedOutput(input, suffix, extension);
        }
    }
    QDir().mkpath(QFileInfo(output).absolutePath());
    m_lastOutput = output;

    qint64 durationMs = m_media.durationMs;
    QStringList arguments;

    switch (m_operation) {
    case Repair:
        arguments = FFmpegTool::repairArguments(input, output);
        break;
    case Speed:
        arguments = FFmpegTool::speedArguments(input, output, m_speedSpin->value(),
                                               m_media.hasAudio);
        break;
    case Trim: {
        QRect crop;
        if (m_cropCheck->isChecked() && m_cropW->value() > 0 && m_cropH->value() > 0)
            crop = QRect(m_cropX->value(), m_cropY->value(), m_cropW->value(), m_cropH->value());
        const qint64 startMs = static_cast<qint64>(m_trimStartSpin->value() * 1000);
        const qint64 trimMs = static_cast<qint64>(m_trimDurationSpin->value() * 1000);
        if (trimMs > 0)
            durationMs = trimMs;
        arguments = FFmpegTool::trimArguments(input, output, startMs, trimMs, crop,
                                              m_media.hasAudio);
        break;
    }
    case Compress: {
        const QSize scale = m_scaleCombo->currentData().toSize();
        arguments = FFmpegTool::compressArguments(input, output, m_crfSlider->value(), scale,
                                                  m_media.hasAudio);
        break;
    }
    case Gif: {
        const qint64 startMs = static_cast<qint64>(m_gifStartSpin->value() * 1000);
        const qint64 gifMs = static_cast<qint64>(m_gifDurationSpin->value() * 1000);
        if (gifMs > 0)
            durationMs = gifMs;
        arguments = FFmpegTool::gifArguments(input, output, startMs, gifMs,
                                             m_gifFpsSpin->value(), m_gifWidthSpin->value());
        break;
    }
    case Watermark: {
        const WatermarkConfig config = currentWatermark();
        QString textFile;
        if (config.imagePath.isEmpty() || !QFileInfo::exists(config.imagePath)) {
            textFile = QDir::tempPath() + QStringLiteral("/lightrec_watermark.txt");
            QFile file(textFile);
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                file.write(config.text.toUtf8());
        }
        arguments = FFmpegTool::watermarkArguments(input, output, config, textFile);
        break;
    }
    }

    appendLog(QStringLiteral("开始处理：%1").arg(input));
    m_ffmpeg->run(arguments, QStringLiteral("LightRec 视频工具箱"), durationMs);
}

void ToolboxPage::cancelJob()
{
    m_ffmpeg->cancel();
    setRunning(false);
    appendLog(QStringLiteral("已取消处理。"));
}

void ToolboxPage::handleFinished(bool success, const QString &message)
{
    if (!m_running)
        return;
    appendLog(message);
    if (!success) {
        setRunning(false);
        QMessageBox::warning(this, QStringLiteral("处理失败"), message);
        return;
    }

    if (m_batchIndex < m_batchQueue.size()) {
        processNextBatch();
        return;
    }

    setRunning(false);
    emit fileCreated(m_lastOutput);
    QMessageBox::information(this, QStringLiteral("处理完成"),
                             m_batchMode ? QStringLiteral("批量任务已全部完成。")
                                         : QStringLiteral("处理完成：\n%1").arg(m_lastOutput));
}

void ToolboxPage::appendLog(const QString &line)
{
    if (!m_log)
        return;
    m_log->appendPlainText(line);
}

void ToolboxPage::setRunning(bool running)
{
    m_running = running;
    m_startButton->setEnabled(!running);
    m_cancelButton->setEnabled(running);
    if (!running)
        m_progress->setRange(0, 100);
}

WatermarkConfig ToolboxPage::currentWatermark() const
{
    WatermarkConfig config;
    config.enabled = true;
    config.text = m_watermarkText->text().trimmed();
    config.imagePath = m_watermarkImage->text().trimmed();
    config.position = m_watermarkPosition->currentData().toString();
    config.margin = m_watermarkMargin->value();
    config.fontSize = m_watermarkFontSize->value();
    config.color = m_watermarkColor;
    config.opacity = m_watermarkOpacity->value();
    config.scale = m_watermarkScale->value() / 100.0;
    return config;
}

void ToolboxPage::applyWatermark(const WatermarkConfig &config)
{
    m_watermarkText->setText(config.text);
    m_watermarkImage->setText(config.imagePath);
    const int index = m_watermarkPosition->findData(config.position);
    if (index >= 0)
        m_watermarkPosition->setCurrentIndex(index);
    m_watermarkMargin->setValue(config.margin);
    m_watermarkFontSize->setValue(config.fontSize);
    m_watermarkColor = config.color;
    ui::updateColorButton(m_watermarkColorButton, config.color);
    m_watermarkOpacity->setValue(config.opacity);
    m_watermarkScale->setValue(static_cast<int>(config.scale * 100));
}

void ToolboxPage::refreshWatermarkPresets()
{
    if (!m_watermarkPresetCombo)
        return;
    const QString current = m_watermarkPresetCombo->currentText();
    m_watermarkPresetCombo->clear();
    QSettings settings;
    const QJsonObject presets = QJsonDocument::fromJson(
        settings.value(QStringLiteral("watermark/presets")).toString().toUtf8()).object();
    m_watermarkPresetCombo->addItems(presets.keys());
    const int index = m_watermarkPresetCombo->findText(current);
    if (index >= 0)
        m_watermarkPresetCombo->setCurrentIndex(index);
}

void ToolboxPage::saveWatermarkPreset()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("保存水印方案"),
                                               QStringLiteral("方案名称："), QLineEdit::Normal,
                                               m_watermarkPresetCombo->currentText(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    QSettings settings;
    QJsonObject presets = QJsonDocument::fromJson(
        settings.value(QStringLiteral("watermark/presets")).toString().toUtf8()).object();
    presets[name.trimmed()] = ConfigStore::watermarkToJson(currentWatermark());
    settings.setValue(QStringLiteral("watermark/presets"),
                      QString::fromUtf8(QJsonDocument(presets).toJson(QJsonDocument::Compact)));
    refreshWatermarkPresets();
    m_watermarkPresetCombo->setCurrentText(name.trimmed());
}

void ToolboxPage::deleteWatermarkPreset()
{
    const QString name = m_watermarkPresetCombo->currentText();
    if (name.isEmpty())
        return;
    QSettings settings;
    QJsonObject presets = QJsonDocument::fromJson(
        settings.value(QStringLiteral("watermark/presets")).toString().toUtf8()).object();
    presets.remove(name);
    settings.setValue(QStringLiteral("watermark/presets"),
                      QString::fromUtf8(QJsonDocument(presets).toJson(QJsonDocument::Compact)));
    refreshWatermarkPresets();
}

} // namespace lightrec
