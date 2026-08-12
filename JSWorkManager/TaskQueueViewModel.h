#pragma once

#include <QtCore/QAbstractTableModel>
#include <QtCore/QList>
#include <QtCore/QString>

#include "Task.h"

class TaskRepository;

class TaskQueueViewModel final : public QAbstractTableModel
{
public:
    enum Column
    {
        SequenceColumn = 0,
        TitleColumn,
        DetailsColumn,
        ImportanceColumn,
        DeadlineColumn,
        DateColumn,
        ColumnCount
    };

    explicit TaskQueueViewModel(
        TaskRepository& repository,
        QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(
        const QModelIndex& index,
        int role = Qt::DisplayRole) const override;
    QVariant headerData(
        int section,
        Qt::Orientation orientation,
        int role = Qt::DisplayRole) const override;
    void sort(
        int column,
        Qt::SortOrder order = Qt::AscendingOrder) override;

    bool loadPendingTasks();
    bool loadCompletedTasks();
    bool addTask(
        const QString& title,
        const QString& details,
        const QString& externalUrl,
        int importance,
        const QString& deadline);
    bool updateTask(
        int row,
        const QString& title,
        const QString& details,
        const QString& externalUrl,
        int importance,
        const QString& deadline);
    bool deleteTask(int row);
    bool completeTask(int row, Task* transitionedTask = nullptr);
    bool reopenTask(int row, Task* transitionedTask = nullptr);
    Task taskAt(int row) const;
    bool insertTaskSnapshot(const Task& task);
    int rowForTaskId(qint64 taskId) const;

    bool isShowingCompleted() const;
    int taskCount() const;
    QString lastMessage() const;
    QString lastError() const;
    QString databasePath() const;
    void refreshDeadlineIndicators();

private:
    bool reload(bool showCompleted);

    TaskRepository& repository_;
    QList<Task> tasks_;
    bool showingCompleted_ = false;
    int sortColumn_ = -1;
    Qt::SortOrder sortOrder_ = Qt::AscendingOrder;
    QString lastMessage_;
    QString lastError_;
};
