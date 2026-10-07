#include "WeeklyReportDialog.h"
#include "LocalTaskReport.h"

#include <QtCore/QMap>
#include <QtCore/QMimeData>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QCloseEvent>
#include <QtGui/QTextDocument>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDateEdit>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QVBoxLayout>

namespace
{
QString dateText(const QDate& date)
{
    return date.toString(QStringLiteral("yyyy.MM.dd"));
}

QString linkedText(const QString& text, const QString& server)
{
    const QUrl url(server);
    const bool valid = url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() &&
        !url.hasQuery() && !url.hasFragment() &&
        (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"));
    if (!valid) return text.toHtmlEscaped();
    static const QRegularExpression reference(QStringLiteral("#(\\d+)\\b"));
    auto matches = reference.globalMatch(text);
    QString html;
    int previous = 0;
    while (matches.hasNext())
    {
        const auto match = matches.next();
        html += text.mid(previous, match.capturedStart() - previous).toHtmlEscaped();
        html += QStringLiteral("<a href=\"%1/issues/%2\">#%2</a>")
            .arg(server.toHtmlEscaped(), match.captured(1));
        previous = match.capturedEnd();
    }
    html += text.mid(previous).toHtmlEscaped();
    html.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return html;
}
}

WeeklyReportDialog::WeeklyReportDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("주간업무 보고서"));
    resize(1120, 780);
    setMinimumSize(900, 600);
    const auto settings = WeeklyReportSettings::load();
    redmineUrl_ = settings.redmineUrl;
    auto* layout = new QVBoxLayout(this);
    auto* heading = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("주간업무_"), this);
    reportDate_ = new QDateEdit(QDate::currentDate(), this);
    name_ = new QLineEdit(settings.displayName, this);
    name_->setPlaceholderText(QStringLiteral("이름 (수집 시 현재 API 계정 이름)"));
    settingsButton_ = new QPushButton(QStringLiteral("보고서 설정..."), this);
    heading->addWidget(title);
    heading->addWidget(reportDate_);
    heading->addWidget(new QLabel(QStringLiteral("이름"), this));
    heading->addWidget(name_, 1);
    heading->addWidget(settingsButton_);
    layout->addLayout(heading);

    const QDate monday = QDate::currentDate().addDays(1 - QDate::currentDate().dayOfWeek());
    currentStart_ = new QDateEdit(monday, this);
    currentEnd_ = new QDateEdit(monday.addDays(6), this);
    nextStart_ = new QDateEdit(monday.addDays(7), this);
    nextEnd_ = new QDateEdit(monday.addDays(13), this);
    for (auto* date : {reportDate_, currentStart_, currentEnd_, nextStart_, nextEnd_})
    {
        date->setCalendarPopup(true);
        date->setDisplayFormat(QStringLiteral("yyyy.MM.dd"));
    }
    auto* periods = new QHBoxLayout;
    periods->addWidget(new QLabel(QStringLiteral("금주"), this));
    periods->addWidget(currentStart_);
    periods->addWidget(new QLabel(QStringLiteral("~"), this));
    periods->addWidget(currentEnd_);
    periods->addSpacing(16);
    periods->addWidget(new QLabel(QStringLiteral("차주"), this));
    periods->addWidget(nextStart_);
    periods->addWidget(new QLabel(QStringLiteral("~"), this));
    periods->addWidget(nextEnd_);
    periods->addStretch();
    layout->addLayout(periods);
    auto* hint = new QLabel(QStringLiteral(
        "금주는 Git 커밋일(committer) / SVN 리비전일을 PC의 현지 날짜로 필터링합니다. "
        "차주는 현재 담당자의 열린 이슈 전체에서 고르는 계획 후보입니다.\n"
        "섹션·작업 요약을 직접 편집하고 행을 추가/삭제할 수 있습니다. 분석·회의는 수동 행으로 보완하세요. 초안은 이 창에서만 유지됩니다."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* tabs = new QTabWidget(this);
    auto* editor = new QWidget(tabs);
    auto* editorLayout = new QVBoxLayout(editor);
    auto* columns = new QSplitter(Qt::Horizontal, editor);
    const auto createColumn = [&](const QString& label, QTableWidget** output) {
        auto* host = new QWidget(columns);
        auto* column = new QVBoxLayout(host);
        column->setContentsMargins(0, 0, 0, 0);
        column->addWidget(new QLabel(label, host));
        auto* table = new QTableWidget(0, 3, host);
        *output = table;
        table->setHorizontalHeaderLabels({QStringLiteral("프로젝트/섹션"), QStringLiteral("작업 요약 (#번호)"), QStringLiteral("현재 상태")});
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setWordWrap(true);
        column->addWidget(table, 1);
        auto* actions = new QHBoxLayout;
        auto* add = new QPushButton(QStringLiteral("수동 행 추가"), host);
        auto* remove = new QPushButton(QStringLiteral("선택 행 삭제"), host);
        editButtons_ += {add, remove};
        actions->addWidget(add);
        actions->addWidget(remove);
        actions->addStretch();
        column->addLayout(actions);
        connect(add, &QPushButton::clicked, this, [this, table]() {
            QString section;
            if (table->currentRow() >= 0) section = table->item(table->currentRow(), 0)->text();
            addLine(table, {section, QString(), QString(), {QStringLiteral("수동 작성 (분석·회의 등)")}});
            table->setCurrentCell(table->rowCount() - 1, 1);
            table->editItem(table->item(table->rowCount() - 1, 1));
            dirty_ = true;
        });
        connect(remove, &QPushButton::clicked, this, [this, table]() {
            if (table->currentRow() < 0) return;
            table->removeRow(table->currentRow());
            dirty_ = true;
            refreshPreview();
        });
        connect(table, &QTableWidget::itemChanged, this, [this]() {
            if (filling_) return;
            dirty_ = true;
            refreshPreview();
        });
        connect(table, &QTableWidget::itemSelectionChanged, this, [this, table]() {
            if (filling_ || !evidence_) return;
            const int row = table->currentRow();
            const auto* item = row < 0 ? nullptr : table->item(row, 1);
            evidence_->setPlainText(item ? item->data(Qt::UserRole).toStringList().join(QStringLiteral("\n\n")) : QString());
        });
        columns->addWidget(host);
    };
    createColumn(QStringLiteral("금주 · VCS 수행 근거"), &current_);
    createColumn(QStringLiteral("차주 · 열린 이슈 계획 후보"), &next_);
    editorLayout->addWidget(columns, 1);
    editorLayout->addWidget(new QLabel(QStringLiteral("선택 행의 원문/근거 (보고서에 포함되지 않음)"), editor));
    evidence_ = new QPlainTextEdit(editor);
    evidence_->setReadOnly(true);
    evidence_->setMaximumHeight(135);
    editorLayout->addWidget(evidence_);
    tabs->addTab(editor, QStringLiteral("초안 편집"));
    preview_ = new QTextBrowser(tabs);
    preview_->setOpenExternalLinks(true);
    tabs->addTab(preview_, QStringLiteral("2열 미리보기"));
    notices_ = new QPlainTextEdit(tabs);
    notices_->setReadOnly(true);
    notices_->setPlainText(QStringLiteral("보고서 설정에서 workspace와 Redmine API key를 등록한 뒤 수집하세요.\n"
        "VCS 수행 근거가 없는 Redmine 이슈는 금주 실적으로 만들지 않습니다."));
    tabs->addTab(notices_, QStringLiteral("수집 결과 / 오류"));
    layout->addWidget(tabs, 1);
    status_ = new QLabel(QStringLiteral("수집 전 · 수동 편집도 가능합니다."), this);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto* actions = new QHBoxLayout;
    collectButton_ = new QPushButton(QStringLiteral("이력·이슈 수집 → 초안 만들기"), this);
    collectButton_->setObjectName(QStringLiteral("primaryButton"));
    cancelButton_ = new QPushButton(QStringLiteral("수집 취소"), this);
    cancelButton_->setEnabled(false);
    copyButton_ = new QPushButton(QStringLiteral("HTML 복사"), this);
    exportButton_ = new QPushButton(QStringLiteral("HTML 파일 저장"), this);
    auto* close = new QPushButton(QStringLiteral("닫기"), this);
    actions->addWidget(collectButton_);
    actions->addWidget(cancelButton_);
    actions->addStretch();
    actions->addWidget(copyButton_);
    actions->addWidget(exportButton_);
    actions->addWidget(close);
    layout->addLayout(actions);
    connect(collectButton_, &QPushButton::clicked, this, [this]() { collect(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this]() { if (collector_) collector_->cancel(); });
    connect(settingsButton_, &QPushButton::clicked, this, [this]() {
        if (WeeklyReportSettings::edit(this))
        {
            const auto settings = WeeklyReportSettings::load();
            if (current_->rowCount() == 0 && next_->rowCount() == 0)
                redmineUrl_ = settings.redmineUrl;
            else if (redmineUrl_ != settings.redmineUrl)
                status_->setText(QStringLiteral("Redmine 주소가 변경되었습니다. 기존 이슈 링크는 수집 당시 서버를 유지합니다. 새 서버로 다시 수집하세요."));
            if (!settings.displayName.isEmpty()) name_->setText(settings.displayName);
            refreshPreview();
        }
    });
    connect(copyButton_, &QPushButton::clicked, this, [this]() {
        if (!canExport()) return;
        const QString html = reportHtml();
        QTextDocument text;
        text.setHtml(html);
        auto* mime = new QMimeData;
        mime->setHtml(html);
        mime->setText(text.toPlainText());
        QApplication::clipboard()->setMimeData(mime);
        dirty_ = false;
        status_->setText(QStringLiteral("HTML과 일반 텍스트를 클립보드에 복사했습니다."));
    });
    connect(exportButton_, &QPushButton::clicked, this, [this]() {
        if (!canExport()) return;
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("주간업무 HTML 저장"),
            QStringLiteral("주간업무_%1.html").arg(dateText(reportDate_->date())), QStringLiteral("HTML (*.html)"));
        if (path.isEmpty()) return;
        QSaveFile file(path);
        const QByteArray data = reportHtml().toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        {
            file.cancelWriting();
            QMessageBox::warning(this, QStringLiteral("저장 실패"), QStringLiteral("HTML 파일을 저장하지 못했습니다. 경로와 권한을 확인하세요."));
            return;
        }
        dirty_ = false;
        status_->setText(QStringLiteral("HTML 파일 저장 완료: %1").arg(path));
    });
    connect(close, &QPushButton::clicked, this, [this]() { reject(); });
    connect(name_, &QLineEdit::textChanged, this, [this]() { dirty_ = true; refreshPreview(); });
    connect(reportDate_, &QDateEdit::dateChanged, this, [this]() { dirty_ = true; refreshPreview(); });
    for (auto* date : {currentStart_, currentEnd_, nextStart_, nextEnd_})
        connect(date, &QDateEdit::dateChanged, this, [this]() {
            dirty_ = true;
            if (current_->rowCount() || next_->rowCount())
                status_->setText(QStringLiteral("기간이 변경되었습니다. 기존 행은 자동 재수집되지 않습니다. 새 기간으로 다시 수집하거나 수동으로 확인하세요."));
            refreshPreview();
        });
    refreshPreview();
}

bool WeeklyReportDialog::showSettings(QWidget* parent)
{
    return WeeklyReportSettings::edit(parent);
}

void WeeklyReportDialog::setLocalTaskProvider(std::function<QList<Task>()> provider)
{
    localTaskProvider_ = std::move(provider);
    collectButton_->setToolTip(QStringLiteral("선택한 금주 기간에 완료한 로컬 업무도 초안에 포함합니다. 외부 변경은 다음 초안 생성 시 반영됩니다."));
}

void WeeklyReportDialog::addLine(QTableWidget* table, const ReportLine& line)
{
    const bool wasFilling = filling_;
    filling_ = true;
    const int row = table->rowCount();
    table->insertRow(row);
    table->setItem(row, 0, new QTableWidgetItem(line.section));
    auto* summary = new QTableWidgetItem(line.summary);
    summary->setData(Qt::UserRole, line.evidence);
    summary->setToolTip(QStringLiteral("선택하면 아래에 원문 근거가 표시됩니다."));
    table->setItem(row, 1, summary);
    auto* status = new QTableWidgetItem(line.status);
    status->setFlags(status->flags() & ~Qt::ItemIsEditable);
    table->setItem(row, 2, status);
    filling_ = wasFilling;
}

void WeeklyReportDialog::setBusy(bool busy)
{
    collectButton_->setEnabled(!busy);
    settingsButton_->setEnabled(!busy);
    copyButton_->setEnabled(!busy);
    exportButton_->setEnabled(!busy);
    cancelButton_->setEnabled(busy);
    current_->setEnabled(!busy);
    next_->setEnabled(!busy);
    for (auto* button : editButtons_) button->setEnabled(!busy);
    for (auto* date : {currentStart_, currentEnd_, nextStart_, nextEnd_}) date->setEnabled(!busy);
}

void WeeklyReportDialog::collect()
{
    if (collector_) return;
    if (currentStart_->date() > currentEnd_->date() || nextStart_->date() > nextEnd_->date())
    {
        QMessageBox::warning(this, QStringLiteral("기간 확인"), QStringLiteral("시작일은 종료일보다 늦을 수 없습니다."));
        return;
    }
    if ((current_->rowCount() || next_->rowCount()) && QMessageBox::question(this, QStringLiteral("초안 다시 만들기"),
        QStringLiteral("현재 편집한 행을 수집 결과로 교체할까요? 수동 추가 내용도 교체됩니다."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    const auto settings = WeeklyReportSettings::load();
    redmineUrl_ = settings.redmineUrl;
    if (settings.workspaces.isEmpty() && settings.redmineUrl.isEmpty())
    {
        if (localTaskProvider_)
        {
            applyCollectedResult({});
            return;
        }
        QMessageBox::information(this, QStringLiteral("설정 필요"), QStringLiteral("보고서 설정에서 workspace 또는 Redmine을 먼저 등록하세요. 수동 행은 바로 작성할 수 있습니다."));
        return;
    }
    collector_ = new WeeklyReportCollector(settings, currentStart_->date(), currentEnd_->date(), this);
    setBusy(true);
    collector_->progress = [this](const QString& text) { status_->setText(text); };
    collector_->completed = [this](const WeeklyReportResult& result) {
        auto* finished = collector_;
        collector_ = nullptr;
        applyCollectedResult(result);
        finished->deleteLater();
    };
    collector_->start();
}

void WeeklyReportDialog::applyCollectedResult(WeeklyReportResult result)
{
    if (localTaskProvider_)
        mergeLocalCompletedTasks(result, localTaskProvider_(), currentStart_->date(), currentEnd_->date());
    filling_ = true;
    current_->setRowCount(0);
    next_->setRowCount(0);
    for (const auto& line : result.current) addLine(current_, line);
    for (const auto& line : result.next) addLine(next_, line);
    filling_ = false;
    collectedStart_ = currentStart_->date();
    collectedEnd_ = currentEnd_->date();
    if (name_->text().trimmed().isEmpty() && !result.apiUserName.isEmpty()) name_->setText(result.apiUserName);
    notices_->setPlainText(result.notices.join(QStringLiteral("\n\n")));
    evidence_->clear();
    dirty_ = true;
    status_->setText(QStringLiteral("%1 · 금주 %2행 / 차주 후보 %3행. 수집 결과/오류 탭을 확인하고 초안을 편집하세요.")
        .arg(result.cancelled ? QStringLiteral("수집 취소 (완료된 결과만 반영)") :
             result.partial ? QStringLiteral("일부 수집 실패/누락") : QStringLiteral("수집 종료"))
        .arg(result.current.size()).arg(result.next.size()));
    setBusy(false);
    refreshPreview();
}

QString WeeklyReportDialog::columnHtml(QTableWidget* table) const
{
    QMap<QString, QStringList> sections;
    QMap<QString, QSet<QString>> nonLocalSummaries;
    QSet<QString> localIds;
    QStringList order;
    for (int row = 0; row < table->rowCount(); ++row)
    {
        const QString summary = table->item(row, 1)->text().trimmed();
        if (summary.isEmpty()) continue;
        QString section = table->item(row, 0)->text().trimmed();
        if (section.isEmpty()) section = QStringLiteral("기타");
        if (!sections.contains(section)) order.append(section);
        QString localId;
        for (const auto& evidence : table->item(row, 1)->data(Qt::UserRole).toStringList())
            if (evidence.startsWith(QStringLiteral("local-task:"))) { localId = evidence; break; }
        if (!localId.isEmpty())
        {
            if (localIds.contains(localId)) continue;
            localIds.insert(localId);
        }
        else
        {
            if (nonLocalSummaries[section].contains(summary)) continue;
            nonLocalSummaries[section].insert(summary);
        }
        sections[section].append(summary);
    }
    QString html;
    for (const auto& section : order)
    {
        html += QStringLiteral("<h3 style=\"margin:14px 0 6px;\">%1</h3><ul style=\"margin:0;padding-left:22px;\">").arg(section.toHtmlEscaped());
        for (const auto& summary : sections.value(section))
            html += QStringLiteral("<li style=\"margin-bottom:6px;\">%1</li>").arg(linkedText(summary, redmineUrl_));
        html += QStringLiteral("</ul>");
    }
    return html;
}

QString WeeklyReportDialog::reportHtml() const
{
    const QString currentRange = dateText(currentStart_->date()) + QStringLiteral(" ~ ") + dateText(currentEnd_->date());
    const QString nextRange = dateText(nextStart_->date()) + QStringLiteral(" ~ ") + dateText(nextEnd_->date());
    const QString provenance = collectedStart_.isValid() &&
        (collectedStart_ != currentStart_->date() || collectedEnd_ != currentEnd_->date())
        ? QStringLiteral("<p style=\"color:#a33;\">금주 VCS 수집 기준: %1 ~ %2 (표시 기간 변경 후 검토 필요)</p>")
            .arg(dateText(collectedStart_), dateText(collectedEnd_)) : QString();
    return QStringLiteral(
        "<!DOCTYPE html><html lang=\"ko\"><head><meta charset=\"utf-8\"><title>주간업무_%1</title></head>"
        "<body style=\"font-family:'Malgun Gothic',sans-serif;color:#222;background:#fff;\">"
        "<h1>주간업무_%1</h1><p>%2</p>%7"
        "<table width=\"100%\" border=\"1\" cellspacing=\"0\" cellpadding=\"12\" style=\"border-collapse:collapse;border:1px solid #bbb;table-layout:fixed;\">"
        "<thead><tr><th width=\"50%\" style=\"background:#eee;\">금주<br><span style=\"font-weight:normal;\">%3</span></th>"
        "<th width=\"50%\" style=\"background:#eee;\">차주<br><span style=\"font-weight:normal;\">%4</span></th></tr></thead>"
        "<tbody><tr><td width=\"50%\" valign=\"top\" style=\"overflow-wrap:anywhere;\">%5</td>"
        "<td width=\"50%\" valign=\"top\" style=\"overflow-wrap:anywhere;\">%6</td></tr></tbody></table></body></html>")
        .arg(dateText(reportDate_->date()), name_->text().trimmed().toHtmlEscaped(),
             currentRange, nextRange, columnHtml(current_), columnHtml(next_), provenance);
}

bool WeeklyReportDialog::canExport()
{
    if (currentStart_->date() > currentEnd_->date() || nextStart_->date() > nextEnd_->date())
    {
        QMessageBox::warning(this, QStringLiteral("기간 확인"), QStringLiteral("시작일은 종료일보다 늦을 수 없습니다."));
        return false;
    }
    return true;
}

void WeeklyReportDialog::refreshPreview()
{
    preview_->setHtml(reportHtml());
}

bool WeeklyReportDialog::canClose()
{
    if (collector_)
    {
        if (QMessageBox::question(this, QStringLiteral("수집 중"), QStringLiteral("수집을 취소하고 창을 닫을까요?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return false;
        if (collector_) collector_->cancel();
    }
    if (dirty_ && (current_->rowCount() || next_->rowCount()) &&
        QMessageBox::question(this, QStringLiteral("초안 닫기"), QStringLiteral("아직 내보내지 않은 초안을 닫을까요? 이 창의 편집 내용은 저장되지 않습니다."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return false;
    return true;
}

void WeeklyReportDialog::reject()
{
    if (canClose()) QDialog::reject();
}

void WeeklyReportDialog::closeEvent(QCloseEvent* event)
{
    if (canClose())
    {
        event->accept();
        QDialog::reject();
    }
    else event->ignore();
}
