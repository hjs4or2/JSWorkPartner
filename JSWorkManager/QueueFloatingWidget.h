#pragma once

#include <functional>

#include <QtWidgets/QWidget>

class QTableView;
class QSizeGrip;
class TaskQueueViewModel;

class QueueFloatingWidget final : public QWidget
{
public:
    explicit QueueFloatingWidget(
        TaskQueueViewModel& viewModel,
        QWidget* parent = nullptr);

    void setVisibilityChangedHandler(
        std::function<void(bool)> handler);
    void setTaskActionHandler(
        std::function<void(int, bool)> handler);
    void setAddTaskHandler(std::function<void()> handler);
    void setCompleteTaskHandler(std::function<void(int)> handler);

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool nativeEvent(
        const QByteArray& eventType,
        void* message,
        long* result) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void ensureVisibleOnScreen();

    TaskQueueViewModel& viewModel_;
    QTableView* table_ = nullptr;
    QSizeGrip* sizeGrip_ = nullptr;
    QWidget* dragHandle_ = nullptr;
    bool dragging_ = false;
    QPoint dragOffset_;
    std::function<void(bool)> visibilityChangedHandler_;
    std::function<void(int, bool)> taskActionHandler_;
    std::function<void()> addTaskHandler_;
    std::function<void(int)> completeTaskHandler_;
};
