#include "QueueFloatingWidget.h"

#include "DeadlineColorDelegate.h"

#include <QtCore/QItemSelectionModel>
#include <QtCore/QSortFilterProxyModel>
#include <QtGui/QCloseEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QResizeEvent>
#include <QtGui/QShowEvent>
#include <QtWidgets/QAction>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>
#include <QtWidgets/QGraphicsDropShadowEffect>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSizeGrip>
#include <QtWidgets/QTableView>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

#include "TaskQueueViewModel.h"

namespace
{
class FirstFiveTasksProxyModel final : public QSortFilterProxyModel
{
public:
    explicit FirstFiveTasksProxyModel(QObject* parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
    }

protected:
    bool filterAcceptsRow(
        int sourceRow,
        const QModelIndex&) const override
    {
        return sourceRow < 5;
    }
};

class CenteredCloseButton final : public QToolButton
{
public:
    explicit CenteredCloseButton(QWidget* parent = nullptr)
        : QToolButton(parent)
    {
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QToolButton::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor color =
            isDown()
                ? QColor(QStringLiteral("#B91C1C"))
                : underMouse()
                      ? QColor(QStringLiteral("#DC2626"))
                      : QColor(QStringLiteral("#64748B"));
        QPen pen(color, 1.8);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);

        const QPointF center = rect().center();
        const qreal radius = 4.0;
        painter.drawLine(
            center + QPointF(-radius, -radius),
            center + QPointF(radius, radius));
        painter.drawLine(
            center + QPointF(radius, -radius),
            center + QPointF(-radius, radius));
    }
};

class CenteredMinimizeButton final : public QToolButton
{
public:
    explicit CenteredMinimizeButton(QWidget* parent = nullptr)
        : QToolButton(parent)
    {
    }

    void setRestoreMode(bool restoreMode)
    {
        restoreMode_ = restoreMode;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QToolButton::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor color =
            isDown()
                ? QColor(QStringLiteral("#1D4ED8"))
                : underMouse()
                      ? QColor(QStringLiteral("#2563EB"))
                      : QColor(QStringLiteral("#64748B"));
        QPen pen(color, 2.0);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);

        const QPointF center = rect().center();
        if (restoreMode_)
        {
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(
                QRectF(center.x() - 4.5, center.y() - 3.5, 9.0, 7.0),
                1.5,
                1.5);
        }
        else
        {
            painter.drawLine(
                center + QPointF(-5.0, 2.5),
                center + QPointF(5.0, 2.5));
        }
    }

private:
    bool restoreMode_ = false;
};
}

