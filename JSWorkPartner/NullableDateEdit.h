#pragma once

#include <QtWidgets/QDateEdit>

class QKeyEvent;

class NullableDateEdit final : public QDateEdit
{
public:
    explicit NullableDateEdit(QWidget* parent = nullptr);

    bool hasDate() const;
    QDate nullableDate() const;
    void setNullableDate(const QDate& date);
    void clearDate();

protected:
    QString textFromDateTime(const QDateTime& dateTime) const override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void refreshText();

    bool hasDate_ = false;
    bool updating_ = false;
};
