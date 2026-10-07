#include "ExternalTaskWatcher.h"
#include <QtCore/QVariant>
#include <QtCore/QUuid>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

ExternalTaskWatcher::ExternalTaskWatcher(const QString& path, QObject* parent)
    : QThread(parent), databasePath_(path) {}

ExternalTaskWatcher::~ExternalTaskWatcher() { stop(); }

void ExternalTaskWatcher::start()
{
    if (isRunning()) return;
    stopping_ = false;
    requestRefresh();
    QThread::start();
}

void ExternalTaskWatcher::stop()
{
    stopping_ = true;
    wake_.wakeAll();
    wait();
}

void ExternalTaskWatcher::requestRefresh()
{
    QMutexLocker lock(&mailboxMutex_);
    ++generation_;
    available_ = false; // A snapshot captured before a GUI write must not undo it.
    wake_.wakeAll();
}

bool ExternalTaskWatcher::takeSnapshot(Snapshot* snapshot)
{
    QMutexLocker lock(&mailboxMutex_);
    if (!available_ || mailbox_.generation != generation_.load()) return false;
    *snapshot = mailbox_;
    available_ = false;
    return true;
}

void ExternalTaskWatcher::run()
{
    const QString connection = QStringLiteral("external_tasks_%1").arg(QUuid::createUuid().toString());
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(databasePath_);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=150"));
        qint64 lastVersion = -1;
        quint64 lastGeneration = 0;
        QString lastFailure;
        while (!stopping_)
        {
            const auto generation = generation_.load();
            Snapshot snapshot;
            snapshot.generation = generation;
            bool publish = false;
            bool ok = db.isOpen() || db.open();
            qint64 version = -1;
            if (ok)
            {
                QSqlQuery query(db);
                ok = query.exec(QStringLiteral("PRAGMA busy_timeout=150")) &&
                     query.exec(QStringLiteral("BEGIN"));
                if (ok)
                {
                    ok = query.exec(QStringLiteral("PRAGMA data_version")) && query.next();
                    if (ok) version = query.value(0).toLongLong();
                    query.finish();
                    if (ok && (version != lastVersion || generation != lastGeneration || !lastFailure.isEmpty()))
                    {
                        ok = query.exec(QStringLiteral(
                            "SELECT id, sequence, title, description, external_url, importance, deadline, status, "
                            "created_at, completed_at FROM tasks ORDER BY sequence ASC"));
                        while (ok && query.next() && !stopping_)
                        {
                            Task task;
                            task.id = query.value(0).toLongLong(); task.sequence = query.value(1).toLongLong();
                            task.title = query.value(2).toString(); task.details = query.value(3).toString();
                            task.externalUrl = query.value(4).toString(); task.importance = query.value(5).toInt();
                            task.deadline = query.value(6).toString(); task.completed = query.value(7).toString() == QStringLiteral("completed");
                            task.createdAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODate);
                            task.completedAt = QDateTime::fromString(query.value(9).toString(), Qt::ISODate);
                            (task.completed ? snapshot.completed : snapshot.pending).append(task);
                        }
                        ok = ok && !query.lastError().isValid() && !stopping_;
                        publish = ok;
                    }
                    query.finish();
                    if (!query.exec(ok ? QStringLiteral("COMMIT") : QStringLiteral("ROLLBACK")))
                    { ok = false; db.rollback(); }
                }
            }
            if (!ok && !stopping_)
            {
                snapshot.error = QStringLiteral("업무 목록을 갱신하지 못했습니다. 기존 내용을 유지하며 다시 시도합니다.");
                publish = lastFailure.isEmpty() || generation != lastGeneration;
                lastFailure = snapshot.error;
            }
            else if (ok && publish) lastFailure.clear();
            if (publish && !stopping_)
            {
                QMutexLocker lock(&mailboxMutex_);
                if (generation == generation_.load())
                {
                    mailbox_ = snapshot; available_ = true;
                    if (ok) { lastVersion = version; lastGeneration = generation; }
                }
            }
            QMutexLocker lock(&waitMutex_);
            if (!stopping_) wake_.wait(&waitMutex_, 500);
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
}
