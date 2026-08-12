#pragma once

#include <QtCore/QString>
#include <QtGui/QIcon>

class QApplication;

class StyleManager final
{
public:
    enum class Theme
    {
        Light,
        Dark
    };

    static StyleManager& instance();

    void apply(QApplication& application) const;
    void setTheme(QApplication& application, Theme theme) const;
    Theme currentTheme() const;
    const QIcon& applicationIcon() const;

    StyleManager(const StyleManager&) = delete;
    StyleManager& operator=(const StyleManager&) = delete;

private:
    StyleManager() = default;

    QString styleSheet(Theme theme) const;
    mutable QIcon applicationIcon_;
};
