#include "ui/FilesPage.h"
#include "core/AppPaths.h"
#include "ui/PreviewDialog.h"
#include "ui/Widgets.h"
#include "record/RecordingLibrary.h"

#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QAction>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QShowEvent>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace lightrec {

FilesPage::FilesPage(RecordingLibrary *library, QWidget *parent)
    : QWidget(parent)
    , m_library(library)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 24);
    layout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("录制文件"), this);
    title->setObjectName(QStringLiteral("appTitle"));
    layout->addWidget(title);
    layout->addWidget(ui::mutedLabel(
        QStringLiteral("文件名、时长、大小、录制时间一目了然；支持预览、打开目录、重命名、删除与修复。"),
        this));

    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(8);
    auto *refreshButton = ui::secondaryButton(QStringLiteral("刷新"), this);
    auto *previewButton = ui::primaryButton(QStringLiteral("预览"), this);
    auto *folderButton = ui::secondaryButton(QStringLiteral("打开目录"), this);
    auto *renameButton = ui::secondaryButton(QStringLiteral("批量重命名"), this);
    auto *deleteButton = ui::dangerButton(QStringLiteral("批量删除"), this);
    auto *repairButton = ui::secondaryButton(QStringLiteral("批量修复"), this);
    auto *watermarkButton = ui::secondaryButton(QStringLiteral("批量水印"), this);
    toolbar->addWidget(refreshButton);
    toolbar->addWidget(previewButton);
    toolbar->addWidget(folderButton);
    toolbar->addWidget(renameButton);
    toolbar->addWidget(deleteButton);
    toolbar->addWidget(repairButton);
    toolbar->addWidget(watermarkButton);
    toolbar->addStretch(1);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索文件名…"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(220);
    toolbar->addWidget(m_searchEdit);
    layout->addLayout(toolbar);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("文件名"), QStringLiteral("时长"), QStringLiteral("大小"),
        QStringLiteral("录制时间"), QStringLiteral("分辨率"), QStringLiteral("状态")
    });
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 5; ++column)
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->verticalHeader()->setDefaultSectionSize(36);
    layout->addWidget(m_table, 1);

    m_summaryLabel = ui::mutedLabel(QString(), this);
    layout->addWidget(m_summaryLabel);

    connect(refreshButton, &QPushButton::clicked, this, &FilesPage::refresh);
    connect(previewButton, &QPushButton::clicked, this, &FilesPage::previewSelected);
    connect(folderButton, &QPushButton::clicked, this, &FilesPage::openFolderSelected);
    connect(renameButton, &QPushButton::clicked, this, &FilesPage::renameSelected);
    connect(deleteButton, &QPushButton::clicked, this, &FilesPage::deleteSelected);
    connect(repairButton, &QPushButton::clicked, this, &FilesPage::repairSelected);
    connect(watermarkButton, &QPushButton::clicked, this, &FilesPage::batchWatermark);
    connect(m_table, &QTableWidget::doubleClicked, this, [this](const QModelIndex &) {
        previewSelected();
    });
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &FilesPage::showContextMenu);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { populate(); });
    connect(m_library, &RecordingLibrary::changed, this, &FilesPage::refresh);
}

void FilesPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
}

void FilesPage::refresh()
{
    m_files = m_library->scan();
    populate();
}

void FilesPage::populate()
{
    const QString filter = m_searchEdit->text().trimmed();
    m_table->setRowCount(0);
    for (const RecordingFileInfo &info : m_files) {
        if (!filter.isEmpty() && !info.fileName.contains(filter, Qt::CaseInsensitive))
            continue;

        const int row = m_table->rowCount();
        m_table->insertRow(row);

        auto *nameItem = new QTableWidgetItem(info.fileName);
        nameItem->setData(Qt::UserRole, info.path);
        nameItem->setToolTip(info.path);
        m_table->setItem(row, 0, nameItem);

        m_table->setItem(row, 1, new QTableWidgetItem(info.broken ? QStringLiteral("--")
                                                                  : formatDuration(info.durationMs)));
        m_table->setItem(row, 2, new QTableWidgetItem(formatSize(info.sizeBytes)));
        m_table->setItem(row, 3, new QTableWidgetItem(info.created.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        m_table->setItem(row, 4, new QTableWidgetItem(
            info.width > 0 ? QStringLiteral("%1 × %2").arg(info.width).arg(info.height)
                           : QStringLiteral("--")));

        auto *statusItem = new QTableWidgetItem(info.broken ? QStringLiteral("损坏") : QStringLiteral("正常"));
        statusItem->setForeground(info.broken ? QColor(0xE5, 0x48, 0x4D) : QColor(0x2D, 0x8C, 0xF0));
        m_table->setItem(row, 5, statusItem);
    }
    updateSummary();
}

void FilesPage::updateSummary()
{
    qint64 totalSize = 0;
    qint64 totalDuration = 0;
    for (const RecordingFileInfo &info : m_files) {
        totalSize += info.sizeBytes;
        totalDuration += info.durationMs;
    }
    m_summaryLabel->setText(QStringLiteral("共 %1 个录像，总时长 %2，占用 %3")
                                .arg(m_files.size())
                                .arg(formatDuration(totalDuration))
                                .arg(formatSize(totalSize)));
}

QString FilesPage::selectedPath() const
{
    const int row = m_table->currentRow();
    if (row < 0)
        return QString();
    QTableWidgetItem *item = m_table->item(row, 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void FilesPage::selectFile(const QString &path)
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *item = m_table->item(row, 0);
        if (item && item->data(Qt::UserRole).toString() == path) {
            m_table->selectRow(row);
            return;
        }
    }
}

QStringList FilesPage::selectedPaths() const
{
    QStringList paths;
    const auto rows = m_table->selectionModel()
                          ? m_table->selectionModel()->selectedRows()
                          : QModelIndexList();
    for (const QModelIndex &index : rows) {
        QTableWidgetItem *item = m_table->item(index.row(), 0);
        if (item)
            paths.append(item->data(Qt::UserRole).toString());
    }
    if (paths.isEmpty()) {
        const QString path = selectedPath();
        if (!path.isEmpty())
            paths.append(path);
    }
    return paths;
}

void FilesPage::showContextMenu(const QPoint &position)
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;

    QMenu menu(this);
    QAction *preview = menu.addAction(QStringLiteral("预览"));
    QAction *openFolder = menu.addAction(QStringLiteral("打开保存目录"));
    menu.addSeparator();
    QAction *rename = menu.addAction(QStringLiteral("批量重命名…"));
    QAction *remove = menu.addAction(QStringLiteral("批量删除…"));
    QAction *repair = menu.addAction(QStringLiteral("批量修复"));
    QAction *watermark = menu.addAction(QStringLiteral("批量添加水印…"));

    QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(position));
    if (!chosen)
        return;
    if (chosen == preview)
        previewSelected();
    else if (chosen == openFolder)
        openFolderSelected();
    else if (chosen == rename)
        batchRename();
    else if (chosen == remove)
        batchDelete();
    else if (chosen == repair)
        batchRepair();
    else if (chosen == watermark)
        batchWatermark();
}

