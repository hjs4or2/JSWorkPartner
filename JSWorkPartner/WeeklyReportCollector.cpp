#include "WeeklyReportCollector.h"

#include <algorithm>
#include <memory>

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QXmlStreamReader>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QSslSocket>

namespace
{
const int MaxOutput = 16 * 1024 * 1024;
const int MaxIssues = 10000;

QDate localDate(const QString& text)
{
    return QDateTime::fromString(text, Qt::ISODate).toLocalTime().date();
}

QString flatMessage(const QString& message)
{
    // Preserve every word for unlinked work; only collapse formatting whitespace.
    return message.simplified();
}
}

WeeklyReportCollector::WeeklyReportCollector(const WeeklyReportSettings& settings,
    const QDate& start, const QDate& end, QObject* parent)
    : QObject(parent), settings_(settings), start_(start), end_(end),
      network_(new QNetworkAccessManager(this)), timeout_(new QTimer(this))
{
    timeout_->setSingleShot(true);
}

WeeklyReportCollector::~WeeklyReportCollector()
{
    running_ = false;
    if (reply_)
    {
        reply_->disconnect(this);
        reply_->abort();
    }
    if (process_)
    {
        process_->disconnect(this);
        process_->kill();
    }
}

void WeeklyReportCollector::start()
{
    if (running_) return;
    running_ = true;
    QTimer::singleShot(0, this, [this]() { collectWorkspace(); });
}

void WeeklyReportCollector::cancel()
{
    if (running_) finish(true);
}

void WeeklyReportCollector::collectWorkspace()
{
    if (!running_) return;
    if (workspaceIndex_ >= settings_.workspaces.size())
    {
        collectRedmine();
        return;
    }
    const ReportWorkspace w = settings_.workspaces.at(workspaceIndex_);
    if (progress) progress(QStringLiteral("%1/%2: %3 (%4) 이력 수집 중")
        .arg(workspaceIndex_ + 1).arg(settings_.workspaces.size()).arg(w.section, w.vcs));
    if (w.author.isEmpty() || w.section.isEmpty() || !QDir::isAbsolutePath(w.path) || !QDir(w.path).exists() ||
        (w.vcs != QStringLiteral("Git") && w.vcs != QStringLiteral("SVN")))
    {
        result_.partial = true;
        result_.notices.append(QStringLiteral("%1: workspace 설정 또는 폴더가 잘못되어 수집하지 못했습니다.").arg(w.section));
        ++workspaceIndex_;
        QTimer::singleShot(0, this, [this]() { collectWorkspace(); });
        return;
    }
    auto* process = new QProcess(this);
    process_ = process;
    process->setWorkingDirectory(w.path);
    // On Windows command-line tools must be executables, not .bat shell wrappers.
    const QString program = w.vcs == QStringLiteral("Git") ? QStringLiteral("git.exe") : QStringLiteral("svn.exe");
    QStringList arguments;
    if (w.vcs == QStringLiteral("Git"))
    {
        arguments = QStringList{QStringLiteral("-c"), QStringLiteral("log.showSignature=false"),
            QStringLiteral("log"), QStringLiteral("--no-color"), QStringLiteral("--encoding=UTF-8"),
            QStringLiteral("--since-as-filter=") + QDateTime(start_, QTime(0, 0)).toString(Qt::ISODate),
            QStringLiteral("--until=") + QDateTime(end_.addDays(1), QTime(0, 0)).toString(Qt::ISODate),
            QStringLiteral("--format=%H%x1f%an%x1f%ae%x1f%cI%x1f%B%x1e"), QStringLiteral("HEAD"), QStringLiteral("--")};
    }
    else
    {
        const QString lower = QDateTime(start_, QTime(0, 0)).toUTC().toString(Qt::ISODate);
        const QString upper = QDateTime(end_.addDays(1), QTime(0, 0)).toUTC().toString(Qt::ISODate);
        arguments = QStringList{QStringLiteral("log"), QStringLiteral("--xml"), QStringLiteral("--non-interactive"),
            QStringLiteral("-r"), QStringLiteral("{%1}:{%2}").arg(lower, upper),
            QStringLiteral("--"), QDir::toNativeSeparators(w.path) + QLatin1Char('@')};
    }
    auto output = std::make_shared<QByteArray>();
    auto failure = std::make_shared<QString>();
    auto handled = std::make_shared<bool>(false);
    const auto consume = [process, output, failure]() {
        output->append(process->readAllStandardOutput());
        process->readAllStandardError(); // Never expose raw tool output or credential prompts.
        if (output->size() > MaxOutput && failure->isEmpty())
        {
            *failure = QStringLiteral("이력 출력이 16 MiB를 초과했습니다. 기간을 줄여 다시 수집하세요.");
            process->kill();
        }
    };
    connect(process, &QProcess::readyReadStandardOutput, this, consume);
    connect(process, &QProcess::readyReadStandardError, this, consume);
    const auto advance = [this, process, w, output, failure, handled, consume](int exitCode, QProcess::ExitStatus status) {
        if (*handled || !running_) return;
        *handled = true;
        timeout_->stop();
        timeout_->disconnect(this);
        consume();
        QVector<Change> parsed;
        if (failure->isEmpty() && (exitCode != 0 || status != QProcess::NormalExit))
        {
            *failure = QStringLiteral("명령 실행 실패 (종료 코드 %1). PATH, 저장소 접근 권한 및 SVN 인증 캐시를 확인하세요.").arg(exitCode);
            if (w.vcs == QStringLiteral("Git"))
                *failure += QStringLiteral(" 날짜 역전 이력의 누락을 방지하려면 Git 2.37 이상이 필요합니다 (--since-as-filter).");
        }
        if (failure->isEmpty())
        {
            const bool ok = w.vcs == QStringLiteral("Git") ? parseGit(*output, &parsed) : parseSvn(*output, &parsed);
            if (!ok) *failure = QStringLiteral("이력 응답을 해석하지 못했습니다. 해당 폴더의 결과는 반영하지 않았습니다.");
        }
        if (!failure->isEmpty())
        {
            result_.partial = true;
            result_.notices.append(QStringLiteral("%1 [%2]: %3").arg(w.section, w.path, *failure));
        }
        else
        {
            changes_ += parsed;
            result_.notices.append(QStringLiteral("%1 [%2]: 기간·작성자에 해당하는 이력 %3건 수집.").arg(w.section, w.path).arg(parsed.size()));
        }
        process_ = nullptr;
        process->deleteLater();
        ++workspaceIndex_;
        QTimer::singleShot(0, this, [this]() { collectWorkspace(); });
    };
    connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, advance);
    connect(process, &QProcess::errorOccurred, this, [advance, failure](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
        {
            *failure = QStringLiteral("git.exe/svn.exe를 시작하지 못했습니다. 설치 및 PATH를 확인하세요.");
            advance(-1, QProcess::CrashExit);
        }
    });
    connect(timeout_, &QTimer::timeout, this, [process, failure]() {
        *failure = QStringLiteral("60초 제한 시간 초과. 기간 또는 저장소 연결을 확인하세요.");
        process->kill();
    });
    timeout_->start(60000);
    process->start(program, arguments, QIODevice::ReadOnly);
}

