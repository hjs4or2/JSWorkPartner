#include "NullableDateEdit.h"

#include <QtGui/QKeyEvent>
#include <QtWidgets/QCalendarWidget>
#include <QtWidgets/QLineEdit>

NullableDateEdit::NullableDateEdit(QWidget* parent)
    : QDateEdit(QDate::currentDate(), parent)
{
    setCalendarPopup(true);
    setDisplayFormat(QStringLiteral("yyyy-MM-dd"));

    connect(this, &QDateEdit::dateChanged, this, [this](const QDate&) {
        if (!updating_)
        {
            hasDate_ = true;
            refreshText();
        }
    });
    connect(calendarWidget(), &QCalendarWidget::clicked, this, [this](const QDate&) {
        hasDate_ = true;
        refreshText();
    });

    clearDate();
}

bool NullableDateEdit::hasDate() const
{
    return hasDate_;
}

QDate NullableDateEdit::nullableDate() const
{
    return hasDate_ ? date() : QDate();
}

void NullableDateEdit::setNullableDate(const QDate& date)
{
    if (!date.isValid())
    {
        clearDate();
        return;
    }

    updating_ = true;
    hasDate_ = true;
    setDate(date);
    updating_ = false;
    refreshText();
}

void NullableDateEdit::clearDate()
{
    updating_ = true;
    hasDate_ = false;
    setDate(QDate::currentDate());
    updating_ = false;
    refreshText();
}

QString NullableDateEdit::textFromDateTime(const QDateTime& dateTime) const
{
    return hasDate_
               ? QDateEdit::textFromDateTime(dateTime)
               : QStringLiteral("마감일 없음");
}

void NullableDateEdit::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete ||
        event->key() == Qt::Key_Backspace)
    {
        clearDate();
        event->accept();
        return;
    }

    QDateEdit::keyPressEvent(event);
}

void NullableDateEdit::refreshText()
{
    lineEdit()->setText(textFromDateTime(dateTime()));
}
