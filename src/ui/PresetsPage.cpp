#include "ui/PresetsPage.h"

#include "core/ConfigStore.h"
#include "preset/PresetManager.h"
#include "ui/Widgets.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

namespace lightrec {

PresetsPage::PresetsPage(PresetManager *presets, QWidget *parent)
    : QWidget(parent)
    , m_presets(presets)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 24);
    layout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("配置预设"), this);
    title->setObjectName(QStringLiteral("appTitle"));
    layout->addWidget(title);
    layout->addWidget(ui::mutedLabel(
        QStringLiteral("把录制区域、帧率、水印、摄像头布局、音频参数与画笔样式保存为一套预设，一键切换。"),
        this));

    auto *body = new QHBoxLayout();
    body->setSpacing(16);

    auto *left = new QVBoxLayout();
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("toolList"));
    m_list->setMinimumWidth(220);
    left->addWidget(m_list, 1);

    auto *listButtons = new QHBoxLayout();
    auto *saveButton = ui::primaryButton(QStringLiteral("保存当前配置"), this);
    auto *deleteButton = ui::dangerButton(QStringLiteral("删除"), this);
    listButtons->addWidget(saveButton);
    listButtons->addWidget(deleteButton);
    left->addLayout(listButtons);
    body->addLayout(left);

    auto *rightCard = ui::card(this);
    auto *rightLayout = new QVBoxLayout(rightCard);
    rightLayout->setContentsMargins(20, 18, 20, 18);
    rightLayout->setSpacing(12);
    rightLayout->addWidget(ui::sectionTitle(QStringLiteral("预设详情"), rightCard));
    m_details = new QLabel(rightCard);
    m_details->setWordWrap(true);
    m_details->setObjectName(QStringLiteral("statusLabel"));
    m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    rightLayout->addWidget(m_details, 1);

    auto *applyButton = ui::primaryButton(QStringLiteral("应用此预设"), rightCard);
    rightLayout->addWidget(applyButton, 0, Qt::AlignRight);
    body->addWidget(rightCard, 1);
    layout->addLayout(body, 1);

    connect(m_list, &QListWidget::currentItemChanged, this, [this] { updateDetails(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] {
        const QString name = selectedName();
        if (!name.isEmpty())
            emit applyRequested(m_presets->load(name));
    });
    connect(applyButton, &QPushButton::clicked, this, [this] {
        const QString name = selectedName();
        if (name.isEmpty()) {
            ui::info(this, QStringLiteral("应用预设"), QStringLiteral("请先选择一个预设"));
            return;
        }
        emit applyRequested(m_presets->load(name));
    });
    connect(saveButton, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("保存预设"),
                                                   QStringLiteral("预设名称："), QLineEdit::Normal,
                                                   selectedName(), &ok);
        if (ok && !name.trimmed().isEmpty())
            emit saveCurrentRequested(name.trimmed());
    });
    connect(deleteButton, &QPushButton::clicked, this, [this] {
        const QString name = selectedName();
        if (name.isEmpty())
            return;
        if (QMessageBox::question(this, QStringLiteral("删除预设"),
                                  QStringLiteral("确定删除预设“%1”吗？").arg(name)) != QMessageBox::Yes) {
            return;
        }
        QString error;
        if (!m_presets->remove(name, &error))
            ui::warn(this, QStringLiteral("删除失败"), error);
        refresh();
    });
    connect(m_presets, &PresetManager::presetsChanged, this, &PresetsPage::refresh);
    refresh();
}

void PresetsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
}

void PresetsPage::refresh()
{
    const QString current = selectedName();
    m_list->clear();
    for (const Preset &preset : m_presets->presets())
        m_list->addItem(preset.name);
    const QString last = ConfigStore::instance().lastPreset();
    const QString wanted = !current.isEmpty() ? current : last;
    if (!wanted.isEmpty()) {
        const auto items = m_list->findItems(wanted, Qt::MatchExactly);
        if (!items.isEmpty())
            m_list->setCurrentItem(items.first());
    }
    if (!m_list->currentItem() && m_list->count() > 0)
        m_list->setCurrentRow(0);
    updateDetails();
}

void PresetsPage::selectPreset(const QString &name)
{
    const auto items = m_list->findItems(name, Qt::MatchExactly);
    if (!items.isEmpty())
        m_list->setCurrentItem(items.first());
}

QString PresetsPage::selectedName() const
{
    return m_list->currentItem() ? m_list->currentItem()->text() : QString();
}

void PresetsPage::updateDetails()
{
    const QString name = selectedName();
    if (name.isEmpty()) {
        m_details->setText(QStringLiteral("暂无预设。录制前可在本页保存当前配置。"));
        return;
    }
    const Preset preset = m_presets->load(name);
    const QString text = QStringLiteral(
        "名称：%1\n\n"
        "录制模式：%2\n"
        "帧率：%3 fps    编码：%4    码率：%5 Mbps\n"
        "视频：%6\n"
        "音频：系统声音 %7 / 麦克风 %8%9\n"
        "摄像头布局：%10\n"
        "画笔：%11    线宽：%12 px\n"
        "水印：%13\n"
        "RTMP：%14")
        .arg(preset.name)
        .arg(captureModeName(preset.video.mode))
        .arg(preset.video.fps)
        .arg(preset.video.codec.toUpper())
        .arg(preset.video.bitrateKbps / 1000.0, 0, 'f', 1)
        .arg(preset.video.mode == CaptureMode::Region && preset.video.region.isValid()
                 ? QStringLiteral("选区 %1×%2").arg(preset.video.region.width()).arg(preset.video.region.height())
                 : preset.video.mode == CaptureMode::Window
                       ? QStringLiteral("窗口：%1").arg(preset.video.windowTitle)
                       : QStringLiteral("全屏（显示器 %1）").arg(preset.video.monitorIndex + 1))
        .arg(preset.audio.systemEnabled ? QStringLiteral("开") : QStringLiteral("关"))
        .arg(preset.audio.micEnabled ? QStringLiteral("开") : QStringLiteral("关"))
        .arg(preset.audio.noiseSuppression ? QStringLiteral("（降噪）") : QString())
        .arg(preset.overlay.layout == LayoutTemplate::Single ? QStringLiteral("单屏 + 画中画")
              : preset.overlay.layout == LayoutTemplate::Split ? QStringLiteral("二分屏")
                                                               : QStringLiteral("三分屏"))
        .arg(preset.overlay.penColor.name())
        .arg(preset.overlay.penWidth)
        .arg(preset.watermark.enabled ? preset.watermark.text : QStringLiteral("未启用"))
        .arg(preset.stream.enabled ? preset.stream.serverUrl : QStringLiteral("未启用"));
    m_details->setText(text);
}

} // namespace lightrec
