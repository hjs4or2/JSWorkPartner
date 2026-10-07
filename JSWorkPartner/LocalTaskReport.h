#pragma once
#include <QtCore/QList>
#include "Task.h"
#include "WeeklyReportCollector.h"

// Pure transformation: completed local tasks never create next-week plans.
void mergeLocalCompletedTasks(WeeklyReportResult& report, const QList<Task>& tasks,
                              const QDate& start, const QDate& end);
