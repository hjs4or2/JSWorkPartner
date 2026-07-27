#pragma once

#include <QtCore/QList>
#include <QtCore/QString>
#include <QtSql/QSqlDatabase>

#include "Task.h"

class TaskRepository
{
public:
    TaskRepository();
    ~TaskRepository();

    TaskRepository(const TaskRepository&) = delete;
    TaskRepository& operator=(const TaskRepository&) = delete;

    bool initialize();
    QList<Task> loadTasks(bool completed);
    bool addTask(
        const QString& title,
        const QString& details,
        const QString& externalUrl,
        int importance,
        const QString& deadline,
        Task* createdTask);
    bool updateTask(
        qint64 taskId,
        const QString& title,
        const QString& details,
        const QString& externalUrl,
        int importance,
        const QString& deadline);
    bool deleteTask(qint64 taskId);
    bool completeTask(qint64 taskId);
    bool titleExists(const QString& title, qint64 excludedTaskId = 0);

    QString databasePath() const;
    QString lastError() const;

private:
    bool executeSchema(const QString& sql);
    bool ensureTitleColumn();
    bool ensureExternalUrlColumn();
    void setLastDatabaseError(const QString& context);

    QString connectionName_;
    QString databasePath_;
    QString lastError_;
    QSqlDatabase database_;
};
