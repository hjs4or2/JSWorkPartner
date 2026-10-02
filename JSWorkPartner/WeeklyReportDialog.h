#pragma once

#include <QtWidgets/QDialog>
#include "WeeklyReportCollector.h"

class QDateEdit;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTextBrowser;
class QCloseEvent;

class WeeklyReportDialog final : public QDialog
{
public:
    explicit WeeklyReportDialog(QWidget* parent = nullptr);
    static bool showSettings(QWidget* parent);

protected:
    void reject() override;
    void closeEvent(QCloseEvent* event) override;

private:
    void collect();
    void setBusy(bool busy);
    void addLine(QTableWidget* table, const ReportLine& line);
    void refreshPreview();
    QString reportHtml() const;
    QString columnHtml(QTableWidget* table) const;
    bool canClose();
    bool canExport();

    QDateEdit* reportDate_;
    QDateEdit* currentStart_;
    QDateEdit* currentEnd_;
    QDateEdit* nextStart_;
    QDateEdit* nextEnd_;
    QLineEdit* name_;
    QTableWidget* current_;
    QTableWidget* next_;
    QPlainTextEdit* evidence_ = nullptr;
    QPlainTextEdit* notices_;
    QTextBrowser* preview_;
    QLabel* status_;
    QPushButton* collectButton_;
    QPushButton* cancelButton_;
    QPushButton* settingsButton_;
    QPushButton* copyButton_;
    QPushButton* exportButton_;
    QVector<QPushButton*> editButtons_;
    WeeklyReportCollector* collector_ = nullptr;
    QString redmineUrl_;
    QDate collectedStart_;
    QDate collectedEnd_;
    bool dirty_ = false;
    bool filling_ = false;
};