void FilesPage::previewSelected()
{
    const QString path = selectedPath();
    if (path.isEmpty()) {
        ui::info(this, QStringLiteral("预览"), QStringLiteral("请先选择一个录像文件"));
        return;
    }
    PreviewDialog dialog(path, this);
    dialog.exec();
}

void FilesPage::openFolderSelected()
{
    QString path = selectedPath();
    if (path.isEmpty())
        path = m_library->outputDir();
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            { QStringLiteral("/select,"), QDir::toNativeSeparators(path) });
}

void FilesPage::renameSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.size() > 1) {
        batchRename();
        return;
    }
    const QString path = paths.value(0);
    if (path.isEmpty())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("重命名录像"),
                                               QStringLiteral("新的文件名："), QLineEdit::Normal,
                                               m_library->suggestedName(path), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    QString newPath;
    QString error;
    if (!m_library->renameRecording(path, name, &newPath, &error))
        ui::warn(this, QStringLiteral("重命名失败"), error);
    else
        refresh();
}

void FilesPage::batchRename()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) {
        ui::info(this, QStringLiteral("批量重命名"), QStringLiteral("请先选择录像文件"));
        return;
    }
    bool ok = false;
    const QString prefix = QInputDialog::getText(
        this, QStringLiteral("批量重命名"),
        QStringLiteral("输入新文件名前缀，将生成“前缀_001.mp4”格式："),
        QLineEdit::Normal, QStringLiteral("录像"), &ok);
    if (!ok || prefix.trimmed().isEmpty())
        return;

    int renamed = 0;
    QStringList failures;
    const QString safePrefix = AppPaths::sanitizeFileName(prefix.trimmed());
    for (int i = 0; i < paths.size(); ++i) {
        const QString name = QStringLiteral("%1_%2").arg(safePrefix)
                                 .arg(i + 1, 3, 10, QLatin1Char('0'));
        QString newPath;
        QString error;
        if (m_library->renameRecording(paths.at(i), name, &newPath, &error))
            ++renamed;
        else
            failures.append(QStringLiteral("%1：%2").arg(QFileInfo(paths.at(i)).fileName(), error));
    }
    refresh();
    if (!failures.isEmpty())
        ui::warn(this, QStringLiteral("部分文件重命名失败"),
                 QStringLiteral("成功 %1 个，失败 %2 个\n%3")
                     .arg(renamed).arg(failures.size()).arg(failures.join(QLatin1Char('\n'))));
}

void FilesPage::deleteSelected()
{
    batchDelete();
}

void FilesPage::batchDelete()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    const auto result = QMessageBox::question(
        this, QStringLiteral("批量删除录像"),
        paths.size() == 1
            ? QStringLiteral("确定要删除“%1”吗？此操作不可撤销。").arg(QFileInfo(paths.first()).fileName())
            : QStringLiteral("确定要删除选中的 %1 个录像吗？此操作不可撤销。").arg(paths.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (result != QMessageBox::Yes)
        return;

    int deleted = 0;
    QStringList failures;
    for (const QString &path : paths) {
        QString error;
        if (m_library->deleteRecording(path, &error))
            ++deleted;
        else
            failures.append(QStringLiteral("%1：%2").arg(QFileInfo(path).fileName(), error));
    }
    refresh();
    if (!failures.isEmpty())
        ui::warn(this, QStringLiteral("部分文件删除失败"),
                 QStringLiteral("成功 %1 个，失败 %2 个\n%3")
                     .arg(deleted).arg(failures.size()).arg(failures.join(QLatin1Char('\n'))));
}

void FilesPage::repairSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) {
        ui::info(this, QStringLiteral("视频修复"), QStringLiteral("请先选择录像文件"));
        return;
    }
    if (paths.size() > 1) {
        batchRepair();
        return;
    }
    emit repairRequested(paths.first());
}

void FilesPage::batchRepair()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) {
        ui::info(this, QStringLiteral("批量修复"), QStringLiteral("请先选择录像文件"));
        return;
    }
    emit batchRepairRequested(paths);
}

void FilesPage::batchWatermark()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) {
        ui::info(this, QStringLiteral("批量水印"), QStringLiteral("请先选择录像文件"));
        return;
    }
    emit batchWatermarkRequested(paths);
}

} // namespace lightrec
