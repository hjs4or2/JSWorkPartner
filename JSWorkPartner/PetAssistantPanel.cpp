#include "PetAssistantPanel.h"
#include <QtCore/QJsonObject>
#include <QtWidgets/QLabel>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QStyleOption>
#include <QtGui/QPainter>
#include <QtGui/QKeyEvent>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QScreen>
#include <QtGui/QGuiApplication>

PetAssistantPanel::PetAssistantPanel(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setObjectName(QStringLiteral("petAssistant")); setWindowTitle(QStringLiteral("모아 · 개인비서"));
    setAttribute(Qt::WA_QuitOnClose, false);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground);
    resize(380, 520); setMinimumSize(340, 440);
    setAccessibleName(QStringLiteral("모아 개인비서 대화창"));
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
    auto* name = new QLabel(QStringLiteral("모아"), this); QFont title = f; title.setPointSize(16); title.setBold(true); name->setFont(title);
    connection_ = new QLabel(QStringLiteral("AI 연결 안 됨"), this); connection_->setTextFormat(Qt::PlainText); connection_->setAccessibleName(QStringLiteral("AI 연결 상태"));
    auto* close = new QPushButton(QStringLiteral("닫기"), this); close->setToolTip(QStringLiteral("대화창 숨기기 (Esc)")); close->setAccessibleName(QStringLiteral("대화창 닫기"));
    header->addWidget(name); header->addStretch(); header->addWidget(close); layout->addLayout(header); layout->addWidget(connection_);
    auto* quick = new QHBoxLayout; quick->setSpacing(6);
    const auto action = [this, quick](const QString& text, void (PetAssistantPanel::*signal)()) {
        auto* b = new QPushButton(text, this); b->setAccessibleName(text); b->setToolTip(text + QStringLiteral(" 화면 요청")); quick->addWidget(b); connect(b, &QPushButton::clicked, this, signal);
    };
    action(QStringLiteral("업무 추가"), &PetAssistantPanel::newTaskRequested); action(QStringLiteral("할 일"), &PetAssistantPanel::tasksRequested); action(QStringLiteral("주간보고서"), &PetAssistantPanel::weeklyReportRequested); layout->addLayout(quick);
    notice_ = new QLabel(this); notice_->setTextFormat(Qt::PlainText); notice_->setWordWrap(true); layout->addWidget(notice_);
    storageNotice_ = new QLabel(this); storageNotice_->setTextFormat(Qt::PlainText); storageNotice_->setWordWrap(true); layout->addWidget(storageNotice_);
    history_ = new QTextBrowser(this); history_->setOpenLinks(false); history_->setOpenExternalLinks(false); history_->setAccessibleName(QStringLiteral("대화 목록")); layout->addWidget(history_, 1);
    state_ = new QLabel(this); state_->setTextFormat(Qt::PlainText); state_->setWordWrap(true); state_->setAccessibleName(QStringLiteral("전송 상태")); layout->addWidget(state_);
    input_ = new QPlainTextEdit(this); input_->setPlaceholderText(QStringLiteral("업무를 추가하거나 궁금한 점을 물어봐.")); input_->setFixedHeight(86); input_->setAccessibleName(QStringLiteral("메시지 입력")); input_->setToolTip(QStringLiteral("Enter 전송 · Shift+Enter 줄바꿈 · 최대 4,000자")); input_->installEventFilter(this); layout->addWidget(input_);
    auto* footer = new QHBoxLayout; fresh_ = new QPushButton(QStringLiteral("새 대화"), this); fresh_->setToolTip(QStringLiteral("새 대화 시작 요청")); fresh_->setAccessibleName(QStringLiteral("새 대화"));
    send_ = new QPushButton(QStringLiteral("보내기"), this); send_->setObjectName(QStringLiteral("send")); send_->setAccessibleName(QStringLiteral("메시지 보내기 또는 중지")); footer->addWidget(fresh_); footer->addStretch(); footer->addWidget(send_); layout->addLayout(footer);
    connect(close, &QPushButton::clicked, this, &QWidget::hide); connect(fresh_, &QPushButton::clicked, this, &PetAssistantPanel::newConversationRequested);
    connect(send_, &QPushButton::clicked, this, [this] { if (busy_) { state_->setText(QStringLiteral("중지 요청 중…")); emit cancelRequested(); } else submit(); });
    connect(input_, &QPlainTextEdit::textChanged, this, [this] { boundMessages(); updateInputState(); renderMessages(); emit conversationChanged(); });
    updateInputState(); renderMessages();
}
void PetAssistantPanel::updateInputState()
{
    const int length = input_->toPlainText().size();
    send_->setEnabled(!restorePending_ && (busy_ || (length <= MaximumInputLength && !input_->toPlainText().trimmed().isEmpty())));
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
    if (restorePending_ || busy_ || composing_ || text.isEmpty() || input_->toPlainText().size() > MaximumInputLength) return;
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
void PetAssistantPanel::appendUserMessage(const QString& text) { messages_.append({QStringLiteral("user"), text}); boundMessages(); renderMessages(); emit conversationChanged(); }
void PetAssistantPanel::beginAssistantMessage() { messages_.append({QStringLiteral("assistant"), QString(),true}); assistantIndex_ = messages_.size()-1; boundMessages(); interrupted_=true; setBusy(true); renderMessages(); emit conversationChanged(); }
void PetAssistantPanel::appendAssistantDelta(const QString& text) { if (assistantIndex_ < 0) return; auto& message=messages_[assistantIndex_]; if (message.text.size()+text.size()>PetConversationStore::MaximumMessageLength) limited_=true; message.text += text.left(qMax(0,PetConversationStore::MaximumMessageLength-message.text.size())); boundMessages(); renderMessages(); emit conversationChanged(); }
void PetAssistantPanel::finishAssistantMessage(bool completed) { if (assistantIndex_>=0) messages_[assistantIndex_].interrupted=!completed; interrupted_=!completed; assistantIndex_ = -1; setBusy(false); boundMessages(); renderMessages(); emit conversationChanged(); }
void PetAssistantPanel::showError(const QString& text) { finishAssistantMessage(false); state_->setText(QStringLiteral("오류 · ") + text); }
void PetAssistantPanel::setBusy(bool value) { busy_ = value; input_->setReadOnly(value || restorePending_); fresh_->setEnabled(!value && !restorePending_); send_->setText(value ? QStringLiteral("중지") : QStringLiteral("보내기")); send_->setToolTip(value ? QStringLiteral("응답 중지 요청") : QStringLiteral("메시지 전송")); updateInputState(); if (value) state_->setText(QStringLiteral("모아가 응답 중이야…")); }
void PetAssistantPanel::setConnectionStatus(const QString& text) { connection_->setText(text); }
void PetAssistantPanel::resetConversation() { messages_.clear(); assistantIndex_ = -1; limited_=false; interrupted_=false; input_->clear(); setBusy(false); renderMessages(); emit conversationChanged(); }
void PetAssistantPanel::renderMessages()
{
    auto* bar = history_->verticalScrollBar(); const int old = bar->value(); const bool bottom = old >= bar->maximum()-4;
    QString html = QStringLiteral("<html><body style='font-family:맑은 고딕; font-size:10pt; color:#253e47;'>");
    if (messages_.isEmpty()) html += QStringLiteral("<p style='color:#566f75;'>업무와 궁금한 이야기를 이곳에서 시작해.<br>AI 연결 상태는 위에서 확인할 수 있어.</p>");
    for (const auto& message : messages_) { QString text = message.text.toHtmlEscaped(); text.replace(QStringLiteral("\n"), QStringLiteral("<br>")); html += QStringLiteral("<p style='margin-top:14px;'><b style='color:#137e76;'>%1</b><br>%2</p>").arg((message.role==QStringLiteral("user") ? QStringLiteral("나") : QStringLiteral("모아") + (message.interrupted ? QStringLiteral(" · 중단된 응답") : QString())).toHtmlEscaped(), text); }
    QStringList notices;
    bool hasInterrupted=interrupted_; for (const auto& m:messages_) hasInterrupted=hasInterrupted || m.interrupted;
    if (hasInterrupted) notices.append(QStringLiteral("중단된 요청은 이미 완료됐을 수 있어. 필요하면 할 일 목록에서 확인해. 자동으로 다시 실행하지 않아."));
    if (limited_) notices.append(QStringLiteral("저장 한도로 최근 100개·256KiB만 보관해. 긴 응답은 32,768자, 작성 중 입력은 4,000자까지 보관해."));
    notice_->setText(notices.join(QLatin1Char('\n'))); notice_->setVisible(!notices.isEmpty());
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

PetConversationSnapshot PetAssistantPanel::snapshot() const
{
    PetConversationSnapshot s;s.draft=input_->toPlainText().left(MaximumInputLength);s.interrupted=interrupted_ || busy_;s.limited=limited_ || input_->toPlainText().size()>MaximumInputLength;
    for (const auto& m:messages_) s.messages.append(QJsonObject{{QStringLiteral("role"),m.role},{QStringLiteral("text"),m.text},{QStringLiteral("interrupted"),m.interrupted}});
    return s;
}
void PetAssistantPanel::boundMessages()
{
    while (messages_.size()>1 && (messages_.size()>PetConversationStore::MaximumMessages || !PetConversationStore::validate(snapshot()))) {
        messages_.removeFirst();if (assistantIndex_>=0) --assistantIndex_;limited_=true;
    }
}
void PetAssistantPanel::restoreConversation(const PetConversationSnapshot& s)
{
    messages_.clear();assistantIndex_=-1;limited_=s.limited;interrupted_=s.interrupted;
    for (const auto v:s.messages) {const auto m=v.toObject();messages_.append({m.value(QStringLiteral("role")).toString(),m.value(QStringLiteral("text")).toString(),m.value(QStringLiteral("interrupted")).toBool()});}
    input_->setPlainText(s.draft);setBusy(false);renderMessages();emit conversationChanged();
}
void PetAssistantPanel::setRestorePending(bool pending)
{restorePending_=pending;setBusy(false);}
void PetAssistantPanel::setStorageNotice(const QString& text)
{storageNotice_->setText(text);storageNotice_->setVisible(!text.isEmpty());}
