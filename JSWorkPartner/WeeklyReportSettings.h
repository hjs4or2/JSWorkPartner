#pragma once

#include <QtCore/QString>
#include <QtCore/QVector>

class QWidget;

struct ReportWorkspace
{
    QString section;
    QString vcs;
    QString path;
    QString author;
    QString redmineProject;
};

struct WeeklyReportSettings
{
    QString redmineUrl;
    QString displayName;
    QVector<ReportWorkspace> workspaces;

    static WeeklyReportSettings load();
    // Secrets are scoped to the server and Windows user, never stored as plaintext.
    static QString apiKey(const QString& server, QString* error);
    static bool edit(QWidget* parent);
};