bool WeeklyReportCollector::parseGit(const QByteArray& data, QVector<Change>* changes)
{
    const auto& w = settings_.workspaces.at(workspaceIndex_);
    for (const QByteArray& record : data.split('\x1e'))
    {
        if (record.trimmed().isEmpty()) continue;
        const auto fields = record.trimmed().split('\x1f');
        if (fields.size() != 5) return false;
        const QString date = QString::fromUtf8(fields.at(3));
        const QDate day = localDate(date);
        if (!day.isValid()) return false;
        const QString author = QString::fromUtf8(fields.at(1));
        const QString email = QString::fromUtf8(fields.at(2));
        if (day < start_ || day > end_ ||
            (author != w.author && email.compare(w.author, Qt::CaseInsensitive) != 0)) continue;
        const QString message = QString::fromUtf8(fields.at(4)).trimmed();
        changes->append({w, QString::fromLatin1(fields.at(0)), message, date});
    }
    return true;
}

bool WeeklyReportCollector::parseSvn(const QByteArray& data, QVector<Change>* changes)
{
    const auto& w = settings_.workspaces.at(workspaceIndex_);
    QXmlStreamReader xml(data);
    bool sawLog = false;
    while (!xml.atEnd())
    {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        if (xml.name() == QStringLiteral("log")) sawLog = true;
        if (xml.name() != QStringLiteral("logentry")) continue;
        const QString revision = xml.attributes().value(QStringLiteral("revision")).toString();
        QString author, date, message;
        while (xml.readNextStartElement())
        {
            if (xml.name() == QStringLiteral("author")) author = xml.readElementText();
            else if (xml.name() == QStringLiteral("date")) date = xml.readElementText();
            else if (xml.name() == QStringLiteral("msg")) message = xml.readElementText();
            else xml.skipCurrentElement();
        }
        const QDate day = localDate(date);
        if (!day.isValid() || revision.isEmpty()) return false;
        if (author == w.author && day >= start_ && day <= end_)
            changes->append({w, revision, message.trimmed(), date});
    }
    return sawLog && !xml.hasError();
}

