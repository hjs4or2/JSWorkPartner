#pragma once

#include <QtWidgets/QStyle>
#include <QtWidgets/QStyledItemDelegate>

class DeadlineColorDelegate final : public QStyledItemDelegate
{
public:
    explicit DeadlineColorDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(
        QPainter* painter,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QStyleOptionViewItem deadlineOption(option);
        if ((deadlineOption.state & QStyle::State_Selected) &&
            index.data(Qt::BackgroundRole).isValid())
        {
            deadlineOption.state &= ~QStyle::State_Selected;
        }

        QStyledItemDelegate::paint(painter, deadlineOption, index);
    }
};
