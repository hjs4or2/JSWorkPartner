#include "TaskQueueViewModel.h"

#include <QtCore/QDate>
#include <QtCore/QDateTime>
#include <QtCore/QUrl>
#include <QtCore/QVariant>
#include <QtGui/QBrush>
#include <QtGui/QColor>

#include "TaskRepository.h"

TaskQueueViewModel::TaskQueueViewModel(
    TaskRepository& repository,
    QObject* parent)
    : QAbstractTableModel(parent),
      repository_(repository)
{
}

int TaskQueueViewModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : tasks_.size();
}

int TaskQueueViewModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant TaskQueueViewModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= tasks_.size())
    {
        return QVariant();
    }

    const Task& task = tasks_.at(index.row());

    if (!showingCompleted_ &&
        index.column() == DeadlineColumn &&
        !task.deadline.isEmpty())
    {
        const QDate deadline = QDate::fromString(task.deadline, Qt::ISODate);
        if (deadline.isValid())
        {
            const int daysRemaining = QDate::currentDate().daysTo(deadline);

            if (role == Qt::BackgroundRole)
            {
                if (daysRemaining < 0)
                {
                    return QBrush(QColor(QStringLiteral("#DC4C4C")));
                }
                if (daysRemaining <= 7)
                {
                    // 7일 전부터 매일 붉은색 농도가 한 단계씩 진해집니다.
                    const int urgency = 7 - daysRemaining;
                    const int red = 255 - (16 * urgency / 7);
                    const int greenBlue = 241 - (130 * urgency / 7);
                    return QBrush(QColor(red, greenBlue, greenBlue));
                }
            }
            else if (role == Qt::ForegroundRole && daysRemaining < 0)
            {
                return QBrush(QColor(QStringLiteral("#FFFFFF")));
            }
            else if (role == Qt::ToolTipRole)
            {
                if (daysRemaining < 0)
                {
                    return QStringLiteral("마감일이 %1일 지났습니다.")
                        .arg(-daysRemaining);
                }
                if (daysRemaining == 0)
                {
                    return QStringLiteral("오늘이 마감일입니다.");
                }
                return QStringLiteral("마감일까지 %1일 남았습니다.")
                    .arg(daysRemaining);
            }
        }
    }

    if (role == Qt::TextAlignmentRole)
    {
        if (index.column() != TitleColumn &&
            index.column() != DetailsColumn)
        {
            return static_cast<int>(Qt::AlignCenter);
        }
        return QVariant();
    }

    if (role != Qt::DisplayRole)
    {
        return QVariant();
    }

    switch (index.column())
    {
    case SequenceColumn:
        return task.sequence;
    case TitleColumn:
        return task.title;
    case DetailsColumn:
        return task.details.isEmpty() ? QStringLiteral("-") : task.details;
    case ImportanceColumn:
        return task.importance;
    case DeadlineColumn:
        return task.deadline.isEmpty() ? QStringLiteral("X") : task.deadline;
    case DateColumn:
    {
        const QDateTime date = showingCompleted_ ? task.completedAt : task.createdAt;
        return date.isValid()
                   ? date.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                   : QStringLiteral("-");
    }
    default:
        return QVariant();
    }
}

QVariant TaskQueueViewModel::headerData(
    int section,
    Qt::Orientation orientation,
    int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    switch (section)
    {
    case SequenceColumn:
        return QStringLiteral("접수 순번");
    case TitleColumn:
        return QStringLiteral("제목");
    case DetailsColumn:
        return QStringLiteral("상세 설명");
    case ImportanceColumn:
        return QStringLiteral("중요도");
    case DeadlineColumn:
        return QStringLiteral("마감일");
    case DateColumn:
        return showingCompleted_
                   ? QStringLiteral("완료 시각")
                   : QStringLiteral("접수 시각");
    default:
        return QVariant();
    }
}

bool TaskQueueViewModel::loadPendingTasks()
{
    return reload(false);
}

bool TaskQueueViewModel::loadCompletedTasks()
{
    return reload(true);
}

bool TaskQueueViewModel::addTask(
    const QString& title,
    const QString& details,
    const QString& externalUrl,
    int importance,
    const QString& deadline)
{
    const QString normalizedTitle = title.trimmed();
    const QString normalizedDetails = details.trimmed();
    const QString normalizedExternalUrl = externalUrl.trimmed();
    if (normalizedTitle.isEmpty())
    {
        lastError_ = QStringLiteral("업무 제목을 입력해 주세요.");
        return false;
    }

    if (importance < 1 || importance > 5)
    {
        lastError_ = QStringLiteral("중요도는 1부터 5 사이여야 합니다.");
        return false;
    }

    if (repository_.titleExists(normalizedTitle))
    {
        lastError_ = repository_.lastError().isEmpty()
                         ? QStringLiteral(
                               "같은 제목의 업무가 이미 있습니다. 다른 제목을 입력해 주세요.")
                         : repository_.lastError();
        return false;
    }

    if (!normalizedExternalUrl.isEmpty())
    {
        const QUrl url(normalizedExternalUrl);
        if (!url.isValid() ||
            url.host().isEmpty() ||
            (url.scheme() != QStringLiteral("http") &&
             url.scheme() != QStringLiteral("https")))
        {
            lastError_ = QStringLiteral(
                "외부 링크는 http:// 또는 https://로 시작하는 URL이어야 합니다.");
            return false;
        }
    }

    Task createdTask;
    if (!repository_.addTask(
            normalizedTitle,
            normalizedDetails,
            normalizedExternalUrl,
            importance,
            deadline,
            &createdTask))
    {
        lastError_ = repository_.lastError();
        return false;
    }

    showingCompleted_ = false;
    if (!reload(false))
    {
        return false;
    }

    lastMessage_ = QStringLiteral("%1번 업무가 저장되어 Queue에 추가되었습니다.")
                       .arg(createdTask.sequence);
    lastError_.clear();
    return true;
}