void WeeklyReportCollector::request(const QString& path, const QStringList& query,
    std::function<void(const QJsonObject&, const QString&)> callback)
{
    if (!running_) return;
    QUrl url(settings_.redmineUrl + path);
    QUrlQuery parameters;
    for (int i = 0; i + 1 < query.size(); i += 2)
        parameters.addQueryItem(query.at(i), query.at(i + 1));
    url.setQuery(parameters);
    QNetworkRequest request(url);
    request.setRawHeader("X-Redmine-API-Key", apiKey_.toUtf8());
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, false);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto* reply = network_->get(request);
    reply_ = reply;
    auto body = std::make_shared<QByteArray>();
    auto failure = std::make_shared<QString>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, body, failure]() {
        body->append(reply->readAll());
        if (body->size() > MaxOutput)
        {
            *failure = QStringLiteral("API 응답 크기 제한 초과.");
            reply->abort();
        }
    });
    connect(timeout_, &QTimer::timeout, this, [reply, failure]() {
        *failure = QStringLiteral("API 요청 30초 제한 시간 초과.");
        reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, body, failure, callback]() {
        if (!running_) return;
        timeout_->stop();
        timeout_->disconnect(this);
        body->append(reply->readAll());
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (failure->isEmpty() && (reply->error() != QNetworkReply::NoError || status != 200))
            *failure = QStringLiteral("API 조회 실패 (HTTP %1, 네트워크 오류 %2). HTTPS/인증/REST API 활성화 및 권한을 확인하세요.")
                .arg(status).arg(static_cast<int>(reply->error()));
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(*body, &error);
        if (failure->isEmpty() && (body->size() > MaxOutput || error.error != QJsonParseError::NoError || !document.isObject()))
            *failure = QStringLiteral("API가 유효한 JSON 객체를 반환하지 않았습니다.");
        reply_ = nullptr;
        reply->deleteLater();
        callback(document.object(), *failure);
    });
    timeout_->start(30000);
}

void WeeklyReportCollector::collectRedmine()
{
    if (!running_) return;
    settings_.redmineUrl = WeeklyReportSettings::normalizeServerUrl(settings_.redmineUrl);
    const QString addressError = WeeklyReportSettings::serverError(settings_.redmineUrl, settings_.allowHttp);
    if (!addressError.isEmpty())
    {
        result_.partial = true;
        result_.notices.append(QStringLiteral("Redmine 미수집: %1 차주 후보는 조회되지 않았습니다.").arg(addressError));
        finish();
        return;
    }
    QString error;
    apiKey_ = WeeklyReportSettings::apiKey(settings_.redmineUrl, &error);
    if (!error.isEmpty()) result_.notices.append(error);
    const QUrl server(settings_.redmineUrl);
    if (apiKey_.isEmpty())
    {
        result_.partial = true;
        result_.notices.append(QStringLiteral("Redmine 미수집: 보고서 설정에서 주소와 API key를 확인하세요. 차주 후보는 조회되지 않았습니다."));
        finish();
        return;
    }
    if (server.scheme() == QStringLiteral("https") && !QSslSocket::supportsSsl())
    {
        result_.partial = true;
        result_.notices.append(QStringLiteral("Redmine 미수집: Qt의 TLS 런타임을 사용할 수 없습니다. 이 Qt 배포본과 호환되는 TLS 라이브러리 설정이 필요합니다. 차주 후보는 조회되지 않았습니다."));
        finish();
        return;
    }
    if (progress) progress(QStringLiteral("Redmine 현재 API 계정 확인 중"));
    request(QStringLiteral("/users/current.json"), {}, [this](const QJsonObject& root, const QString& error) {
        const auto user = root.value(QStringLiteral("user")).toObject();
        userId_ = user.value(QStringLiteral("id")).toInt();
        if (!error.isEmpty() || userId_ <= 0)
        {
            result_.partial = true;
            result_.notices.append(QStringLiteral("Redmine 현재 사용자 확인 실패: %1").arg(error.isEmpty() ? QStringLiteral("사용자 응답 누락") : error));
            finish();
            return;
        }
        result_.apiUserName = (user.value(QStringLiteral("lastname")).toString() +
            QStringLiteral(" ") + user.value(QStringLiteral("firstname")).toString()).trimmed();
        if (result_.apiUserName.isEmpty()) result_.apiUserName = user.value(QStringLiteral("login")).toString();
        for (const auto& w : settings_.workspaces)
        {
            if (!w.redmineProject.isEmpty() && !projects_.contains(w.redmineProject))
                projects_.append(w.redmineProject);
        }
        projectFilter_ = !projects_.isEmpty();
        resolveProject();
    });
}

