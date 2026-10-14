#include "DesktopPetWidget.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>
#include <QtGui/QScreen>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
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
QStringList DesktopPetWidget::designIds()
{ return {QStringLiteral("mint-bot"), QStringLiteral("space-bot"), QStringLiteral("cat-bot"), QStringLiteral("mini-drone")}; }
QString DesktopPetWidget::normalizedDesignId(const QString& id)
{ return designIds().contains(id) ? id : QStringLiteral("mint-bot"); }
QString DesktopPetWidget::designName(const QString& id)
{
    const int index = designIds().indexOf(normalizedDesignId(id));
    return QStringList{QStringLiteral("민트봇"), QStringLiteral("우주봇"), QStringLiteral("고양이봇"), QStringLiteral("미니드론")}.at(index);
}
void DesktopPetWidget::setDesignId(const QString& id)
{ designId_ = normalizedDesignId(id); update(); }
void DesktopPetWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.scale(sizePercent_ / 100.0, sizePercent_ / 100.0);
    paintDesign(p, designId_, frame_, busy_, pendingCount_);
}
void DesktopPetWidget::paintDesign(QPainter& p, const QString& requestedId, int frame, bool busy, int pendingCount)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QString id = normalizedDesignId(requestedId);
    const QColor ink("#273d50");
    const QColor accent = id == "space-bot" ? QColor("#bda8ff") : id == "cat-bot" ? QColor("#ffc478") : id == "mini-drone" ? QColor("#8ad6ff") : QColor("#92ead2");
    auto shape = [&](const QColor& fill) { p.setPen(QPen(ink, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(fill); };
    auto round = [&](qreal x,qreal y,qreal w,qreal h,qreal r) { p.drawRoundedRect(QRectF(x,y,w,h),r,r); };
    p.setPen(Qt::NoPen); p.setBrush(QColor(30,50,65,22)); p.drawEllipse(QRectF(30,133,84,8));
    p.translate(0, std::sin(frame * .045) * 1.5);
    QRectF face;
    if (id == "space-bot") {
        // Broad helmet, side pods, compact suit and unmistakable rocket boots.
        shape(QColor("#7978bb")); round(18,48,14,30,6); round(112,48,14,30,6);
        shape(QColor("#e5e3fa")); round(44,87,56,35,12);
        shape(QColor("#8883cd")); round(40,119,25,12,4); round(79,119,25,12,4);
        shape(QColor("#e5e3fa")); p.drawEllipse(QRectF(28,24,88,76));
        shape(ink); face=QRectF(38,41,68,43); round(38,41,68,43,20);
        p.setPen(QPen(Qt::white,3,Qt::SolidLine,Qt::RoundCap)); p.drawLine(QPointF(46,43),QPointF(56,36));
        shape(accent); round(29,94,11,20,5); round(104,94,11,20,5);
        p.setPen(QPen(ink,2.6)); p.drawLine(QPointF(72,24),QPointF(82,14)); p.setBrush(accent); p.drawEllipse(QPointF(84,12),4,4);
    } else if (id == "cat-bot") {
        // Angular ears and curled tail stay distinct even at the default size.
        shape(QColor("#f7d7a8"));
        QPainterPath tail; tail.moveTo(103,115); tail.cubicTo(132,119,127,89,116,94); p.setBrush(Qt::NoBrush); p.setPen(QPen(ink,10,Qt::SolidLine,Qt::RoundCap)); p.drawPath(tail); p.setPen(QPen(accent,5,Qt::SolidLine,Qt::RoundCap)); p.drawPath(tail);
        shape(QColor("#f7d7a8")); round(44,83,56,43,17); round(39,118,25,12,5); round(80,118,25,12,5);
        QPolygonF ears; ears << QPointF(31,48) << QPointF(31,16) << QPointF(57,35) << QPointF(87,35) << QPointF(113,16) << QPointF(113,48); p.drawPolygon(ears);
        p.setPen(Qt::NoPen); p.setBrush(QColor("#ed9b83")); QPolygonF ear; ear << QPointF(37,34)<<QPointF(37,25)<<QPointF(49,35); p.drawPolygon(ear); ear.clear(); ear<<QPointF(95,35)<<QPointF(107,25)<<QPointF(107,34); p.drawPolygon(ear);
        shape(QColor("#f7d7a8")); round(27,35,90,60,20);
        shape(ink); face=QRectF(38,47,68,35); round(38,47,68,35,12);
        p.setPen(QPen(ink,2.5,Qt::SolidLine,Qt::RoundCap)); p.drawLine(QPointF(22,63),QPointF(34,66)); p.drawLine(QPointF(22,76),QPointF(34,73)); p.drawLine(QPointF(110,66),QPointF(122,63)); p.drawLine(QPointF(110,73),QPointF(122,76));
    } else if (id == "mini-drone") {
        // Wide rotors and a floating diamond hull, with no humanoid legs.
        shape(QColor("#a9dff1")); p.drawLine(QPointF(39,65),QPointF(23,39)); p.drawLine(QPointF(105,65),QPointF(121,39));
        shape(QColor("#e3f4fa")); p.drawEllipse(QRectF(9,26,34,11)); p.drawEllipse(QRectF(101,26,34,11));
        p.setPen(QPen(ink,2.6)); p.drawLine(QPointF(26,29),QPointF(26,42)); p.drawLine(QPointF(118,29),QPointF(118,42));
        shape(accent); QPolygonF hull; hull<<QPointF(72,44)<<QPointF(119,76)<<QPointF(99,107)<<QPointF(72,121)<<QPointF(45,107)<<QPointF(25,76); p.drawPolygon(hull);
        shape(QColor("#e3f4fa")); round(39,58,66,40,17);
        shape(ink); face=QRectF(45,65,54,26); round(45,65,54,26,10);
        p.setPen(QPen(ink,3,Qt::SolidLine,Qt::RoundCap)); p.drawLine(QPointF(54,116),QPointF(46,127)); p.drawLine(QPointF(90,116),QPointF(98,127));
        p.setPen(QPen(accent,2,Qt::SolidLine,Qt::RoundCap)); const qreal spread=busy ? 4+frame%4 : 4; p.drawLine(QPointF(62-spread,128),QPointF(82+spread,128));
    } else {
        // Mint family: separate rounded square head, pear body and leaf antenna.
        shape(QColor("#edf9f4")); round(22,87,14,25,7); round(108,87,14,25,7); round(43,119,23,12,5); round(78,119,23,12,5); round(39,83,66,43,17);
        p.setPen(QPen(ink,2.6)); p.drawLine(QPointF(72,30),QPointF(72,17));
        shape(accent); p.drawEllipse(QRectF(71,9,16,10));
        shape(QColor("#edf9f4")); round(27,30,90, 60,22);
        shape(ink); face=QRectF(37,43,70,34); round(37,43,70,34,12);
        p.setPen(Qt::NoPen); p.setBrush(accent); round(55,91,34,6,3);
    }
    const bool blink = frame % 112 >= 108;
    const qreal eyeHeight = blink ? 2 : 11;
    const qreal eyeY = face.center().y()-2;
    p.setPen(Qt::NoPen); p.setBrush(accent);
    round(face.center().x()-21,eyeY-eyeHeight/2,7,eyeHeight,3);
    round(face.center().x()+14,eyeY-eyeHeight/2,7,eyeHeight,3);
    p.setPen(QPen(accent,2,Qt::SolidLine,Qt::RoundCap)); p.drawArc(QRectF(66,eyeY+2,12,8),200*16,140*16);
    // A fixed chest indicator and top-right badge work across all silhouettes.
    shape(ink); round(62,101,20,16,5);
    if (busy) { p.setPen(QPen(accent,2.5,Qt::SolidLine,Qt::RoundCap)); p.setBrush(Qt::NoBrush); p.drawArc(QRectF(66,103,12,12),(frame*12%360)*16,240*16); }
    else { p.setPen(Qt::NoPen); p.setBrush(accent); round(67,107,10,4,2); }
    if (!busy && pendingCount > 0) {
        p.setPen(QPen(Qt::white,1.5)); p.setBrush(QColor("#176d78")); p.drawEllipse(QRectF(107,43,26,26));
        p.setPen(Qt::white); QFont f=p.font(); f.setPixelSize(11); f.setBold(true); p.setFont(f);
        p.drawText(QRectF(107,43,26,26),Qt::AlignCenter,pendingCount>99 ? QStringLiteral("99+") : QString::number(pendingCount));
    }
    p.restore();
}
