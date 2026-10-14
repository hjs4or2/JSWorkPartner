#pragma once
#include "PetJsonRpcProcess.h"
#include <QtCore/QTemporaryDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QSet>
#include <memory>

class CodexAssistantBackend final : public QObject
{
    Q_OBJECT
public:
    struct ToolOperationToken {
        QJsonValue requestId;
        QString callId,threadId,turnId;
        quint64 generation=0;
    };
    explicit CodexAssistantBackend(QObject* parent=nullptr);
    ~CodexAssistantBackend() override;
    void start(const QJsonArray& tools);
    void setDynamicTools(const QJsonArray& tools) { if (!busy_) tools_=tools; }
    void submit(const QString& text);
    void cancel();
    void newConversation();
    void shutdown();
    bool isBusy() const { return busy_; }
    bool isReady() const { return ready_; }
    ToolOperationToken toolOperation(QJsonValue requestId,QString callId,QString threadId,QString turnId) const;
    bool canDispatchTool(const ToolOperationToken& operation) const;
    void answerTool(const QJsonValue& requestId,const QString& callId,const QString& threadId,
                    const QString& turnId,const QJsonObject& data,bool success);
    void answerQuestions(const QJsonValue& requestId,const QJsonObject& answers);
    void answerAsyncQuestions(const QString& text);
    static QString locateExecutable();
signals:
    void ready();
    void statusChanged(QString status);
    void busyChanged(bool busy);
    void assistantDelta(QString text);
    void turnFinished(QString status);
    void connectionError(QString message);
    void registeredTaskServer(QString executable,QString database);
    void toolRequested(QJsonValue requestId,QString callId,QString threadId,QString turnId,QString name,QJsonObject arguments);
    void questionsRequested(QJsonValue requestId,QJsonArray questions);
    void asyncQuestionsRequested(QString itemId,QJsonArray questions);
    void stopped();
private:
    void runDiscovery(const QStringList& arguments,std::function<void(const QByteArray&)> done);
    void stopDiscovery();
    void launch(const QJsonArray& registrations);
    void initialize();
    void startThread();
    void startTurn();
    void sendInterrupt();
    void handle(const QJsonObject& message);
    void fail(const QString& message);
    void setBusy(bool busy);
    bool verifyIsolation(const QJsonObject& result) const;
    bool verifyCatalog() const;
    void checkRuntime(const QString& threadId,std::function<void(bool)> done);
    PetJsonRpcProcess rpc_;
    std::unique_ptr<QTemporaryDir> context_;
    QProcess* discovery_=nullptr;
    QTimer watchdog_;
    QElapsedTimer activity_,duration_,cancelElapsed_;
    QJsonArray tools_;
    QString program_,version_,model_,thread_,turn_,pendingText_,catalogPath_;
    QHash<QString,QJsonObject> toolCalls_;
    QHash<QString,QJsonValue> inputRequests_;
    QSet<QString> asyncItems_;
    QStringList disabledServers_;
    bool ready_=false,busy_=false,cancelling_=false,starting_=false,shuttingDown_=false,interruptSent_=false;
    quint64 generation_=0;
};