void WeeklyReportCollector::resolveProject()
{
    if (!running_) return;
    if (projectIndex_ >= projects_.size())
    {
        collectIssues(false, 0);
        return;
    }
    const QString project = projects_.at(projectIndex_++);
    request(QStringLiteral("/projects/%1.json").arg(QString::fromLatin1(QUrl::toPercentEncoding(project))), {},
        [this, project](const QJsonObject& root, const QString& error) {
            const int id = root.value(QStringLiteral("project")).toObject().value(QStringLiteral("id")).toInt();
            if (!error.isEmpty() || id <= 0)
            {
                result_.partial = true;
                result_.notices.append(QStringLiteral("프로젝트 %1 조회 실패: %2").arg(project, error.isEmpty() ? QStringLiteral("ID 누락") : error));
            }
            else
            {
                for (const auto& w : settings_.workspaces)
                {
                    if (w.redmineProject != project) continue;
                    if (projectSections_.contains(id) && projectSections_.value(id) != w.section)
                    {
                        result_.partial = true;
                        result_.notices.append(QStringLiteral("프로젝트 %1의 섹션명이 다릅니다. 첫 섹션으로 합쳤습니다.").arg(project));
                    }
                    else projectSections_.insert(id, w.section);
                }
            }
            resolveProject();
        });
}

void WeeklyReportCollector::collectIssues(bool open, int offset)
{
    if (!running_) return;
    if (progress) progress(QStringLiteral("Redmine 내 %1 이슈 조회 중 (%2건 이후)")
        .arg(open ? QStringLiteral("열린") : QStringLiteral("전체 상태")).arg(offset));
    request(QStringLiteral("/issues.json"), {QStringLiteral("assigned_to_id"), QString::number(userId_),
        QStringLiteral("status_id"), open ? QStringLiteral("open") : QStringLiteral("*"),
        QStringLiteral("limit"), QStringLiteral("100"), QStringLiteral("offset"), QString::number(offset),
        QStringLiteral("sort"), QStringLiteral("id:asc")},
        [this, open, offset](const QJsonObject& root, const QString& error) {
            const auto pageValue = root.value(QStringLiteral("issues"));
            const int total = root.value(QStringLiteral("total_count")).toInt(-1);
            const auto page = pageValue.toArray();
            QString failure = error;
            if (failure.isEmpty() && (!pageValue.isArray() || total < 0 ||
                root.value(QStringLiteral("offset")).toInt(-1) != offset || (page.isEmpty() && offset < total)))
                failure = QStringLiteral("이슈 페이지 응답 누락 또는 페이지 불일치.");
            QVector<Issue> parsed;
            if (failure.isEmpty())
            {
                for (const auto value : page)
                {
                    const auto object = value.toObject();
                    const auto assigned = object.value(QStringLiteral("assigned_to")).toObject();
                    Issue issue;
                    issue.id = object.value(QStringLiteral("id")).toInt();
                    const auto project = object.value(QStringLiteral("project")).toObject();
                    issue.projectId = project.value(QStringLiteral("id")).toInt();
                    issue.project = project.value(QStringLiteral("name")).toString();
                    issue.title = object.value(QStringLiteral("subject")).toString();
                    issue.status = object.value(QStringLiteral("status")).toObject().value(QStringLiteral("name")).toString();
                    if (issue.id <= 0 || issue.projectId <= 0 || issue.title.isEmpty() || assigned.value(QStringLiteral("id")).toInt() != userId_)
                    {
                        failure = QStringLiteral("현재 담당자 또는 이슈 필수 정보가 일치하지 않습니다.");
                        break;
                    }
                    parsed.append(issue);
                }
            }
            if (!failure.isEmpty())
            {
                result_.partial = true;
                result_.notices.append(QStringLiteral("Redmine %1 이슈 수집 실패: %2%3")
                    .arg(open ? QStringLiteral("열린") : QStringLiteral("전체 상태"), failure,
                         open ? QStringLiteral(" 차주 후보가 불완전합니다.") : QStringLiteral(" 일부 VCS 이슈 연결이 누락될 수 있습니다.")));
                if (open) finish(); else collectIssues(true, 0);
                return;
            }
            for (const auto& issue : parsed)
            {
                issues_.insert(issue.id, issue);
                if (open && (!projectFilter_ || projectSections_.contains(issue.projectId))) openIssues_.insert(issue.id);
            }
            const int next = offset + page.size();
            if (next < total && next < MaxIssues)
            {
                collectIssues(open, next);
                return;
            }
            if (next < total)
            {
                result_.partial = true;
                result_.notices.append(QStringLiteral("Redmine 이슈 10,000건 제한으로 일부 결과가 빠졌습니다."));
            }
            if (!open) collectIssues(true, 0);
            else
            {
                openComplete_ = next >= total;
                finish();
            }
        });
}

