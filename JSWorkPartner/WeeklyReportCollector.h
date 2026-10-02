#pragma once

#include "WeeklyReportSettings.h"
#include <functional>
#include <QtCore/QDate>
#include <QtCore/QMap>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QStringList>

class QJsonObject;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QTimer;

struct ReportLine
{
    QString section;
    QString summary;
    QString status;
    QStringList evidence;
};

struct WeeklyReportResult
{
    QVector<ReportLine> current;
    QVector<ReportLine> next;
    QString apiUserName;
    QStringList notices;
    bool cancelled = false;
    bool partial = false;
};

// All I/O uses the Qt event loop; no blocking process/network waits or shell.
class WeeklyReportCollector final : public QObject
{
public:
    WeeklyReportCollector(const WeeklyReportSettings& settings, const QDate& start,
                          const QDate& end, QObject* parent);
    ~WeeklyReportCollector() override;
    std::function<void(const QString&)> progress;
    std::function<void(const WeeklyReportResult&)> completed;
    void start();
    void cancel();

private:
    struct Issue
    {
        int id = 0;
        int projectId = 0;
        QString title;
        QString project;
        QString status;
    };
    struct Change
    {
        ReportWorkspace workspace;
        QString revision;
        QString message;
        QString date;
    };
    void collectWorkspace();
    bool parseGit(const QByteArray& data, QVector<Change>* changes);
    bool parseSvn(const QByteArray& data, QVector<Change>* changes);
    void collectRedmine();
    void resolveProject();
    void collectIssues(bool open, int offset);
    void request(const QString& path, const QStringList& query,
                 std::function<void(const QJsonObject&, const QString&)> callback);
    QString issueSection(const Issue& issue) const;
    void finish(bool cancelled = false);

    WeeklyReportSettings settings_;
    QDate start_;
    QDate end_;
    QNetworkAccessManager* network_;
    QPointer<QNetworkReply> reply_;
    QPointer<QProcess> process_;
    QTimer* timeout_;
    QString apiKey_;
    int workspaceIndex_ = 0;
    int projectIndex_ = 0;
    int userId_ = 0;
    bool running_ = false;
    bool openComplete_ = false;
    bool projectFilter_ = false;
    QStringList projects_;
    QMap<int, QString> projectSections_;
    QMap<int, Issue> issues_;
    QSet<int> openIssues_;
    QVector<Change> changes_;
    WeeklyReportResult result_;
};
