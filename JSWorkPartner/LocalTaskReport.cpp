#include "LocalTaskReport.h"
#include <QtCore/QSet>

void mergeLocalCompletedTasks(WeeklyReportResult& report, const QList<Task>& tasks,
                              const QDate& start, const QDate& end)
{
    QSet<QString> ids;
    for (const auto& line : report.current)
        for (const auto& evidence : line.evidence)
            if (evidence.startsWith(QStringLiteral("local-task:"))) ids.insert(evidence);
    for (const auto& task : tasks)
    {
        const QDate day = task.completedAt.toLocalTime().date();
        if (!task.completed || task.id <= 0 || !day.isValid() || day < start || day > end) continue;
        const QString id = QStringLiteral("local-task:%1").arg(task.id);
        if (ids.contains(id)) continue;
        ids.insert(id);
        report.current.append({QStringLiteral("로컬 업무"), task.title, QStringLiteral("완료"),
            {id, QStringLiteral("완료시각: %1").arg(task.completedAt.toLocalTime().toString(Qt::ISODate))}});
    }
}
