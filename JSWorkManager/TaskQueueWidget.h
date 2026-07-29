#pragma once

#include <array>

#include <QtWidgets/QWidget>

class QCloseEvent;
class QAction;
class QDate;
class QEvent;
class QLabel;
class QLineEdit;
class QPoint;
class QPushButton;
class QTableView;
class QTabWidget;
class QTextEdit;
class QToolButton;
class QSystemTrayIcon;
class NullableDateEdit;
class QueueFloatingWidget;
class TaskQueueViewModel;
struct Task;

class TaskQueueWidget final : public QWidget
{
public:
    explicit TaskQueueWidget(
        TaskQueueViewModel& pendingViewModel,
        TaskQueueViewModel& completedViewModel,
        QWidget* parent = nullptr);
    ~TaskQueueWidget() override;
    void showInitialView();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void setupTrayIcon();
    void showSettingsDialog();
    void showFromTray();
    void showTrayHint();
    void addTask();
    void startEditing();
    void cancelEditing();
    void completeSelectedTask();
    void deleteSelectedTask();
    void handleTableSelection(QTableView* source);
    void showTableContextMenu(QTableView* table, const QPoint& position);
    void openTaskFromFloatingWidget(int row, bool edit);
    void addTaskFromFloatingWidget();
    void completeTaskFromFloatingWidget(int row);
    void setImportance(int importance);
    void setEditingUi(bool editing);
    Task selectedTask() const;
    int selectedPendingRow() const;
    void updateSelectedTaskDetails();
    void updateUiState();
    QString summaryText(
        const QDate& rangeStart,
        const QDate& rangeEnd) const;
    void showWeeklySummaryList();
    void showViewModelError();

    TaskQueueViewModel& pendingViewModel_;
    TaskQueueViewModel& completedViewModel_;
    QLineEdit* titleInput_ = nullptr;
    QLineEdit* externalLinkInput_ = nullptr;
    QTextEdit* detailsInput_ = nullptr;
    std::array<QPushButton*, 5> importanceStars_{{nullptr, nullptr, nullptr, nullptr, nullptr}};
    int importance_ = 1;
    NullableDateEdit* deadlineInput_ = nullptr;
    QToolButton* deadlineClearButton_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* editButton_ = nullptr;
    QPushButton* cancelEditButton_ = nullptr;
    QPushButton* completeButton_ = nullptr;
    QTableView* pendingPreviewTable_ = nullptr;
    QTableView* pendingFullTable_ = nullptr;
    QTableView* completedTable_ = nullptr;
    QTableView* selectedTable_ = nullptr;
    QTabWidget* workTabs_ = nullptr;
    QWidget* newTaskHost_ = nullptr;
    QWidget* selectedEditHost_ = nullptr;
    QWidget* selectedDisplayWidget_ = nullptr;
    QWidget* taskFormWidget_ = nullptr;
    QTabWidget* queueTabs_ = nullptr;
    QLabel* queueCountLabel_ = nullptr;
    QLabel* weeklySummaryLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* selectedTitleLabel_ = nullptr;
    QLabel* selectedDetailsLabel_ = nullptr;
    QLabel* selectedMetaLabel_ = nullptr;
    QPushButton* externalLinkButton_ = nullptr;
    QString selectedExternalUrl_;
    int editingRow_ = -1;
    TaskQueueViewModel* editingViewModel_ = nullptr;
    QSystemTrayIcon* trayIcon_ = nullptr;
    QueueFloatingWidget* floatingQueueWidget_ = nullptr;
    QAction* floatingQueueAction_ = nullptr;
    bool trayHintShown_ = false;
};
