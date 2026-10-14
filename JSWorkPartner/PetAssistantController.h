#pragma once
#include <QtCore/QObject>
#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <functional>
class DesktopPetWidget;
class PetAssistantPanel;
class CodexAssistantBackend;
class LocalTaskToolClient;
class QDialog;

class PetAssistantController final : public QObject
{
    Q_OBJECT
public:
    PetAssistantController(DesktopPetWidget& pet,PetAssistantPanel& panel,const QString& existingDatabase,QObject* parent=nullptr);
    ~PetAssistantController() override;
    void showAssistant();
    void shutdown();
    QString connectionStatus() const { return connectionStatus_; }
signals:
    void tasksChanged();
    void connectionStatusChanged(QString status);
private:
    void setConnectionStatus(const QString& status);
    void submit(const QString& text);
    void handleTool(QJsonValue id,QString call,QString thread,QString turn,QString name,QJsonObject arguments);
    void dispatch(const QString& name,QJsonObject arguments,std::function<void(QJsonObject,bool)> done,std::function<bool()> operationGate={});
    void selectCompletion(QJsonObject arguments,std::function<void(QJsonObject,bool)> done,std::function<bool()> operationGate={});
    void observe(const QJsonObject& data);
    void askQuestions(QJsonValue id,QJsonArray questions);
    void askAsyncQuestions(QString itemId,QJsonArray questions);
    QString findTaskExecutable() const;
    DesktopPetWidget& pet_;
    PetAssistantPanel& panel_;
    CodexAssistantBackend* backend_=nullptr;
    LocalTaskToolClient* tasks_=nullptr;
    QString database_,pendingText_,intent_,lastText_;
    QString connectionStatus_=QStringLiteral("연결 전");
    QHash<QString,QString> unresolved_;
    QHash<QString,QJsonObject> successful_;
    QHash<QString,QJsonObject> knownTasks_;
    QPointer<QDialog> selection_;
    QPointer<QDialog> questions_;
    bool connecting_=false,lastFailed_=false,shuttingDown_=false,asyncQuestion_=false;
};