bool TaskQueueViewModel::updateTask(
    int row,
    const QString& title,
    const QString& details,
    const QString& externalUrl,
    int importance,
    const QString& deadline)
{
    if (row < 0 || row >= tasks_.size())
    {
        lastError_ = QStringLiteral("수정할 업무를 선택해 주세요.");
        return false;
    }

    const QString normalizedTitle = title.trimmed();
    const QString normalizedExternalUrl = externalUrl.trimmed();
    const Task task = tasks_.at(row);
    if (normalizedTitle.isEmpty())
    {
        lastError_ = QStringLiteral("업무 제목을 입력해 주세요.");
        return false;
    }

    if (importance < 1 || importance > 5)
    {
        lastError_ = QStringLiteral("중요도는 1부터 5 사이여야 합니다.");
        return false;
    }

    if (repository_.titleExists(normalizedTitle, task.id))
    {
        lastError_ = repository_.lastError().isEmpty()
                         ? QStringLiteral(
                               "같은 제목의 업무가 이미 있습니다. 다른 제목을 입력해 주세요.")
                         : repository_.lastError();
        return false;
    }

    if (!normalizedExternalUrl.isEmpty())
    {
        const QUrl url(normalizedExternalUrl);
        if (!url.isValid() ||
            url.host().isEmpty() ||
            (url.scheme() != QStringLiteral("http") &&
             url.scheme() != QStringLiteral("https")))
        {
            lastError_ = QStringLiteral(
                "외부 링크는 http:// 또는 https://로 시작하는 URL이어야 합니다.");
            return false;
        }
    }

    if (!repository_.updateTask(
            task.id,
            normalizedTitle,
            details.trimmed(),
            normalizedExternalUrl,
            importance,
            deadline))
    {
        lastError_ = repository_.lastError();
        return false;
    }

    if (!reload(showingCompleted_))
    {
        return false;
    }

    lastMessage_ = QStringLiteral("%1번 업무를 수정했습니다.")
                       .arg(task.sequence);
    lastError_.clear();
    return true;
}

bool TaskQueueViewModel::completeTask(int row)
{
    if (showingCompleted_)
    {
        lastError_ = QStringLiteral("완료 목록의 업무는 다시 완료 처리할 수 없습니다.");
        return false;
    }

    if (row < 0 || row >= tasks_.size())
    {
        lastError_ = QStringLiteral("완료할 업무를 선택해 주세요.");
        return false;
    }

    const Task task = tasks_.at(row);
    if (!repository_.completeTask(task.id))
    {
        lastError_ = repository_.lastError();
        return false;
    }

    if (!reload(false))
    {
        return false;
    }

    lastMessage_ = QStringLiteral("%1번 업무를 완료 처리했습니다.")
                       .arg(task.sequence);
    lastError_.clear();
    return true;
}

bool TaskQueueViewModel::deleteTask(int row)
{
    if (row < 0 || row >= tasks_.size())
    {
        lastError_ = QStringLiteral("삭제할 업무를 선택해 주세요.");
        return false;
    }

    const Task task = tasks_.at(row);
    if (!repository_.deleteTask(task.id))
    {
        lastError_ = repository_.lastError();
        return false;
    }

    if (!reload(showingCompleted_))
    {
        return false;
    }

    lastMessage_ = QStringLiteral("%1번 업무를 삭제했습니다.")
                       .arg(task.sequence);
    lastError_.clear();
    return true;
}

Task TaskQueueViewModel::taskAt(int row) const
{
    return row >= 0 && row < tasks_.size() ? tasks_.at(row) : Task();
}

bool TaskQueueViewModel::isShowingCompleted() const
{
    return showingCompleted_;
}

int TaskQueueViewModel::taskCount() const
{
    return tasks_.size();
}

QString TaskQueueViewModel::lastMessage() const
{
    return lastMessage_;
}

QString TaskQueueViewModel::lastError() const
{
    return lastError_;
}

QString TaskQueueViewModel::databasePath() const
{
    return repository_.databasePath();
}

void TaskQueueViewModel::refreshDeadlineIndicators()
{
    if (!showingCompleted_ && !tasks_.isEmpty())
    {
        emit dataChanged(
            index(0, 0),
            index(tasks_.size() - 1, ColumnCount - 1));
    }
}

bool TaskQueueViewModel::reload(bool showCompleted)
{
    beginResetModel();
    showingCompleted_ = showCompleted;
    tasks_ = repository_.loadTasks(showCompleted);
    endResetModel();

    if (!repository_.lastError().isEmpty())
    {
        lastError_ = repository_.lastError();
        return false;
    }

    lastError_.clear();
    return true;
}
