#include "TaskRepository.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QStandardPaths>
#include <QtCore/QUuid>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>
#include <QtSql/QSqlDriver>
#include <QtCore/QVariant>

TaskRepository::TaskRepository()
    : connectionName_(
          QStringLiteral("task_repository_%1")
              .arg(QUuid::createUuid().toString())),
      database_(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_))
{
}

TaskRepository::~TaskRepository()
{
    if (database_.isOpen())
    {
        database_.close();
    }

    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName_);
}

bool TaskRepository::initialize()
{
    QString dataDirectory =
        QProcessEnvironment::systemEnvironment().value(
            QStringLiteral("JSWORKMANAGER_DATA_DIR"));

    if (dataDirectory.isEmpty())
    {
        dataDirectory =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }

    if (dataDirectory.isEmpty())
    {
        lastError_ = QStringLiteral("로컬 데이터 저장 경로를 찾을 수 없습니다.");
        return false;
    }

    QDir directory;
    if (!directory.mkpath(dataDirectory))
    {
        lastError_ = QStringLiteral("로컬 데이터 폴더를 만들 수 없습니다: %1")
                         .arg(dataDirectory);
        return false;
    }

    databasePath_ = QDir(dataDirectory).filePath(QStringLiteral("work_manager.db"));
    database_.setDatabaseName(databasePath_);

    if (!database_.open())
    {
        setLastDatabaseError(QStringLiteral("SQLite DB를 열 수 없습니다."));
        return false;
    }

    if (!executeSchema(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS tasks ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "sequence INTEGER NOT NULL UNIQUE,"
            "title TEXT NOT NULL,"
            "description TEXT NOT NULL DEFAULT '',"
            "external_url TEXT NULL,"
            "importance INTEGER NOT NULL DEFAULT 1,"
            "deadline TEXT NULL,"
            "status TEXT NOT NULL DEFAULT 'pending' "
            "CHECK(status IN ('pending', 'completed')),"
            "created_at TEXT NOT NULL,"
            "completed_at TEXT NULL"
            ")")))
    {
        return false;
    }

    if (!ensureTitleColumn())
    {
        return false;
    }

    if (!ensureExternalUrlColumn())
    {
        return false;
    }

    return executeSchema(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_tasks_status_sequence "
        "ON tasks(status, sequence)"));
}

QList<Task> TaskRepository::loadTasks(bool completed)
{
    QList<Task> tasks;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, sequence, title, description, external_url, "
        "importance, deadline, status, "
        "created_at, completed_at "
        "FROM tasks WHERE status = :status ORDER BY sequence ASC"));
    query.bindValue(
        QStringLiteral(":status"),
        completed ? QStringLiteral("completed") : QStringLiteral("pending"));

    if (!query.exec())
    {
        lastError_ = QStringLiteral("업무 목록을 불러오지 못했습니다: %1")
                         .arg(query.lastError().text());
        return tasks;
    }

    while (query.next())
    {
        Task task;
        task.id = query.value(0).toLongLong();
        task.sequence = query.value(1).toLongLong();
        task.title = query.value(2).toString();
        task.details = query.value(3).toString();
        task.externalUrl = query.value(4).toString();
        task.importance = query.value(5).toInt();
        task.deadline = query.value(6).toString();
        task.completed = query.value(7).toString() == QStringLiteral("completed");
        task.createdAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODate);
        task.completedAt = QDateTime::fromString(query.value(9).toString(), Qt::ISODate);
        tasks.append(task);
    }

    lastError_.clear();
    return tasks;
}

