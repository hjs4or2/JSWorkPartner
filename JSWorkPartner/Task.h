#pragma once

#include <QtCore/QDateTime>
#include <QtCore/QString>

struct Task
{
    qint64 id = 0;
    qint64 sequence = 0;
    QString title;
    QString details;
    QString externalUrl;
    int importance = 1;
    QString deadline;
    bool completed = false;
    QDateTime createdAt;
    QDateTime completedAt;
};