QueueFloatingWidget::QueueFloatingWidget(
    TaskQueueViewModel& viewModel,
    QWidget* parent)
    : QWidget(
          parent,
          Qt::Tool |
              Qt::FramelessWindowHint |
              Qt::WindowStaysOnTopHint),
      viewModel_(viewModel)
{
    setObjectName(QStringLiteral("floatingQueueWidget"));
    setWindowTitle(QStringLiteral("퀘스트 보드"));
    setAttribute(Qt::WA_QuitOnClose, false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    resize(640, 305);
    setMinimumSize(480, 270);

    auto* card = new QWidget(this);
    card->setObjectName(QStringLiteral("floatingQueueCard"));
    auto* shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(15, 23, 42, 70));
    card->setGraphicsEffect(shadow);

    dragHandle_ = new QWidget(this);
    dragHandle_->setObjectName(QStringLiteral("floatingQueueHeader"));
    dragHandle_->installEventFilter(this);

    auto* titleLabel = new QLabel(QStringLiteral("퀘스트 보드"), dragHandle_);
    titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(11);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    auto* closeButton = new CenteredCloseButton(dragHandle_);
    closeButton->setObjectName(QStringLiteral("floatingQueueCloseButton"));
    closeButton->setToolTip(QStringLiteral("위젯 숨기기"));
    closeButton->setFixedSize(24, 24);

    auto* minimizeButton = new CenteredMinimizeButton(dragHandle_);
    minimizeButton->setObjectName(
        QStringLiteral("floatingQueueMinimizeButton"));
    minimizeButton->setToolTip(QStringLiteral("접기"));
    minimizeButton->setFixedSize(24, 24);

    auto* headerLayout = new QHBoxLayout(dragHandle_);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(minimizeButton);
    headerLayout->addWidget(closeButton);

    auto* proxyModel = new FirstFiveTasksProxyModel(this);
    proxyModel->setSourceModel(&viewModel_);

    table_ = new QTableView(this);
    table_->setObjectName(QStringLiteral("floatingQueueTable"));
    table_->setModel(proxyModel);
    table_->setItemDelegateForColumn(
        TaskQueueViewModel::DeadlineColumn,
        new DeadlineColorDelegate(table_));
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->setAlternatingRowColors(true);
    table_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setMinimumSectionSize(22);
    table_->verticalHeader()->setDefaultSectionSize(26);
    table_->horizontalHeader()->setFixedHeight(25);
    table_->hideColumn(TaskQueueViewModel::DetailsColumn);
    table_->hideColumn(TaskQueueViewModel::DateColumn);
    table_->horizontalHeader()->setSectionResizeMode(
        TaskQueueViewModel::SequenceColumn,
        QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(
        TaskQueueViewModel::TitleColumn,
        QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(
        TaskQueueViewModel::ImportanceColumn,
        QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(
        TaskQueueViewModel::DeadlineColumn,
        QHeaderView::ResizeToContents);

    auto* addButton = new QPushButton(QStringLiteral("새 퀘스트"), card);
    addButton->setObjectName(QStringLiteral("primaryButton"));
    auto* completeButton =
        new QPushButton(QStringLiteral("선택 퀘스트 완료"), card);
    completeButton->setObjectName(QStringLiteral("successButton"));
    completeButton->setEnabled(false);

    auto* actionLayout = new QHBoxLayout();
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);
    actionLayout->addWidget(addButton);
    actionLayout->addStretch();
    actionLayout->addWidget(completeButton);

    auto* contentHost = new QWidget(card);
    auto* contentLayout = new QVBoxLayout(contentHost);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(8);
    contentLayout->addWidget(table_, 1);
    contentLayout->addLayout(actionLayout);

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(14, 7, 14, 7);
    cardLayout->setSpacing(6);
    cardLayout->addWidget(dragHandle_);
    cardLayout->addWidget(contentHost, 1);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(12, 8, 12, 16);
    outerLayout->addWidget(card);

    sizeGrip_ = new QSizeGrip(this);
    sizeGrip_->setToolTip(QStringLiteral("드래그하여 위젯 크기 조절"));
    sizeGrip_->setFixedSize(20, 20);
    sizeGrip_->raise();

    connect(closeButton, &QToolButton::clicked, this, &QWidget::close);
    connect(
        minimizeButton,
        &QToolButton::clicked,
        this,
        [this,
         contentHost,
         minimizeButton,
         expandedSize = QSize()]() mutable {
            const bool collapsing = contentHost->isVisible();
            if (collapsing)
            {
                const QRect expandedGeometry = geometry();
                expandedSize = size();
                contentHost->hide();
                sizeGrip_->hide();
                setMinimumSize(240, 62);
                setMaximumSize(240, 62);
                resize(240, 62);
                move(
                    expandedGeometry.right() - width() + 1,
                    expandedGeometry.top());
                minimizeButton->setRestoreMode(true);
                minimizeButton->setToolTip(QStringLiteral("펼치기"));
            }
            else
            {
                setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                setMinimumSize(480, 270);
                contentHost->show();
                sizeGrip_->show();
                const QSize restoredSize =
                    expandedSize.isValid()
                        ? expandedSize
                        : QSize(640, 305);
                const QRect availableGeometry =
                    QApplication::desktop()->availableGeometry(this);
                QPoint restoredPosition = pos();
                if (restoredPosition.x() + restoredSize.width() >
                    availableGeometry.right() + 1)
                {
                    restoredPosition.setX(
                        availableGeometry.right() -
                        restoredSize.width() +
                        1);
                }
                if (restoredPosition.x() < availableGeometry.left())
                {
                    restoredPosition.setX(availableGeometry.left());
                }
                if (restoredPosition.y() + restoredSize.height() >
                    availableGeometry.bottom() + 1)
                {
                    restoredPosition.setY(
                        availableGeometry.bottom() -
                        restoredSize.height() +
                        1);
                }
                if (restoredPosition.y() < availableGeometry.top())
                {
                    restoredPosition.setY(availableGeometry.top());
                }

                resize(restoredSize);
                move(restoredPosition);
                minimizeButton->setRestoreMode(false);
                minimizeButton->setToolTip(QStringLiteral("접기"));
            }
        });
    connect(addButton, &QPushButton::clicked, this, [this]() {
        if (addTaskHandler_)
        {
            addTaskHandler_();
        }
    });
    connect(
        table_->selectionModel(),
        &QItemSelectionModel::selectionChanged,
        this,
        [completeButton, this]() {
            completeButton->setEnabled(
                table_->selectionModel()->hasSelection());
        });
    connect(completeButton, &QPushButton::clicked, this, [this, proxyModel]() {
        const QModelIndex proxyIndex = table_->currentIndex();
        if (proxyIndex.isValid() && completeTaskHandler_)
        {
            completeTaskHandler_(
                proxyModel->mapToSource(proxyIndex).row());
        }
    });
    connect(
        table_,
        &QWidget::customContextMenuRequested,
        this,
        [this, proxyModel](const QPoint& position) {
            const QModelIndex proxyIndex = table_->indexAt(position);
            if (!proxyIndex.isValid())
            {
                return;
            }

            table_->selectRow(proxyIndex.row());
            QMenu menu(this);
            QAction* detailsAction =
                menu.addAction(QStringLiteral("자세히 보기"));
            QAction* editAction =
                menu.addAction(QStringLiteral("수정하기"));
            QAction* selectedAction =
                menu.exec(table_->viewport()->mapToGlobal(position));
            if (selectedAction == nullptr || !taskActionHandler_)
            {
                return;
            }

            const int sourceRow =
                proxyModel->mapToSource(proxyIndex).row();
            taskActionHandler_(sourceRow, selectedAction == editAction);
        });
    connect(
        table_,
        &QTableView::doubleClicked,
        this,
        [this, proxyModel](const QModelIndex& proxyIndex) {
            if (proxyIndex.isValid() && taskActionHandler_)
            {
                taskActionHandler_(
                    proxyModel->mapToSource(proxyIndex).row(),
                    false);
            }
        });
}

void QueueFloatingWidget::setVisibilityChangedHandler(
    std::function<void(bool)> handler)
{
    visibilityChangedHandler_ = std::move(handler);
}

void QueueFloatingWidget::setTaskActionHandler(
    std::function<void(int, bool)> handler)
{
    taskActionHandler_ = std::move(handler);
}

void QueueFloatingWidget::setAddTaskHandler(std::function<void()> handler)
{
    addTaskHandler_ = std::move(handler);
}

void QueueFloatingWidget::setCompleteTaskHandler(
    std::function<void(int)> handler)
{
    completeTaskHandler_ = std::move(handler);
}

void QueueFloatingWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    ensureVisibleOnScreen();
    if (visibilityChangedHandler_)
    {
        visibilityChangedHandler_(true);
    }
}

void QueueFloatingWidget::ensureVisibleOnScreen()
{
    QDesktopWidget* desktop = QApplication::desktop();
    if (desktop == nullptr || desktop->screenCount() <= 0)
    {
        return;
    }

    const QRect currentGeometry = geometry();
    int targetScreen = -1;
    qint64 largestVisibleArea = 0;
    for (int screen = 0; screen < desktop->screenCount(); ++screen)
    {
        const QRect intersection =
            currentGeometry.intersected(desktop->screenGeometry(screen));
        const qint64 visibleArea =
            static_cast<qint64>(intersection.width()) *
            intersection.height();
        if (visibleArea > largestVisibleArea)
        {
            largestVisibleArea = visibleArea;
            targetScreen = screen;
        }
    }

    if (targetScreen < 0)
    {
        targetScreen = desktop->primaryScreen();
    }

    const QRect available = desktop->availableGeometry(targetScreen);
    if (!available.isValid())
    {
        return;
    }

    const QSize visibleSize(
        qMin(width(), available.width()),
        qMin(height(), available.height()));
    if (visibleSize != size())
    {
        resize(visibleSize);
    }

    QPoint visiblePosition = pos();
    const int maximumX =
        available.right() - width() + 1;
    const int maximumY =
        available.bottom() - height() + 1;
    visiblePosition.setX(
        maximumX < available.left()
            ? available.left()
            : qBound(
                  available.left(),
                  visiblePosition.x(),
                  maximumX));
    visiblePosition.setY(
        maximumY < available.top()
            ? available.top()
            : qBound(
                  available.top(),
                  visiblePosition.y(),
                  maximumY));
    if (visiblePosition != pos())
    {
        move(visiblePosition);
    }
}

void QueueFloatingWidget::closeEvent(QCloseEvent* event)
{
    if (visibilityChangedHandler_)
    {
        visibilityChangedHandler_(false);
    }
    QWidget::closeEvent(event);
}

void QueueFloatingWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (sizeGrip_ != nullptr)
    {
        sizeGrip_->move(width() - sizeGrip_->width(), height() - 24);
        sizeGrip_->raise();
    }
}

