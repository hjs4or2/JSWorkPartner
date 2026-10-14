#include "PetAssistantPanel.h"
#include "AppConstants.h"
#include <QtWidgets/QLabel>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QStyleOption>
#include <QtWidgets/QSizePolicy>
#include <QtGui/QPainter>
#include <QtGui/QResizeEvent>
#include <QtGui/QFontMetrics>
#include <QtGui/QKeyEvent>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QScreen>
#include <QtGui/QGuiApplication>

namespace
{
class ElidedNameLabel final : public QLabel
{
public:
    explicit ElidedNameLabel(QWidget* parent)
        : QLabel(parent)
    {
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setTextFormat(Qt::PlainText);
    }

    void setFullText(const QString& text)
    {
        fullText_ = text;
        setAccessibleName(text);
        setToolTip(QStringLiteral("<span>%1</span>").arg(text.toHtmlEscaped()));
        updateElidedText();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        updateElidedText();
    }

private:
    void updateElidedText()
    {
        QLabel::setText(fontMetrics().elidedText(
            fullText_, Qt::ElideRight, contentsRect().width()));
    }

    QString fullText_;
};
}

PetAssistantPanel::PetAssistantPanel(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setObjectName(QStringLiteral("petAssistant"));
    setAttribute(Qt::WA_QuitOnClose, false);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground);
    resize(380, 520); setMinimumSize(340, 440);
    setStyleSheet(QStringLiteral(
        "QWidget#petAssistant { background:#f5f9f8; border:1px solid #ccded9; border-radius:16px; }"
        "QLabel { color:#253e47; border:none; background:transparent; }"
        "QPushButton { color:#33535b; background:#e7f1ee; border:1px solid #cbded8; border-radius:8px; padding:8px 10px; }"
        "QPushButton:hover { background:#d7eae3; } QPushButton:focus { border:2px solid #168b82; }"
        "QPushButton:disabled { color:#80948f; background:#eef3f1; }"
        "QPushButton#send { color:white; background:#137e76; border-color:#137e76; }"
        "QPushButton#send:disabled { color:#657d77; background:#dce9e5; border-color:#cbded8; }"
        "QTextBrowser,QPlainTextEdit { color:#253e47; background:white; border:1px solid #d3e3de; border-radius:10px; padding:9px; selection-background-color:#b0e5d7; }"
        "QPlainTextEdit:focus { border:2px solid #168b82; }"));
    QFont f(QStringLiteral("맑은 고딕")); f.setPointSize(10); setFont(f);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(18,18,18,18); layout->setSpacing(12);
    auto* header = new QHBoxLayout;
    nameLabel_ = new ElidedNameLabel(this); QFont title = f; title.setPointSize(16); title.setBold(true); nameLabel_->setFont(title);
    connection_ = new QLabel(QStringLiteral("AI 연결 안 됨"), this); connection_->setTextFormat(Qt::PlainText); connection_->setAccessibleName(QStringLiteral("AI 연결 상태"));
    auto* close = new QPushButton(QStringLiteral("닫기"), this); close->setToolTip(QStringLiteral("대화창 숨기기 (Esc)")); close->setAccessibleName(QStringLiteral("대화창 닫기"));
    header->addWidget(nameLabel_, 1); header->addWidget(close); layout->addLayout(header); layout->addWidget(connection_);
    auto* quick = new QHBoxLayout; quick->setSpacing(6);
    const auto action = [this, quick](const QString& text, void (PetAssistantPanel::*signal)()) {
        auto* b = new QPushButton(text, this); b->setAccessibleName(text); b->setToolTip(text + QStringLiteral(" 화면 요청")); quick->addWidget(b); connect(b, &QPushButton::clicked, this, signal);
    };
    action(QStringLiteral("업무 추가"), &PetAssistantPanel::newTaskRequested); action(QStringLiteral("할 일"), &PetAssistantPanel::tasksRequested); action(QStringLiteral("주간보고서"), &PetAssistantPanel::weeklyReportRequested); layout->addLayout(quick);
    history_ = new QTextBrowser(this); history_->setOpenLinks(false); history_->setOpenExternalLinks(false); history_->setAccessibleName(QStringLiteral("대화 목록")); layout->addWidget(history_, 1);
    state_ = new QLabel(this); state_->setTextFormat(Qt::PlainText); state_->setWordWrap(true); state_->setAccessibleName(QStringLiteral("전송 상태")); layout->addWidget(state_);
    input_ = new QPlainTextEdit(this); input_->setPlaceholderText(QStringLiteral("업무를 추가하거나 궁금한 점을 물어봐.")); input_->setFixedHeight(86); input_->setAccessibleName(QStringLiteral("메시지 입력")); input_->setToolTip(QStringLiteral("Enter 전송 · Shift+Enter 줄바꿈 · 최대 4,000자")); input_->installEventFilter(this); layout->addWidget(input_);
    auto* footer = new QHBoxLayout; fresh_ = new QPushButton(QStringLiteral("새 대화"), this); fresh_->setToolTip(QStringLiteral("새 대화 시작 요청")); fresh_->setAccessibleName(QStringLiteral("새 대화"));
    send_ = new QPushButton(QStringLiteral("보내기"), this); send_->setObjectName(QStringLiteral("send")); send_->setAccessibleName(QStringLiteral("메시지 보내기 또는 중지")); footer->addWidget(fresh_); footer->addStretch(); footer->addWidget(send_); layout->addLayout(footer);
    connect(close, &QPushButton::clicked, this, &QWidget::hide); connect(fresh_, &QPushButton::clicked, this, &PetAssistantPanel::newConversationRequested);
    connect(send_, &QPushButton::clicked, this, [this] { if (busy_) { state_->setText(QStringLiteral("중지 요청 중…")); emit cancelRequested(); } else submit(); });
    connect(input_, &QPlainTextEdit::textChanged, this, &PetAssistantPanel::updateInputState);
    setDisplayName(AppConstants::PetDefaultName);
    updateInputState(); renderMessages();
}
void PetAssistantPanel::setDisplayName(const QString& value)
{
    const QString previousName = displayName_;
    displayName_ = AppConstants::normalizedPetName(value);
    static_cast<ElidedNameLabel*>(nameLabel_)->setFullText(displayName_);
    setWindowTitle(QStringLiteral("%1 · 개인비서").arg(displayName_));
    setAccessibleName(QStringLiteral("%1 개인비서 대화창").arg(displayName_));
    if (busy_ && state_->text() == QStringLiteral("%1가 응답 중이야…").arg(previousName))
        state_->setText(QStringLiteral("%1가 응답 중이야…").arg(displayName_));
    renderMessages();
}
void PetAssistantPanel::updateInputState()
{
    const int length = input_->toPlainText().size();
    send_->setEnabled(busy_ || (length <= MaximumInputLength && !input_->toPlainText().trimmed().isEmpty()));
    if (!busy_) state_->setText(length > MaximumInputLength ? QStringLiteral("입력은 4,000자까지 가능해. (%1자)").arg(length) : QStringLiteral("Enter 전송 · Shift+Enter 줄바꿈"));
}
void PetAssistantPanel::paintEvent(QPaintEvent*)
{
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}
void PetAssistantPanel::submit()
{
    const QString text = input_->toPlainText().trimmed();
    if (busy_ || composing_ || text.isEmpty() || input_->toPlainText().size() > MaximumInputLength) return;
    input_->clear(); emit messageSubmitted(text);
}
bool PetAssistantPanel::eventFilter(QObject* object, QEvent* e)
{
    if (object == input_) {
        if (e->type() == QEvent::InputMethod) composing_ = !static_cast<QInputMethodEvent*>(e)->preeditString().isEmpty();
        if (e->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(e);
            if (key->key() == Qt::Key_Escape && !composing_) { hide(); return true; }
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !(key->modifiers() & Qt::ShiftModifier) && !composing_) { submit(); return true; }
        }
    }
    return QWidget::eventFilter(object, e);
}
void PetAssistantPanel::appendUserMessage(const QString& text) { messages_.append({MessageRole::User, text}); renderMessages(); }
void PetAssistantPanel::beginAssistantMessage() { messages_.append({MessageRole::Assistant, QString()}); assistantIndex_ = messages_.size()-1; setBusy(true); renderMessages(); }
void PetAssistantPanel::appendAssistantDelta(const QString& text) { if (assistantIndex_ < 0) return; messages_[assistantIndex_].text += text; renderMessages(); }
void PetAssistantPanel::finishAssistantMessage() { assistantIndex_ = -1; setBusy(false); }
void PetAssistantPanel::showError(const QString& text) { assistantIndex_ = -1; setBusy(false); state_->setText(QStringLiteral("오류 · ") + text); }
void PetAssistantPanel::setBusy(bool value) { busy_ = value; input_->setReadOnly(value); fresh_->setEnabled(!value); send_->setText(value ? QStringLiteral("중지") : QStringLiteral("보내기")); send_->setToolTip(value ? QStringLiteral("응답 중지 요청") : QStringLiteral("메시지 전송")); updateInputState(); if (value) state_->setText(QStringLiteral("%1가 응답 중이야…").arg(displayName_)); }
void PetAssistantPanel::setConnectionStatus(const QString& text) { connection_->setText(text); }
void PetAssistantPanel::resetConversation() { messages_.clear(); assistantIndex_ = -1; input_->clear(); setBusy(false); renderMessages(); }
void PetAssistantPanel::renderMessages()
{
    auto* bar = history_->verticalScrollBar(); const int old = bar->value(); const bool bottom = old >= bar->maximum()-4;
    QString html = QStringLiteral("<html><body style='font-family:맑은 고딕; font-size:10pt; color:#253e47;'>");
    if (messages_.isEmpty()) html += QStringLiteral("<p style='color:#566f75;'>업무와 궁금한 이야기를 이곳에서 시작해.<br>AI 연결 상태는 위에서 확인할 수 있어.</p>");
    for (const auto& message : messages_) { const QString roleName = message.role == MessageRole::User ? QStringLiteral("나") : displayName_; QString text = message.text.toHtmlEscaped(); text.replace(QStringLiteral("\n"), QStringLiteral("<br>")); html += QStringLiteral("<p style='margin-top:14px;'><b style='color:#137e76;'>%1</b><br>%2</p>").arg(roleName.toHtmlEscaped(), text); }
    history_->setHtml(html + QStringLiteral("</body></html>")); bar->setValue(bottom ? bar->maximum() : old);
}
void PetAssistantPanel::showNear(const QRect& anchor)
{
    QScreen* target = QGuiApplication::primaryScreen();
    for (auto* screen : QGuiApplication::screens()) if (screen->availableGeometry().contains(anchor.center())) { target = screen; break; }
    if (target) { const QRect area = target->availableGeometry(); int x = anchor.left()-width()-12; if (x < area.left()) x = anchor.right()+12;
        move(qBound(area.left(), x, qMax(area.left(), area.right()-width()+1)), qBound(area.top(), anchor.center().y()-height()/2, qMax(area.top(), area.bottom()-height()+1))); }
    show(); raise(); activateWindow(); input_->setFocus();
}
