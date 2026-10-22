#pragma once

#include "core/Types.h"

#include <QWidget>

class QTableWidget;

namespace lightrec {

class ScheduleManager;
class PresetManager;

class TasksPage : public QWidget
{
    Q_OBJECT
public:
    explicit TasksPage(ScheduleManager *schedule, PresetManager *presets, QWidget *parent = nullptr);

    void refresh();

signals:
    void tasksUpdated();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void addTask();
    void editTask();
    void deleteTask();
    QString selectedTaskId() const;
    ScheduleTask taskFromDialog(const ScheduleTask &base);

    ScheduleManager *m_schedule = nullptr;
    PresetManager *m_presets = nullptr;
    QTableWidget *m_table = nullptr;
};

} // namespace lightrec