bool QueueFloatingWidget::nativeEvent(
    const QByteArray& eventType,
    void* message,
    long* result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType);

    MSG* nativeMessage = static_cast<MSG*>(message);
    if (nativeMessage != nullptr &&
        nativeMessage->message == WM_NCHITTEST &&
        !isMaximized())
    {
        RECT windowRect;
        POINT cursorPosition;
        if (GetWindowRect(
                reinterpret_cast<HWND>(winId()),
                &windowRect) &&
            GetCursorPos(&cursorPosition))
        {
            constexpr LONG resizeBorder = 10;
            const bool left =
                cursorPosition.x < windowRect.left + resizeBorder;
            const bool right =
                cursorPosition.x >= windowRect.right - resizeBorder;
            const bool top =
                cursorPosition.y < windowRect.top + resizeBorder;
            const bool bottom =
                cursorPosition.y >= windowRect.bottom - resizeBorder;

            if (top && left)
            {
                *result = HTTOPLEFT;
                return true;
            }
            if (top && right)
            {
                *result = HTTOPRIGHT;
                return true;
            }
            if (bottom && left)
            {
                *result = HTBOTTOMLEFT;
                return true;
            }
            if (bottom && right)
            {
                *result = HTBOTTOMRIGHT;
                return true;
            }
            if (left)
            {
                *result = HTLEFT;
                return true;
            }
            if (right)
            {
                *result = HTRIGHT;
                return true;
            }
            if (top)
            {
                *result = HTTOP;
                return true;
            }
            if (bottom)
            {
                *result = HTBOTTOM;
                return true;
            }
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif

    return QWidget::nativeEvent(eventType, message, result);
}

bool QueueFloatingWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != dragHandle_)
    {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseButtonPress)
    {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            dragging_ = true;
            dragOffset_ = mouseEvent->globalPos() - frameGeometry().topLeft();
            return true;
        }
    }
    else if (event->type() == QEvent::MouseMove && dragging_)
    {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        move(mouseEvent->globalPos() - dragOffset_);
        return true;
    }
    else if (event->type() == QEvent::MouseButtonRelease)
    {
        dragging_ = false;
        return true;
    }

    return QWidget::eventFilter(watched, event);
}
