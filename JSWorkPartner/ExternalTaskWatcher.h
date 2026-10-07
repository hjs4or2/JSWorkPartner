#pragma once

#include <atomic>
#include <QtCore/QMutex>
#include <QtCore/QList>
#include <QtCore/QThread>
#include <QtCore/QWaitCondition>
#include "Task.h"

// The SQL connection lives entirely in run(). The GUI only takes a mailbox copy.
class ExternalTaskWatcher final : public QThread
{
public:
    struct Snapshot
    {
        QList<Task> pending;
        QList<Task> completed;
        QString error;
        quint64 generation = 0;
    };
    explicit ExternalTaskWatcher(const QString& databasePath, QObject* parent = nullptr);
    ~ExternalTaskWatcher() override;
    void start();
    void requestRefresh();
    bool takeSnapshot(Snapshot* snapshot);
    void stop();
protected:
    void run() override;
private:
    const QString databasePath_;
    std::atomic<bool> stopping_{false};
    std::atomic<quint64> generation_{1};
    QMutex mailboxMutex_;
    Snapshot mailbox_;
    bool available_ = false;
    QMutex waitMutex_;
    QWaitCondition wake_;
};
