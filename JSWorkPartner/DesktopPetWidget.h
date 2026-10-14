#pragma once
#include <QtWidgets/QWidget>
#include <QtCore/QTimer>

class DesktopPetWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit DesktopPetWidget(QWidget* parent = nullptr);
    void setBusy(bool busy);
    void setPendingCount(int count);
    void setSizePercent(int percent);
    int sizePercent() const { return sizePercent_; }
    static int normalizedSizePercent(int percent);
    void setAlwaysOnTop(bool alwaysOnTop);
    bool alwaysOnTop() const { return windowFlags().testFlag(Qt::WindowStaysOnTopHint); }
    void restorePosition(const QPoint& position);
    void ensureVisibleOnScreen();
signals:
    void assistantRequested();
    void newTaskRequested();
    void tasksRequested();
    void weeklyReportRequested();
    void settingsRequested();
    void hideRequested();
    void positionChanged(QPoint position);
protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
private:
    QPoint boundedPosition(const QPoint& position) const;
    QTimer animation_;
    int frame_ = 0;
    int sizePercent_ = 50;
    int pendingCount_ = 0;
    bool busy_ = false;
    bool pressed_ = false;
    bool dragging_ = false;
    QPoint pressPosition_;
    QPoint dragOffset_;
};
