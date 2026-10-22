#pragma once

#include "core/Types.h"

#include <QList>
#include <QObject>
#include <QTimer>

namespace lightrec {

class ScheduleManager : public QObject
{
    Q_OBJECT
public:
    explicit ScheduleManager(QObject *parent = nullptr);

    QList<ScheduleTask> tasks() const { return m_tasks; }
    void setTasks(const QList<ScheduleTask> &tasks);
    void addTask(const ScheduleTask &task);
    void updateTask(const ScheduleTask &task);
    void removeTask(const QString &id);
    void setTaskEnabled(const QString &id, bool enabled);
    void setActiveTask(const QString &id);

    void load();
    void save();

    static QString storagePath();

signals:
    void taskStartRequested(const QString &taskId, const QString &presetName,
                            const QString &taskName);
    void taskStopRequested(const QString &taskId, const QString &taskName);
    void tasksChanged();

private:
    void check();
    static bool withinWindow(const QTime &now, const QTime &start, const QTime &stop);

    QList<ScheduleTask> m_tasks;
    QTimer m_timer;
    QString m_activeTaskId;
};

} // namespace lightrec
