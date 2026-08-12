#include "StyleManager.h"

#include <array>

#include <QtCore/QSettings>
#include <QtGui/QFont>
#include <QtGui/QPainter>
#include <QtGui/QPen>
#include <QtGui/QPixmap>
#include <QtWidgets/QApplication>
#include <QtWidgets/QStyleFactory>

StyleManager& StyleManager::instance()
{
    static StyleManager manager;
    return manager;
}

void StyleManager::apply(QApplication& application) const
{
    if (QStyle* fusionStyle = QStyleFactory::create(QStringLiteral("Fusion")))
    {
        application.setStyle(fusionStyle);
    }

    application.setFont(QFont(QStringLiteral("Malgun Gothic"), 10));
    application.setStyleSheet(styleSheet(currentTheme()));
    application.setWindowIcon(applicationIcon());
}

void StyleManager::setTheme(
    QApplication& application,
    Theme theme) const
{
    QSettings settings;
    settings.setValue(
        QStringLiteral("appearance/theme"),
        theme == Theme::Dark
            ? QStringLiteral("dark")
            : QStringLiteral("light"));
    application.setStyleSheet(styleSheet(theme));
}

StyleManager::Theme StyleManager::currentTheme() const
{
    QSettings settings;
    return settings.value(
                       QStringLiteral("appearance/theme"),
                       QStringLiteral("light"))
                   .toString() == QStringLiteral("dark")
               ? Theme::Dark
               : Theme::Light;
}

const QIcon& StyleManager::applicationIcon() const
{
    if (!applicationIcon_.isNull())
    {
        return applicationIcon_;
    }

    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#2563EB")));
    painter.drawRoundedRect(QRectF(4, 4, 56, 56), 14, 14);

    // 작은 트레이 아이콘에서도 퀘스트 앱임을 알아볼 수 있는 체크리스트입니다.
    painter.setBrush(QColor(QStringLiteral("#FFFFFF")));
    painter.drawRoundedRect(QRectF(13, 11, 38, 43), 6, 6);

    painter.setBrush(QColor(QStringLiteral("#1D4ED8")));
    painter.drawRoundedRect(QRectF(23, 8, 18, 8), 4, 4);

    QPen checkPen(QColor(QStringLiteral("#2563EB")), 3.2);
    checkPen.setCapStyle(Qt::RoundCap);
    checkPen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(checkPen);
    painter.setBrush(Qt::NoBrush);

    const std::array<qreal, 3> rowCenters{{25.0, 35.0, 45.0}};
    for (qreal y : rowCenters)
    {
        painter.drawLine(QPointF(19, y), QPointF(22, y + 3));
        painter.drawLine(QPointF(22, y + 3), QPointF(27, y - 3));
        painter.drawLine(QPointF(32, y), QPointF(44, y));
    }

    applicationIcon_ = QIcon(pixmap);
    return applicationIcon_;
}

