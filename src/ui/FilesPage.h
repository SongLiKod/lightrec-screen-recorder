#pragma once

#include "core/Types.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPoint;
class QTableWidget;

namespace lightrec {

class RecordingLibrary;

class FilesPage : public QWidget
{
    Q_OBJECT
public:
    explicit FilesPage(RecordingLibrary *library, QWidget *parent = nullptr);

    void refresh();
    QString selectedPath() const;
    void selectFile(const QString &path);

signals:
    void repairRequested(const QString &path);
    void batchRepairRequested(const QStringList &paths);
    void batchWatermarkRequested(const QStringList &paths);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void previewSelected();
    void openFolderSelected();
    void renameSelected();
    void deleteSelected();
    void repairSelected();
    void batchRename();
    void batchDelete();
    void batchRepair();
    void batchWatermark();
    void showContextMenu(const QPoint &position);
    void updateSummary();
    void populate();
    QStringList selectedPaths() const;

    RecordingLibrary *m_library = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QList<RecordingFileInfo> m_files;
};

} // namespace lightrec
