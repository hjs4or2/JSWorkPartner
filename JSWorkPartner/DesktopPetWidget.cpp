#include "DesktopPetWidget.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>
#include <QtGui/QScreen>
#include <QtGui/QPainter>
#include <QtGui/QMouseEvent>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QShowEvent>
#include <QtGui/QHideEvent>
#include <QtCore/QtMath>
#include <cmath>
#include <limits>

#include "AppConstants.h"

DesktopPetWidget::DesktopPetWidget(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
{
    setSizePercent(50);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_QuitOnClose, false);
    setFocusPolicy(Qt::NoFocus);
    setDisplayName(AppConstants::PetDefaultName);
    animation_.setInterval(40);
    connect(&animation_, &QTimer::timeout, this, [this] { frame_ = (frame_ + 1) % 10000; update(); });
    const auto constrain = [this] { ensureVisibleOnScreen(); };
    for (auto* screen : QGuiApplication::screens())
        connect(screen, &QScreen::availableGeometryChanged, this, constrain);
    connect(qApp, &QGuiApplication::screenAdded, this, [this, constrain](QScreen* screen) {
        connect(screen, &QScreen::availableGeometryChanged, this, constrain); constrain();
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, constrain);
}
void DesktopPetWidget::setBusy(bool value) { busy_ = value; update(); }
void DesktopPetWidget::setPendingCount(int value) { pendingCount_ = qMax(0, value); update(); }
void DesktopPetWidget::setDisplayName(const QString& value)
{
    displayName_ = AppConstants::normalizedPetName(value);
    setAccessibleName(QStringLiteral("%1 개인비서").arg(displayName_));
    setToolTip(QStringLiteral("<span>%1 · 클릭해서 대화 · 드래그해서 이동 · 우클릭해서 업무 메뉴</span>")
                   .arg(displayName_.toHtmlEscaped()));
}
int DesktopPetWidget::normalizedSizePercent(int value)
{
    static const int supported[] = {25, 50, 75, 100, 125, 150};
    for (const int size : supported)
        if (size == value) return value;
    return 50;
}
void DesktopPetWidget::setSizePercent(int value)
{
    const int normalized = normalizedSizePercent(value);
    const qreal scale = normalized / 100.0;
    const QSize target(qRound(144 * scale), qRound(150 * scale));
    if (sizePercent_ == normalized && size() == target) { ensureVisibleOnScreen(); return; }
    const QPoint previousPosition = pos();
    sizePercent_ = normalized;
    setFixedSize(target);
    restorePosition(previousPosition);
    update();
}
void DesktopPetWidget::setAlwaysOnTop(bool value)
{
    Qt::WindowFlags flags = windowFlags();
    if (flags.testFlag(Qt::WindowStaysOnTopHint) == value) return;
    const bool wasVisible = isVisible();
    const QPoint previousPosition = pos();
    if (value) flags |= Qt::WindowStaysOnTopHint;
    else flags &= ~Qt::WindowStaysOnTopHint;
    setWindowFlags(flags);
    move(previousPosition);
    if (wasVisible) { show(); raise(); }
    else hide();
    ensureVisibleOnScreen();
}
QPoint DesktopPetWidget::boundedPosition(const QPoint& position) const
{
    QRect target;
    qint64 distance = std::numeric_limits<qint64>::max();
    const QPoint center = position + rect().center();
    for (auto* screen : QGuiApplication::screens()) {
        const QRect area = screen->availableGeometry();
        const QPoint nearest(qBound(area.left(), center.x(), area.right()), qBound(area.top(), center.y(), area.bottom()));
        const qint64 dx = qint64(center.x()) - nearest.x(), dy = qint64(center.y()) - nearest.y();
        if (dx * dx + dy * dy < distance) { distance = dx * dx + dy * dy; target = area; }
    }
    if (!target.isValid()) return position;
    return QPoint(qBound(target.left(), position.x(), qMax(target.left(), target.right() - width() + 1)),
                  qBound(target.top(), position.y(), qMax(target.top(), target.bottom() - height() + 1)));
}
void DesktopPetWidget::restorePosition(const QPoint& value)
{
    const QPoint next = boundedPosition(value);
    if (next != pos()) { move(next); emit positionChanged(next); }
}
void DesktopPetWidget::ensureVisibleOnScreen() { restorePosition(pos()); }
void DesktopPetWidget::showEvent(QShowEvent* e) { QWidget::showEvent(e); ensureVisibleOnScreen(); animation_.start(); }
void DesktopPetWidget::hideEvent(QHideEvent* e) { animation_.stop(); pressed_ = dragging_ = false; QWidget::hideEvent(e); }
void DesktopPetWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) { pressed_ = true; dragging_ = false; pressPosition_ = e->globalPos(); dragOffset_ = e->globalPos() - pos(); e->accept(); }
}
void DesktopPetWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (!pressed_) return;
    if ((e->globalPos() - pressPosition_).manhattanLength() >= QApplication::startDragDistance()) dragging_ = true;
    if (dragging_) restorePosition(e->globalPos() - dragOffset_);
}
void DesktopPetWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !pressed_) return;
    const bool click = !dragging_ && (e->globalPos() - pressPosition_).manhattanLength() < QApplication::startDragDistance();
    pressed_ = dragging_ = false;
    if (click) emit assistantRequested();
}
void DesktopPetWidget::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu menu(this);
    connect(menu.addAction(QStringLiteral("업무 추가")), &QAction::triggered, this, &DesktopPetWidget::newTaskRequested);
    connect(menu.addAction(QStringLiteral("할 일")), &QAction::triggered, this, &DesktopPetWidget::tasksRequested);
    connect(menu.addAction(QStringLiteral("주간보고서")), &QAction::triggered, this, &DesktopPetWidget::weeklyReportRequested);
    menu.addSeparator();
    connect(menu.addAction(QStringLiteral("로봇 설정...")), &QAction::triggered, this, &DesktopPetWidget::settingsRequested);
    menu.addSeparator();
    connect(menu.addAction(QStringLiteral("로봇 숨기기")), &QAction::triggered, this, &DesktopPetWidget::hideRequested);
    menu.exec(e->globalPos());
}
void DesktopPetWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.scale(sizePercent_ / 100.0, sizePercent_ / 100.0);
    p.setPen(Qt::NoPen);
    for (int i = 6; i > 0; --i) { p.setBrush(QColor(22, 50, 58, 4)); p.drawEllipse(QRectF(25-i, 131-i/2., 94+i*2, 9+i)); }
    p.translate(0, std::sin(frame_ * .045) * 1.5);
    p.setPen(QPen(QColor("#c8dbdf"), 1.6)); p.setBrush(QColor("#eef5f5"));
    p.drawRoundedRect(QRectF(18, 75, 17, 34), 8, 8); p.drawRoundedRect(QRectF(109, 75, 17, 34), 8, 8);
    p.drawRoundedRect(QRectF(43, 119, 23, 14), 6, 6); p.drawRoundedRect(QRectF(78, 119, 23, 14), 6, 6);
    QLinearGradient body(0, 33, 0, 128); body.setColorAt(0, Qt::white); body.setColorAt(1, QColor("#eaf4f2")); p.setBrush(body);
    p.drawRoundedRect(QRectF(29, 34, 86, 94), 28, 28);
    p.setPen(QPen(QColor("#6cbdb1"), 3, Qt::SolidLine, Qt::RoundCap)); p.drawLine(QPointF(72, 33), QPointF(72, 22));
    p.setPen(QPen(QColor("#77c9ba"), 1.4)); p.setBrush(QColor("#b7f1df")); p.drawEllipse(QPointF(72, 17), 6, 6);
    p.setPen(QPen(QColor("#344b57"), 1.5)); p.setBrush(QColor("#20353f")); p.drawRoundedRect(QRectF(38, 47, 68, 47), 18, 18);
    p.setPen(Qt::NoPen); p.setBrush(QColor("#a6f2df"));
    const bool blink = frame_ % 112 >= 108;
    const qreal eyeHeight = blink ? 2 : 13;
    p.drawRoundedRect(QRectF(51, 70-eyeHeight/2, 8, eyeHeight), 4, 4); p.drawRoundedRect(QRectF(85, 70-eyeHeight/2, 8, eyeHeight), 4, 4);
    p.setPen(QPen(QColor("#79cdbd"), 2, Qt::SolidLine, Qt::RoundCap)); p.drawArc(QRectF(66, 73, 12, 9), 200*16, 140*16);
    p.setPen(Qt::NoPen); p.setBrush(QColor("#a0dfd0")); p.drawRoundedRect(QRectF(57, 105, 30, 5), 2.5, 2.5);
    if (busy_) { p.setPen(QPen(QColor("#168b82"), 2.5, Qt::SolidLine, Qt::RoundCap)); p.drawArc(QRectF(65, 101, 14, 14), (frame_*12%360)*16, 240*16); }
    else if (pendingCount_ > 0) { p.setBrush(QColor("#168b82")); p.drawEllipse(QRectF(102, 31, 24, 24)); p.setPen(Qt::white); QFont f = font(); f.setPixelSize(11); f.setBold(true); p.setFont(f); p.drawText(QRectF(102, 31, 24, 24), Qt::AlignCenter, pendingCount_ > 99 ? QStringLiteral("99+") : QString::number(pendingCount_)); }
}
