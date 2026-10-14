#pragma once
#include <QtWidgets/QWidget>
#include <QtCore/QStringList>
class QLabel;
class QTextBrowser;
class QPlainTextEdit;
class QPushButton;

class PetAssistantPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit PetAssistantPanel(QWidget* parent = nullptr);
    void setDisplayName(const QString& displayName);
    void appendUserMessage(const QString& text);
    void beginAssistantMessage();
    void appendAssistantDelta(const QString& text);
    void finishAssistantMessage();
    void showError(const QString& text);
    void setBusy(bool busy);
    void setConnectionStatus(const QString& text);
    void resetConversation();
    void showNear(const QRect& anchor);
    static constexpr int MaximumInputLength = 4000;
signals:
    void messageSubmitted(QString text);
    void cancelRequested();
    void newConversationRequested();
    void newTaskRequested();
    void tasksRequested();
    void weeklyReportRequested();
protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
private:
    void submit();
    void renderMessages();
    void updateInputState();
    enum class MessageRole { User, Assistant };
    struct Message { MessageRole role; QString text; };
    QList<Message> messages_;
    QString displayName_;
    int assistantIndex_ = -1;
    bool busy_ = false;
    bool composing_ = false;
    QLabel* connection_ = nullptr;
    QLabel* nameLabel_ = nullptr;
    QLabel* state_ = nullptr;
    QTextBrowser* history_ = nullptr;
    QPlainTextEdit* input_ = nullptr;
    QPushButton* send_ = nullptr;
    QPushButton* fresh_ = nullptr;
};