bool TaskRepository::addTask(
    const QString& title,
    const QString& details,
    const QString& externalUrl,
    int importance,
    const QString& deadline,
    Task* createdTask)
{
    if (!database_.transaction())
    {
        setLastDatabaseError(QStringLiteral("업무 저장을 시작하지 못했습니다."));
        return false;
    }

    QSqlQuery sequenceQuery(database_);
    if (!sequenceQuery.exec(
            QStringLiteral("SELECT COALESCE(MAX(sequence), 0) + 1 FROM tasks")) ||
        !sequenceQuery.next())
    {
        lastError_ = QStringLiteral("접수 순번을 생성하지 못했습니다: %1")
                         .arg(sequenceQuery.lastError().text());
        database_.rollback();
        return false;
    }

    const qint64 sequence = sequenceQuery.value(0).toLongLong();
    const QDateTime createdAt = QDateTime::currentDateTime();

    QSqlQuery insertQuery(database_);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO tasks "
        "(sequence, title, description, external_url, importance, deadline, "
        "status, created_at) "
        "VALUES (:sequence, :title, :description, :external_url, "
        ":importance, :deadline, "
        "'pending', :created_at)"));
    insertQuery.bindValue(QStringLiteral(":sequence"), sequence);
    insertQuery.bindValue(QStringLiteral(":title"), title);
    insertQuery.bindValue(QStringLiteral(":description"), details);
    insertQuery.bindValue(
        QStringLiteral(":external_url"),
        externalUrl.isEmpty()
            ? QVariant(QVariant::String)
            : QVariant(externalUrl));
    insertQuery.bindValue(QStringLiteral(":importance"), importance);
    insertQuery.bindValue(
        QStringLiteral(":deadline"),
        deadline.isEmpty() ? QVariant(QVariant::String) : QVariant(deadline));
    insertQuery.bindValue(
        QStringLiteral(":created_at"),
        createdAt.toString(Qt::ISODate));

    if (!insertQuery.exec())
    {
        lastError_ = QStringLiteral("업무를 저장하지 못했습니다: %1")
                         .arg(insertQuery.lastError().text());
        database_.rollback();
        return false;
    }

    if (!database_.commit())
    {
        setLastDatabaseError(QStringLiteral("업무 저장을 완료하지 못했습니다."));
        database_.rollback();
        return false;
    }

    if (createdTask != nullptr)
    {
        createdTask->id = insertQuery.lastInsertId().toLongLong();
        createdTask->sequence = sequence;
        createdTask->title = title;
        createdTask->details = details;
        createdTask->externalUrl = externalUrl;
        createdTask->importance = importance;
        createdTask->deadline = deadline;
        createdTask->completed = false;
        createdTask->createdAt = createdAt;
    }

    lastError_.clear();
    return true;
}

bool TaskRepository::updateTask(
    qint64 taskId,
    const QString& title,
    const QString& details,
    const QString& externalUrl,
    int importance,
    const QString& deadline)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE tasks SET title = :title, description = :description, "
        "external_url = :external_url, importance = :importance, "
        "deadline = :deadline "
        "WHERE id = :id"));
    query.bindValue(QStringLiteral(":title"), title);
    query.bindValue(QStringLiteral(":description"), details);
    query.bindValue(
        QStringLiteral(":external_url"),
        externalUrl.isEmpty()
            ? QVariant(QVariant::String)
            : QVariant(externalUrl));
    query.bindValue(QStringLiteral(":importance"), importance);
    query.bindValue(
        QStringLiteral(":deadline"),
        deadline.isEmpty() ? QVariant(QVariant::String) : QVariant(deadline));
    query.bindValue(QStringLiteral(":id"), taskId);

    if (!query.exec())
    {
        lastError_ = QStringLiteral("업무 수정을 저장하지 못했습니다: %1")
                         .arg(query.lastError().text());
        return false;
    }

    if (query.numRowsAffected() != 1)
    {
        lastError_ = QStringLiteral("수정할 업무를 찾지 못했습니다.");
        return false;
    }

    lastError_.clear();
    return true;
}