QString StyleManager::styleSheet(Theme theme) const
{
    // 색상과 간격은 이 QSS만 수정하면 프로그램 전체에 적용됩니다.
    QString sheet = QStringLiteral(R"QSS(
        QWidget {
            background-color: #F4F7FB;
            color: #172033;
            font-size: 10pt;
        }

        QWidget#leftPanel,
        QWidget#rightPanel {
            background-color: #FFFFFF;
            border: 1px solid #DCE3EE;
            border-radius: 12px;
        }

        QWidget#queueGroup {
            background-color: #F8FAFD;
            border: 1px solid #D5DEEA;
            border-radius: 10px;
        }

        QWidget#weeklySummaryCard {
            background-color: #FFF8F8;
            border: 1px solid #F3CACA;
            border-radius: 10px;
        }

        QWidget#floatingQueueWidget {
            background-color: transparent;
            border: none;
        }

        QWidget#floatingQueueCard {
            background-color: #FFFFFF;
            border: 1px solid #C8D3E1;
            border-radius: 16px;
        }

        QWidget#floatingQueueHeader {
            background-color: transparent;
        }

        QSizeGrip {
            background-color: transparent;
        }

        QToolButton#floatingQueueCloseButton {
            background-color: transparent;
            border: none;
            border-radius: 6px;
            padding: 0;
        }

        QToolButton#floatingQueueMinimizeButton {
            background-color: transparent;
            border: none;
            border-radius: 6px;
            padding: 0;
        }

        QToolButton#floatingQueueMinimizeButton:hover {
            background-color: #DBEAFE;
        }

        QToolButton#floatingQueueMinimizeButton:pressed {
            background-color: #BFDBFE;
        }

        QToolButton#floatingQueueCloseButton:hover {
            background-color: #FEE2E2;
        }

        QToolButton#floatingQueueCloseButton:pressed {
            background-color: #FECACA;
        }

        QLabel {
            background: transparent;
        }

        QLineEdit,
        QTextEdit,
        QDateEdit {
            background-color: #FFFFFF;
            border: 1px solid #C9D3E1;
            border-radius: 7px;
            padding: 7px 9px;
            selection-background-color: #BFDBFE;
        }

        QLineEdit:focus,
        QTextEdit:focus,
        QDateEdit:focus {
            border: 2px solid #3B82F6;
            padding: 6px 8px;
        }

        QPushButton {
            background-color: #EFF4FA;
            border: 1px solid #C9D3E1;
            border-radius: 7px;
            padding: 7px 14px;
        }

        QPushButton:hover {
            background-color: #E3ECF7;
            border-color: #94A3B8;
        }

        QPushButton:pressed {
            background-color: #D5E1EF;
        }

        QPushButton:disabled {
            background-color: #F5F7FA;
            color: #9AA7B8;
            border-color: #E1E6ED;
        }

        QPushButton#primaryButton {
            background-color: #2563EB;
            color: #FFFFFF;
            border-color: #2563EB;
            font-weight: 600;
        }

        QPushButton#primaryButton:hover {
            background-color: #1D4ED8;
        }

        QPushButton#successButton {
            background-color: #0F9F6E;
            color: #FFFFFF;
            border-color: #0F9F6E;
            font-weight: 600;
        }

        QPushButton#successButton:hover {
            background-color: #0B815A;
        }

        QPushButton[importanceStar="true"] {
            background: transparent;
            border: none;
            color: #8A8F98;
            font-size: 19px;
            padding: 0;
        }

        QPushButton[importanceStar="true"]:hover,
        QPushButton[starSelected="true"] {
            color: #F4A300;
        }

        QTabWidget::pane {
            background-color: #FFFFFF;
            border: 1px solid #D5DEEA;
            border-radius: 8px;
            top: -1px;
        }

        QTabBar::tab {
            background-color: #EAF0F7;
            color: #526174;
            border: 1px solid #D5DEEA;
            padding: 9px 18px;
            margin-right: 3px;
            border-top-left-radius: 7px;
            border-top-right-radius: 7px;
        }

        QTabBar::tab:selected {
            background-color: #FFFFFF;
            color: #1D4ED8;
            font-weight: 600;
            border-bottom-color: #FFFFFF;
        }

        QHeaderView::section {
            background-color: #E8F1FC;
            color: #24324A;
            border: none;
            border-right: 1px solid #D1DCE9;
            border-bottom: 1px solid #C5D2E2;
            padding: 7px 6px;
            font-weight: 600;
        }

        QTableView {
            background-color: #FFFFFF;
            alternate-background-color: #F8FAFD;
            border: 1px solid #C8D3E1;
            border-radius: 7px;
            gridline-color: #E2E8F0;
            selection-background-color: #DBEAFE;
            selection-color: #172033;
        }

        QTableView::item {
            padding: 6px;
        }

        QTableView#floatingQueueTable::item {
            padding: 2px 5px;
        }

        QTableView#floatingQueueTable QHeaderView::section {
            padding: 3px 5px;
        }

        QToolButton {
            background: transparent;
            border: 1px solid #C8D3E1;
            border-radius: 5px;
            color: #64748B;
            font-size: 17px;
            font-weight: 600;
        }

        QToolButton:hover {
            background-color: #E8F1FC;
            color: #1D4ED8;
            border-color: #93B4E8;
        }

        QToolButton:pressed {
            background-color: #DBEAFE;
        }

        QSlider::groove:horizontal {
            background-color: #D5DEEA;
            height: 6px;
            border-radius: 3px;
        }

        QSlider::sub-page:horizontal {
            background-color: #3B82F6;
            border-radius: 3px;
        }

        QSlider::handle:horizontal {
            background-color: #2563EB;
            border: 2px solid #FFFFFF;
            width: 16px;
            margin: -6px 0;
            border-radius: 9px;
        }

        QMenu {
            background-color: #FFFFFF;
            color: #172033;
            border: 1px solid #C8D3E1;
            border-radius: 7px;
            padding: 5px;
        }

        QMenu::item {
            background-color: transparent;
            border-radius: 5px;
            padding: 7px 28px 7px 12px;
            margin: 1px 0;
        }

        QMenu::item:selected {
            background-color: #DBEAFE;
            color: #1D4ED8;
        }

        QMenu::item:pressed {
            background-color: #BFDBFE;
            color: #1E40AF;
        }

        QMenu::item:disabled {
            color: #9AA7B8;
        }

        QMenu::separator {
            background-color: #DCE3EE;
            height: 1px;
            margin: 5px 8px;
        }

        QToolTip {
            background-color: #172033;
            color: #FFFFFF;
            border: 1px solid #334155;
            padding: 7px;
        }

        QSplitter::handle {
            background-color: #DCE3EE;
            width: 4px;
            margin: 8px 2px;
        }

        QScrollBar:vertical {
            background: #F1F5F9;
            width: 11px;
            margin: 0;
        }

        QScrollBar::handle:vertical {
            background: #AEBBCD;
            min-height: 28px;
            border-radius: 5px;
        }

        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical {
            height: 0;
        }
    )QSS");

    if (theme == Theme::Dark)
    {
        sheet.append(QStringLiteral(R"QSS(
            QWidget {
                background-color: #0F172A;
                color: #E5E7EB;
            }

            QWidget#leftPanel,
            QWidget#rightPanel,
            QWidget#floatingQueueCard {
                background-color: #182235;
                border-color: #334155;
            }

            QWidget#queueGroup {
                background-color: #141E2E;
                border-color: #334155;
            }

            QWidget#weeklySummaryCard {
                background-color: #291D2A;
                border-color: #713F4A;
            }

            QWidget#floatingQueueWidget,
            QWidget#floatingQueueHeader,
            QLabel {
                background-color: transparent;
            }

            QLineEdit,
            QTextEdit,
            QPlainTextEdit,
            QDateEdit {
                background-color: #111827;
                color: #E5E7EB;
                border-color: #475569;
                selection-background-color: #1D4ED8;
            }

            QPushButton {
                background-color: #263449;
                color: #E5E7EB;
                border-color: #475569;
            }

            QPushButton:hover {
                background-color: #334155;
                border-color: #64748B;
            }

            QPushButton:pressed {
                background-color: #1E293B;
            }

            QPushButton:disabled {
                background-color: #182235;
                color: #64748B;
                border-color: #334155;
            }

            QTabWidget::pane {
                background-color: #182235;
                border-color: #334155;
            }

            QTabBar::tab {
                background-color: #1E293B;
                color: #94A3B8;
                border-color: #334155;
            }

            QTabBar::tab:selected {
                background-color: #182235;
                color: #93C5FD;
                border-bottom-color: #182235;
            }

            QHeaderView::section {
                background-color: #1E3A5F;
                color: #E2E8F0;
                border-right-color: #334E68;
                border-bottom-color: #475569;
            }

            QTableView {
                background-color: #111827;
                alternate-background-color: #172033;
                color: #E5E7EB;
                border-color: #475569;
                gridline-color: #334155;
                selection-background-color: #1E40AF;
                selection-color: #FFFFFF;
            }

            QToolButton {
                color: #94A3B8;
                border-color: #475569;
            }

            QToolButton:hover {
                background-color: #263449;
                color: #93C5FD;
                border-color: #64748B;
            }

            QToolButton#floatingQueueMinimizeButton:hover {
                background-color: #263449;
            }

            QToolButton#floatingQueueMinimizeButton:pressed {
                background-color: #1E3A5F;
            }

            QSlider::groove:horizontal {
                background-color: #334155;
            }

            QSlider::handle:horizontal {
                border-color: #182235;
            }

            QMenu {
                background-color: #182235;
                color: #E5E7EB;
                border-color: #475569;
            }

            QMenu::item:selected {
                background-color: #1E40AF;
                color: #FFFFFF;
            }

            QMenu::separator,
            QSplitter::handle {
                background-color: #334155;
            }

            QToolTip {
                background-color: #020617;
                color: #F8FAFC;
                border-color: #475569;
            }

            QScrollBar:vertical {
                background: #111827;
            }

            QScrollBar::handle:vertical {
                background: #475569;
            }
        )QSS"));
    }

    return sheet;
}
