#include "schedule/ScheduleManager.h"
#include "core/AppPaths.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace lightrec {

ScheduleManager::ScheduleManager(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &ScheduleManager::check);
    m_timer.start();
    load();
}

QString ScheduleManager::storagePath()
{
    return AppPaths::configDir() + QStringLiteral("/schedule.json");
}

void ScheduleManager::setTasks(const QList<ScheduleTask> &tasks)
{
    m_tasks = tasks;
    save();
    emit tasksChanged();
}

void ScheduleManager::addTask(const ScheduleTask &task)
{
    ScheduleTask copy = task;
    if (copy.id.isEmpty())
        copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_tasks.append(copy);
    save();
    emit tasksChanged();
}

void ScheduleManager::updateTask(const ScheduleTask &task)
{
    for (ScheduleTask &existing : m_tasks) {
        if (existing.id == task.id) {
            existing = task;
            save();
            emit tasksChanged();
            return;
        }
    }
}

void ScheduleManager::removeTask(const QString &id)
{
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks.at(i).id == id) {
            m_tasks.removeAt(i);
            if (m_activeTaskId == id)
                m_activeTaskId.clear();
            save();
            emit tasksChanged();
            return;
        }
    }
}

void ScheduleManager::setTaskEnabled(const QString &id, bool enabled)
{
    for (ScheduleTask &task : m_tasks) {
        if (task.id == id) {
            task.enabled = enabled;
            save();
            emit tasksChanged();
            return;
        }
    }
}

void ScheduleManager::setActiveTask(const QString &id)
{
    m_activeTaskId = id;
}

void ScheduleManager::load()
{
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();

    const QJsonArray array = document.object().value(QStringLiteral("tasks")).toArray();
    m_tasks.clear();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        ScheduleTask task;
        task.id = object.value(QStringLiteral("id")).toString();
        task.name = object.value(QStringLiteral("name")).toString();
        task.startTime = QTime::fromString(object.value(QStringLiteral("start")).toString(), QStringLiteral("HH:mm"));
        task.stopTime = QTime::fromString(object.value(QStringLiteral("stop")).toString(), QStringLiteral("HH:mm"));
        task.presetName = object.value(QStringLiteral("preset")).toString();
        task.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        task.autoStop = object.value(QStringLiteral("autoStop")).toBool(true);
        task.lastRunDate = object.value(QStringLiteral("lastRunDate")).toString();
        if (!task.startTime.isValid())
            task.startTime = QTime(9, 0);
        if (!task.stopTime.isValid())
            task.stopTime = QTime(10, 0);
        m_tasks.append(task);
    }
}

void ScheduleManager::save()
{
    AppPaths::ensureDirectories();
    QJsonArray array;
    for (const ScheduleTask &task : m_tasks) {
        QJsonObject object;
        object[QStringLiteral("id")] = task.id;
        object[QStringLiteral("name")] = task.name;
        object[QStringLiteral("start")] = task.startTime.toString(QStringLiteral("HH:mm"));
        object[QStringLiteral("stop")] = task.stopTime.toString(QStringLiteral("HH:mm"));
        object[QStringLiteral("preset")] = task.presetName;
        object[QStringLiteral("enabled")] = task.enabled;
        object[QStringLiteral("autoStop")] = task.autoStop;
        object[QStringLiteral("lastRunDate")] = task.lastRunDate;
        array.append(object);
    }

    QJsonObject root;
    root[QStringLiteral("tasks")] = array;

    QSaveFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}

bool ScheduleManager::withinWindow(const QTime &now, const QTime &start, const QTime &stop)
{
    if (stop > start)
        return now >= start && now < stop;
    if (stop < start)
        return now >= start || now < stop;
    return false;
}

void ScheduleManager::check()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QTime time = now.time();
    const QString today = now.date().toString(Qt::ISODate);

    for (ScheduleTask &task : m_tasks) {
        if (!task.enabled)
            continue;

        if (!m_activeTaskId.isEmpty()) {
            if (task.id == m_activeTaskId && task.autoStop
                && !withinWindow(time, task.startTime, task.stopTime)) {
                const QString id = task.id;
                const QString name = task.name;
                m_activeTaskId.clear();
                emit taskStopRequested(id, name);
            }
            continue;
        }

        if (task.lastRunDate == today)
            continue;
        if (!withinWindow(time, task.startTime, task.stopTime))
            continue;

        task.lastRunDate = today;
        save();
        emit taskStartRequested(task.id, task.presetName, task.name);
        break;
    }
}

} // namespace lightrec
