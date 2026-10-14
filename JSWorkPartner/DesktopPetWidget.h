#pragma once
#include <QtWidgets/QWidget>
#include <QtCore/QTimer>
#include <QtCore/QStringList>

class QPainter;

class DesktopPetWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit DesktopPetWidget(QWidget* parent = nullptr);
    void setBusy(bool busy);
    void setDesignId(const QString& id);
    QString designId() const { return designId_; }
    static QString normalizedDesignId(const QString& id);
    static QStringList designIds();
    static QString designName(const QString& id);
    // Shared logical 144x150 canvas for the desktop pet and settings preview.
    static void paintDesign(QPainter& painter, const QString& id, int frame, bool busy, int pendingCount);
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
    QString designId_ = QStringLiteral("mint-bot");
    int frame_ = 0;
    int sizePercent_ = 50;
    int pendingCount_ = 0;
    bool busy_ = false;
    bool pressed_ = false;
    bool dragging_ = false;
    QPoint pressPosition_;
    QPoint dragOffset_;
};
