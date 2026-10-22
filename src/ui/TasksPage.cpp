#include "ui/TasksPage.h"

#include "preset/PresetManager.h"
#include "schedule/ScheduleManager.h"
#include "ui/Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimeEdit>
#include <QUuid>
#include <QVBoxLayout>

namespace lightrec {

TasksPage::TasksPage(ScheduleManager *schedule, PresetManager *presets, QWidget *parent)
    : QWidget(parent)
    , m_schedule(schedule)
    , m_presets(presets)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 24);
    layout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("定时录制"), this);
    title->setObjectName(QStringLiteral("appTitle"));
    layout->addWidget(title);
    layout->addWidget(ui::mutedLabel(
        QStringLiteral("设置开始/停止时间与预设，软件运行期间将自动启停录制；任务会持久化保存。"),
        this));

    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(8);
    auto *addButton = ui::primaryButton(QStringLiteral("新增任务"), this);
    auto *editButton = ui::secondaryButton(QStringLiteral("编辑"), this);
    auto *deleteButton = ui::dangerButton(QStringLiteral("删除"), this);
    toolbar->addWidget(addButton);
    toolbar->addWidget(editButton);
    toolbar->addWidget(deleteButton);
    toolbar->addStretch(1);
    layout->addLayout(toolbar);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("任务名称"), QStringLiteral("开始时间"), QStringLiteral("停止时间"),
        QStringLiteral("预设"), QStringLiteral("启用")
    });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 5; ++i)
        m_table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(38);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    layout->addWidget(m_table, 1);

    auto *hint = ui::mutedLabel(
        QStringLiteral("提示：任务在设定时间窗口内启动一次；若勾选“自动停止”，到达停止时间会结束录制。"
                       "关闭主窗口后定时任务不会运行。"), this);
    layout->addWidget(hint);

    connect(addButton, &QPushButton::clicked, this, &TasksPage::addTask);
    connect(editButton, &QPushButton::clicked, this, &TasksPage::editTask);
    connect(deleteButton, &QPushButton::clicked, this, &TasksPage::deleteTask);
    connect(m_table, &QTableWidget::doubleClicked, this, [this](const QModelIndex &) { editTask(); });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (!item || item->column() != 4)
            return;
        QTableWidgetItem *nameItem = m_table->item(item->row(), 0);
        if (!nameItem)
            return;
        m_schedule->setTaskEnabled(nameItem->data(Qt::UserRole).toString(),
                                   item->checkState() == Qt::Checked);
    });
    connect(m_schedule, &ScheduleManager::tasksChanged, this, &TasksPage::refresh);
    refresh();
}

void TasksPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
}

void TasksPage::refresh()
{
    const QSignalBlocker blocker(m_table);
    m_table->setRowCount(0);
    for (const ScheduleTask &task : m_schedule->tasks()) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        auto *nameItem = new QTableWidgetItem(task.name.isEmpty() ? QStringLiteral("未命名任务")
                                                                  : task.name);
        nameItem->setData(Qt::UserRole, task.id);
        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, new QTableWidgetItem(task.startTime.toString(QStringLiteral("HH:mm"))));
        m_table->setItem(row, 2, new QTableWidgetItem(task.stopTime.toString(QStringLiteral("HH:mm"))));
        m_table->setItem(row, 3, new QTableWidgetItem(task.presetName.isEmpty()
                                                          ? QStringLiteral("默认预设")
                                                          : task.presetName));

        auto *enabledItem = new QTableWidgetItem();
        enabledItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        enabledItem->setCheckState(task.enabled ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(row, 4, enabledItem);
    }
}

QString TasksPage::selectedTaskId() const
{
    const int row = m_table->currentRow();
    if (row < 0)
        return QString();
    QTableWidgetItem *item = m_table->item(row, 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

ScheduleTask TasksPage::taskFromDialog(const ScheduleTask &base)
{
    QDialog dialog(this);
    dialog.setWindowTitle(base.id.isEmpty() ? QStringLiteral("新增定时任务")
                                            : QStringLiteral("编辑定时任务"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();
    form->setSpacing(10);

    auto *nameEdit = new QLineEdit(&dialog);
    nameEdit->setText(base.name.isEmpty() ? QStringLiteral("会议录制") : base.name);
    form->addRow(QStringLiteral("任务名称"), nameEdit);

    auto *startEdit = new QTimeEdit(&dialog);
    startEdit->setDisplayFormat(QStringLiteral("HH:mm"));
    startEdit->setTime(base.startTime);
    form->addRow(QStringLiteral("开始时间"), startEdit);

    auto *stopEdit = new QTimeEdit(&dialog);
    stopEdit->setDisplayFormat(QStringLiteral("HH:mm"));
    stopEdit->setTime(base.stopTime);
    form->addRow(QStringLiteral("停止时间"), stopEdit);

    auto *presetCombo = new QComboBox(&dialog);
    const QStringList names = m_presets->names();
    presetCombo->addItems(names);
    int index = presetCombo->findText(base.presetName);
    if (index < 0 && !names.isEmpty())
        index = 0;
    if (index >= 0)
        presetCombo->setCurrentIndex(index);
    form->addRow(QStringLiteral("使用预设"), presetCombo);

    auto *autoStopCheck = new QCheckBox(QStringLiteral("到达停止时间自动结束"), &dialog);
    autoStopCheck->setChecked(base.autoStop);
    form->addRow(QString(), autoStopCheck);

    auto *enabledCheck = new QCheckBox(QStringLiteral("启用此任务"), &dialog);
    enabledCheck->setChecked(base.enabled);
    form->addRow(QString(), enabledCheck);

    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return ScheduleTask();

    ScheduleTask task = base;
    if (task.id.isEmpty())
        task.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task.name = nameEdit->text().trimmed();
    if (task.name.isEmpty())
        task.name = QStringLiteral("定时任务");
    task.startTime = startEdit->time();
    task.stopTime = stopEdit->time();
    task.presetName = presetCombo->currentText();
    task.autoStop = autoStopCheck->isChecked();
    task.enabled = enabledCheck->isChecked();
    return task;
}

void TasksPage::addTask()
{
    ScheduleTask base;
    base.startTime = QTime(9, 0);
    base.stopTime = QTime(10, 0);
    base.enabled = true;
    base.autoStop = true;
    if (!m_presets->names().isEmpty())
        base.presetName = m_presets->names().first();
    const ScheduleTask task = taskFromDialog(base);
    if (task.id.isEmpty())
        return;
    m_schedule->addTask(task);
    refresh();
    emit tasksUpdated();
}

void TasksPage::editTask()
{
    const QString id = selectedTaskId();
    if (id.isEmpty()) {
        ui::info(this, QStringLiteral("编辑任务"), QStringLiteral("请先选择一个任务"));
        return;
    }
    ScheduleTask base;
    for (const ScheduleTask &task : m_schedule->tasks()) {
        if (task.id == id) {
            base = task;
            break;
        }
    }
    const ScheduleTask updated = taskFromDialog(base);
    if (updated.id.isEmpty())
        return;
    m_schedule->updateTask(updated);
    refresh();
    emit tasksUpdated();
}

void TasksPage::deleteTask()
{
    const QString id = selectedTaskId();
    if (id.isEmpty())
        return;
    if (QMessageBox::question(this, QStringLiteral("删除任务"),
                              QStringLiteral("确定删除选中的定时任务吗？")) != QMessageBox::Yes) {
        return;
    }
    m_schedule->removeTask(id);
    refresh();
    emit tasksUpdated();
}

} // namespace lightrec