bool TaskRepository::completeTask(qint64 taskId)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE tasks SET status = 'completed', completed_at = :completed_at "
        "WHERE id = :id AND status = 'pending'"));
    query.bindValue(
        QStringLiteral(":completed_at"),
        QDateTime::currentDateTime().toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":id"), taskId);

    if (!query.exec())
    {
        lastError_ = QStringLiteral("완료 상태를 저장하지 못했습니다: %1")
                         .arg(query.lastError().text());
        return false;
    }

    if (query.numRowsAffected() != 1)
    {
        lastError_ = QStringLiteral("완료 처리할 업무를 찾지 못했습니다.");
        return false;
    }

    lastError_.clear();
    return true;
}

bool TaskRepository::deleteTask(qint64 taskId)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM tasks WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), taskId);

    if (!query.exec())
    {
        lastError_ = QStringLiteral("업무를 삭제하지 못했습니다: %1")
                         .arg(query.lastError().text());
        return false;
    }

    if (query.numRowsAffected() != 1)
    {
        lastError_ = QStringLiteral("삭제할 업무를 찾지 못했습니다.");
        return false;
    }

    lastError_.clear();
    return true;
}

bool TaskRepository::titleExists(
    const QString& title,
    qint64 excludedTaskId)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM tasks "
        "WHERE title = :title COLLATE NOCASE AND id <> :excluded_id "
        "LIMIT 1"));
    query.bindValue(QStringLiteral(":title"), title.trimmed());
    query.bindValue(QStringLiteral(":excluded_id"), excludedTaskId);

    if (!query.exec())
    {
        lastError_ = QStringLiteral("업무 제목 중복 여부를 확인하지 못했습니다: %1")
                         .arg(query.lastError().text());
        return true;
    }

    const bool exists = query.next();
    lastError_.clear();
    return exists;
}

QString TaskRepository::databasePath() const
{
    return databasePath_;
}

QString TaskRepository::lastError() const
{
    return lastError_;
}

bool TaskRepository::executeSchema(const QString& sql)
{
    QSqlQuery query(database_);
    if (!query.exec(sql))
    {
        lastError_ = QStringLiteral("DB 구조를 준비하지 못했습니다: %1")
                         .arg(query.lastError().text());
        return false;
    }

    return true;
}

bool TaskRepository::ensureTitleColumn()
{
    QSqlQuery columnQuery(database_);
    if (!columnQuery.exec(QStringLiteral("PRAGMA table_info(tasks)")))
    {
        lastError_ = QStringLiteral("DB 컬럼 정보를 확인하지 못했습니다: %1")
                         .arg(columnQuery.lastError().text());
        return false;
    }

    bool hasTitleColumn = false;
    while (columnQuery.next())
    {
        if (columnQuery.value(1).toString() == QStringLiteral("title"))
        {
            hasTitleColumn = true;
            break;
        }
    }

    if (hasTitleColumn)
    {
        return true;
    }

    if (!executeSchema(QStringLiteral(
            "ALTER TABLE tasks ADD COLUMN title TEXT NOT NULL DEFAULT ''")))
    {
        return false;
    }

    return executeSchema(QStringLiteral(
        "UPDATE tasks SET title = description WHERE title = ''"));
}

bool TaskRepository::ensureExternalUrlColumn()
{
    QSqlQuery columnQuery(database_);
    if (!columnQuery.exec(QStringLiteral("PRAGMA table_info(tasks)")))
    {
        lastError_ = QStringLiteral("DB 컬럼 정보를 확인하지 못했습니다: %1")
                         .arg(columnQuery.lastError().text());
        return false;
    }

    while (columnQuery.next())
    {
        if (columnQuery.value(1).toString() == QStringLiteral("external_url"))
        {
            return true;
        }
    }

    return executeSchema(QStringLiteral(
        "ALTER TABLE tasks ADD COLUMN external_url TEXT NULL"));
}

void TaskRepository::setLastDatabaseError(const QString& context)
{
    lastError_ = QStringLiteral("%1 %2")
                     .arg(context)
                     .arg(database_.lastError().text());
}