QString WeeklyReportCollector::issueSection(const Issue& issue) const
{
    return projectSections_.value(issue.projectId, issue.project);
}

void WeeklyReportCollector::finish(bool cancelled)
{
    if (!running_) return;
    running_ = false;
    timeout_->stop();
    timeout_->disconnect(this);
    if (reply_)
    {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
    if (process_)
    {
        process_->disconnect(this);
        process_->kill();
        process_->deleteLater();
        process_ = nullptr;
    }
    apiKey_.clear();
    result_.cancelled = cancelled;
    if (cancelled) result_.notices.append(QStringLiteral("수집을 취소했습니다. 완료된 폴더/페이지의 결과만 반영했습니다."));
    QMap<QString, int> currentKeys;
    QSet<QString> evidenceKeys;
    const QRegularExpression reference(QStringLiteral("#(\\d+)\\b"));
    for (const auto& change : changes_)
    {
        const QString evidence = QStringLiteral("%1 | %2 | %3 | %4\n%5")
            .arg(change.workspace.vcs, change.workspace.path, change.revision, change.date, change.message);
        const QString evidenceKey = change.workspace.section + QChar(0x1f) + change.workspace.vcs + QChar(0x1f) + change.workspace.path + QChar(0x1f) + change.revision;
        if (evidenceKeys.contains(evidenceKey)) continue;
        evidenceKeys.insert(evidenceKey);
        QSet<int> linked;
        bool unmatched = false;
        auto matches = reference.globalMatch(change.message);
        while (matches.hasNext())
        {
            const int id = matches.next().captured(1).toInt();
            if (issues_.contains(id)) linked.insert(id);
            else unmatched = true;
        }
        const auto append = [&](const QString& section, const QString& summary, const QString& status, const QString& key) {
            if (!currentKeys.contains(key))
            {
                currentKeys.insert(key, result_.current.size());
                result_.current.append({section, summary, status, {}});
            }
            auto& line = result_.current[currentKeys.value(key)];
            if (!line.evidence.contains(evidence)) line.evidence.append(evidence);
        };
        if (linked.isEmpty() || unmatched)
        {
            const QString message = change.message.isEmpty() ? QStringLiteral("메시지 없는 %1 이력 %2").arg(change.workspace.vcs, change.revision.left(12)) : flatMessage(change.message);
            append(change.workspace.section, message, QString(),
                   change.workspace.section + QStringLiteral("/message/") + message);
        }
        if (!linked.isEmpty())
        {
            auto ids = linked.values();
            std::sort(ids.begin(), ids.end());
            for (int id : ids)
            {
                const auto issue = issues_.value(id);
                append(issueSection(issue), QStringLiteral("%1 (#%2)").arg(issue.title).arg(id), issue.status,
                       QStringLiteral("issue/%1").arg(id));
            }
            // Preserve the complete original messages in each linked row's evidence.
        }
    }
    QList<int> planned = openIssues_.values();
    std::sort(planned.begin(), planned.end());
    for (int id : planned)
    {
        const auto issue = issues_.value(id);
        result_.next.append({issueSection(issue), QStringLiteral("%1 (#%2)").arg(issue.title).arg(id), issue.status,
            {QStringLiteral("Redmine 현재 API 사용자 담당 · 열린 이슈 · 상태: %1").arg(issue.status)}});
    }
    if (openComplete_)
        result_.notices.append(QStringLiteral("Redmine 차주 후보 조회 완료: %1건. 열린 이슈는 계획 후보이며 날짜에 자동 배정된 업무가 아닙니다.").arg(result_.next.size()));
    if (settings_.workspaces.isEmpty()) result_.notices.append(QStringLiteral("등록된 workspace가 없어 금주 VCS 이력을 수집하지 않았습니다."));
    if (completed) completed(result_);
}
