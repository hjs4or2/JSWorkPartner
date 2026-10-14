#include "PetAssistantController.h"
#include "CodexAssistantBackend.h"
#include "LocalTaskToolClient.h"
#include "DesktopPetWidget.h"
#include "PetAssistantPanel.h"
#include "AppConstants.h"
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <QtCore/QSettings>
#include <QtCore/QUuid>
#include <QtCore/QRegularExpression>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLineEdit>

namespace {
QJsonObject failure(const QString& message)
{ return {{QStringLiteral("error"),QJsonObject{{QStringLiteral("message"),message}}}}; }
bool validId(const QJsonValue& id)
{
    if (!id.isString() || !QRegularExpression(QStringLiteral("^[1-9][0-9]{0,18}$")).match(id.toString()).hasMatch()) return false;
    bool ok=false;const qint64 value=id.toString().toLongLong(&ok);return ok && value>0;
}
}
PetAssistantController::PetAssistantController(DesktopPetWidget& pet,PetAssistantPanel& panel,const QString& database,QObject* parent)
    :QObject(parent),pet_(pet),panel_(panel),database_(QFileInfo(database).absoluteFilePath()),store_(database_)
{
    const auto loaded=store_.load(savedConversation_,restoreError_);
    restorePending_=loaded!=PetConversationStore::LoadResult::Missing;
    panel_.setRestorePending(restorePending_);
    saveTimer_.setSingleShot(true);saveTimer_.setInterval(200);
    connect(&saveTimer_,&QTimer::timeout,this,&PetAssistantController::saveConversation);
    connect(&panel_,&PetAssistantPanel::conversationChanged,this,[this]() {if (!restorePending_ && !restoring_ && !shuttingDown_ && !saveTimer_.isActive()) saveTimer_.start();});
    backend_=new CodexAssistantBackend(this);tasks_=new LocalTaskToolClient(this);
    tasks_->configure(findTaskExecutable(),database_);
    connect(&panel_,&PetAssistantPanel::messageSubmitted,this,&PetAssistantController::submit);
    connect(&panel_,&PetAssistantPanel::cancelRequested,this,[this]() {
        if (selection_) selection_->reject();
        if (questions_) questions_->reject();
        if (connecting_) {pendingText_.clear();connecting_=false;backend_->shutdown();tasks_->shutdown();panel_.setBusy(false);pet_.setBusy(false);panel_.finishAssistantMessage(false);setConnectionStatus(QStringLiteral("연결 전"));}
        else backend_->cancel();
    });
    connect(&panel_,&PetAssistantPanel::newConversationRequested,this,[this]() {
        if (backend_->isBusy() || connecting_) return;
        if (questions_) questions_->reject();
        resetConversation();
        // Lost mutation responses retain retry identities for this application's lifetime.
    });
    connect(backend_,&CodexAssistantBackend::ready,this,[this]() {
        if (connecting_ && !pendingText_.isEmpty()) tasks_->start();
    });
    connect(tasks_,&LocalTaskToolClient::ready,this,[this]() {
        if (!connecting_ || pendingText_.isEmpty()) return;
        // Updating tools is local; does not start another turn or fetch task data.
        backend_->setDynamicTools(tasks_->dynamicTools());
        const QString text=pendingText_;pendingText_.clear();connecting_=false;submitToBackend(text);
    });
    connect(backend_,&CodexAssistantBackend::registeredTaskServer,this,[this](const QString& exe,const QString& registeredDb) {
        if (QFileInfo(registeredDb).absoluteFilePath().compare(database_,Qt::CaseInsensitive)!=0 || !QFileInfo(exe).isFile()) return;
        tasks_->configure(QFileInfo(exe).absoluteFilePath(),database_);
    });
    const auto error=[this](const QString& message) {
        if (shuttingDown_) return;
        setConnectionStatus(QStringLiteral("오류"));
        lastFailed_=true;connecting_=false;pendingText_.clear();
        if (backend_->isBusy()) panel_.setConnectionStatus(QStringLiteral("연결 알림: %1").arg(message));
        else panel_.showError(message);
        if (!backend_->isReady() && questions_) questions_->reject();
        panel_.setBusy(backend_->isBusy());pet_.setBusy(backend_->isBusy());
        if (!backend_->isBusy()) panel_.finishAssistantMessage(false);
    };
    connect(tasks_,&LocalTaskToolClient::connectionError,this,error);
    connect(backend_,&CodexAssistantBackend::connectionError,this,error);
    connect(backend_,&CodexAssistantBackend::statusChanged,&panel_,&PetAssistantPanel::setConnectionStatus);
    connect(backend_,&CodexAssistantBackend::statusChanged,this,[this](const QString& status) {
        if (status.contains(QStringLiteral("ChatGPT 로그인"))) setConnectionStatus(QStringLiteral("연결됨"));
        else if (status.contains(QStringLiteral("연결 확인 중"))) setConnectionStatus(QStringLiteral("연결 중"));
        else if (status.contains(QStringLiteral("연결 확인 필요"))) setConnectionStatus(QStringLiteral("오류"));
        else if (status == QStringLiteral("AI 응답 취소 중"))
            setConnectionStatus(backend_->isReady() ? QStringLiteral("연결됨") : QStringLiteral("연결 전"));
    });
    connect(backend_,&CodexAssistantBackend::assistantDelta,&panel_,&PetAssistantPanel::appendAssistantDelta);
    connect(backend_,&CodexAssistantBackend::busyChanged,this,[this](bool busy) {
        panel_.setBusy(busy || connecting_);pet_.setBusy(busy || connecting_);
        if (!busy && selection_) selection_->reject();
        if (!busy && questions_ && !asyncQuestion_) questions_->reject();
    });
    connect(backend_,&CodexAssistantBackend::turnFinished,this,[this](const QString& status) {
        lastFailed_=status!=QStringLiteral("completed");
        if (status==QStringLiteral("completed") && backend_->isReady() && tasks_->isReady())
            setConnectionStatus(QStringLiteral("연결됨"));
        panel_.finishAssistantMessage(status==QStringLiteral("completed"));
        if (status==QStringLiteral("completed")) restoredReference_=QJsonArray();
        saveConversation();
        if (status==QStringLiteral("interrupted")) panel_.showError(QStringLiteral("응답을 취소했습니다. 이미 요청한 업무 변경은 완료됐을 수 있습니다."));
    });
    connect(backend_,&CodexAssistantBackend::toolRequested,this,&PetAssistantController::handleTool);
    connect(backend_,&CodexAssistantBackend::questionsRequested,this,&PetAssistantController::askQuestions);
    connect(backend_,&CodexAssistantBackend::asyncQuestionsRequested,this,&PetAssistantController::askAsyncQuestions);
    panel_.setConnectionStatus(QStringLiteral("연결 전 · 현재 Codex 로그인 사용"));
}
void PetAssistantController::setConnectionStatus(const QString& status)
{
    if (connectionStatus_ == status) return;
    connectionStatus_ = status;
    emit connectionStatusChanged(status);
}
PetAssistantController::~PetAssistantController()
{ shutdown();delete tasks_;tasks_=nullptr;delete backend_;backend_=nullptr; }
QString PetAssistantController::findTaskExecutable() const
{
    const QString explicitPath=QSettings().value(AppConstants::PetMcpExecutableKey).toString();
    if (!explicitPath.isEmpty()) return explicitPath;
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i=0;i<6;++i) {
        for (const auto& path:{QStringLiteral("JSWorkPartner.Mcp.exe"),QStringLiteral("JSWorkPartner.Mcp/bin/Release/net8.0/win-x64/publish/JSWorkPartner.Mcp.exe")}) {
            const QString candidate=dir.absoluteFilePath(path);if (QFileInfo(candidate).isFile()) return candidate;
        }
        if (!dir.cdUp()) break;
    }
    return QString();
}
void PetAssistantController::showAssistant()
{ if (!pet_.isVisible()) {pet_.ensureVisibleOnScreen();pet_.show();} panel_.showNear(pet_.geometry()); if (restorePending_) offerConversationRestore(); }
void PetAssistantController::submit(const QString& text)
{
    if (restorePending_ || shuttingDown_ || connecting_ || backend_->isBusy() || text.trimmed().isEmpty() || text.size()>4000) return;
    if (!(lastFailed_ && lastText_==text)) {intent_=QUuid::createUuid().toString().mid(1,36);successful_.clear();}
    lastText_=text;lastFailed_=false;panel_.appendUserMessage(text);panel_.beginAssistantMessage();saveConversation();
    if (backend_->isReady() && tasks_->isReady()) {submitToBackend(text);return;}
    connecting_=true;pendingText_=text;panel_.setBusy(true);pet_.setBusy(true);backend_->start({});
}
void PetAssistantController::handleTool(QJsonValue id,QString call,QString thread,QString turn,QString name,QJsonObject arguments)
{
    const auto done=[this,id,call,thread,turn](QJsonObject data,bool success) {backend_->answerTool(id,call,thread,turn,data,success);};
    const auto operation=backend_->toolOperation(id,call,thread,turn);
    const auto operationGate=[this,operation]() {return !shuttingDown_ && backend_->canDispatchTool(operation);};
    if (!operationGate()) {done(failure(QStringLiteral("취소되었거나 만료된 업무 요청입니다.")),false);return;}
    if (!LocalTaskToolClient::permitted(name) || arguments.contains(QStringLiteral("requestId"))) {done(failure(QStringLiteral("허용되지 않은 업무 도구 또는 인자입니다.")),false);return;}
    if (name==QStringLiteral("complete_task")) {
        if (!validId(arguments.value(QStringLiteral("id")))) {done(failure(QStringLiteral("업무 ID는 양의 십진 문자열이어야 합니다.")),false);return;}
        // Always confirm exact identity in the view. The model cannot silently pick
        // the first result of a search, even when it submits a syntactically valid ID.
        selectCompletion(arguments,done,operationGate);return;
    }
    dispatch(name,arguments,done,operationGate);
}
void PetAssistantController::observe(const QJsonObject& data)
{
    auto remember=[this](const QJsonObject& task) { if (validId(task.value(QStringLiteral("id")))) knownTasks_.insert(task.value(QStringLiteral("id")).toString(),task); };
    remember(data.value(QStringLiteral("task")).toObject());
    for (const auto value:data.value(QStringLiteral("tasks")).toArray()) remember(value.toObject());
}
void PetAssistantController::dispatch(const QString& name,QJsonObject arguments,std::function<void(QJsonObject,bool)> done,std::function<bool()> operationGate)
{
    const bool mutation=name==QStringLiteral("create_task") || name==QStringLiteral("complete_task");
    if ((operationGate && !operationGate()) || (mutation && !operationGate)) {done(failure(QStringLiteral("취소되었거나 만료된 업무 변경 요청입니다.")),false);return;}
    const QString key=name+QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact));
    const QString successfulKey=intent_+key;
    if (mutation && successful_.contains(successfulKey)) {done(successful_.value(successfulKey),true);return;}
    if (mutation) {
        if (!unresolved_.contains(key)) unresolved_.insert(key,QUuid::createUuid().toString().mid(1,36));
        arguments.insert(QStringLiteral("requestId"),unresolved_.value(key));
    }
    tasks_->call(name,arguments,[this,mutation,key,successfulKey,done,operationGate](const QJsonObject& result,const QString& error) {
        if (!error.isEmpty()) {done(failure(error),false);return;}
        if (!mutation && operationGate && !operationGate()) {done(failure(QStringLiteral("취소되었거나 만료된 업무 조회 요청입니다.")),false);return;}
        QJsonObject data=result.value(QStringLiteral("structuredContent")).toObject();
        if (data.isEmpty()) for (const auto value:result.value(QStringLiteral("content")).toArray()) {
            const auto item=value.toObject();if (item.value(QStringLiteral("type")).toString()!=QStringLiteral("text")) continue;
            const auto doc=QJsonDocument::fromJson(item.value(QStringLiteral("text")).toString().toUtf8());if (doc.isObject()) {data=doc.object();break;}
        }
        const bool success=!result.value(QStringLiteral("isError")).toBool() && !data.isEmpty() && !data.contains(QStringLiteral("error"));
        if (success) {observe(data);if (mutation) {successful_.insert(successfulKey,data);unresolved_.remove(key);emit tasksChanged();}}
        done(data.isEmpty() ? failure(QStringLiteral("업무 결과를 해석하지 못했습니다. 변경 여부는 확인이 필요합니다.")) : data,success);
    });
}
void PetAssistantController::selectCompletion(QJsonObject arguments,std::function<void(QJsonObject,bool)> done,std::function<bool()> operationGate)
{
    if (!operationGate || !operationGate()) {done(failure(QStringLiteral("취소되었거나 만료된 업무 완료 요청입니다.")),false);return;}
    if (selection_) {done(failure(QStringLiteral("다른 업무 선택을 먼저 완료해 주세요.")),false);return;}
    const QString requestedId=arguments.value(QStringLiteral("id")).toString();
    dispatch(QStringLiteral("get_task"),{{QStringLiteral("id"),requestedId}},[this,arguments,requestedId,done,operationGate](QJsonObject data,bool success) mutable {
        if (!success || !operationGate()) {done(success ? failure(QStringLiteral("업무 완료 요청이 취소되었습니다.")) : data,false);return;}
        const auto task=data.value(QStringLiteral("task")).toObject();
        if (!validId(task.value(QStringLiteral("id")))) {done(failure(QStringLiteral("완료할 업무를 찾지 못했습니다.")),false);return;}
        auto* dialog=new QDialog(&panel_);dialog->setAttribute(Qt::WA_DeleteOnClose);selection_=dialog;
        dialog->setWindowTitle(QStringLiteral("완료할 업무 선택"));
        auto* layout=new QVBoxLayout(dialog);auto* label=new QLabel(QStringLiteral("완료할 업무를 직접 선택해 주세요. 제목과 ID를 확인해 주세요."),dialog);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);layout->addWidget(label);
        auto* combo=new QComboBox(dialog);combo->addItem(QStringLiteral("업무 선택…"),QString());
        QStringList ids=knownTasks_.keys();ids.sort();
        for (const auto& id:ids) {
            const auto candidate=knownTasks_.value(id);
            if (candidate.value(QStringLiteral("status")).toString()==QStringLiteral("completed") && id!=requestedId) continue;
            combo->addItem(QStringLiteral("#%1  %2").arg(id,candidate.value(QStringLiteral("title")).toString()),id);
        }
        layout->addWidget(combo);auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,dialog);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);layout->addWidget(buttons);
        connect(combo,static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),dialog,[buttons](int index) {buttons->button(QDialogButtonBox::Ok)->setEnabled(index>0);});
        connect(buttons,&QDialogButtonBox::accepted,dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
        connect(dialog,&QDialog::finished,this,[this,dialog,combo,arguments,done,operationGate](int result) mutable {
            if (selection_==dialog) selection_=nullptr;
            if (result!=QDialog::Accepted || !operationGate() || !validId(QJsonValue(combo->currentData().toString()))) {done(failure(QStringLiteral("업무 완료를 취소했습니다.")),false);return;}
            arguments.insert(QStringLiteral("id"),combo->currentData().toString());dispatch(QStringLiteral("complete_task"),arguments,done,operationGate);
        });
        dialog->open();
    },operationGate);
}
void PetAssistantController::shutdown()
{ if (shuttingDown_) return;saveTimer_.stop();saveConversation();shuttingDown_=true;if (restoreChoice_) restoreChoice_->reject();pendingText_.clear();if (selection_) selection_->reject();if (questions_) questions_->reject();backend_->shutdown();tasks_->shutdown(); }
void PetAssistantController::askQuestions(QJsonValue id,QJsonArray questions)
{
    if (questions_ || questions.isEmpty() || questions.size()>3 || shuttingDown_) {backend_->answerQuestions(id,{});return;}
    auto* dialog=new QDialog(&panel_);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(QStringLiteral("도우미 질문"));
    auto* layout=new QVBoxLayout(dialog);QList<QPair<QString,QComboBox*>> fields;QSet<QString> ids;
    for (const auto value:questions) {
        const auto question=value.toObject();const QString key=question.value(QStringLiteral("id")).toString();
        if (key.isEmpty() || ids.contains(key) || question.value(QStringLiteral("question")).toString().size()>4000) {delete dialog;backend_->answerQuestions(id,{});return;}
        ids.insert(key);auto* label=new QLabel(question.value(QStringLiteral("question")).toString(),dialog);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);layout->addWidget(label);
        auto* input=new QComboBox(dialog);input->setEditable(true);input->addItem(QString());input->lineEdit()->setMaxLength(4000);
        if (question.value(QStringLiteral("isSecret")).toBool()) input->lineEdit()->setEchoMode(QLineEdit::Password);
        const auto options=question.value(QStringLiteral("options")).toArray();
        if (options.size()>20) {delete dialog;backend_->answerQuestions(id,{});return;}
        for (const auto option:options) {const auto data=option.toObject();input->addItem(data.value(QStringLiteral("label")).toString().left(200));input->setItemData(input->count()-1,data.value(QStringLiteral("description")).toString().left(1000),Qt::ToolTipRole);}
        layout->addWidget(input);fields.append(qMakePair(key,input));
    }
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,dialog);layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    questions_=dialog;asyncQuestion_=false;
    connect(dialog,&QDialog::finished,this,[this,id,fields](int result) {
        questions_=nullptr;QJsonObject answers;
        for (const auto& field:fields) answers.insert(field.first,QJsonObject{{QStringLiteral("answers"),result==QDialog::Accepted ? QJsonArray{field.second->currentText()} : QJsonArray{}}});
        backend_->answerQuestions(id,answers);
    });dialog->open();
}
void PetAssistantController::askAsyncQuestions(QString,QJsonArray questions)
{
    if (questions_ || questions.isEmpty() || questions.size()>3 || shuttingDown_) return;
    auto* dialog=new QDialog(&panel_);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(QStringLiteral("도우미 질문"));
    auto* layout=new QVBoxLayout(dialog);QList<QPair<QString,QComboBox*>> fields;
    for (const auto value:questions) {
        const auto question=value.toObject();const QString title=question.value(QStringLiteral("title")).toString();
        if (title.isEmpty() || title.size()>4000 || question.value(QStringLiteral("options")).toArray().size()>20) {delete dialog;return;}
        auto* label=new QLabel(title,dialog);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);layout->addWidget(label);
        auto* input=new QComboBox(dialog);input->setEditable(true);input->addItem(QString());input->lineEdit()->setMaxLength(4000);
        for (const auto option:question.value(QStringLiteral("options")).toArray()) input->addItem(option.toString().left(200));
        layout->addWidget(input);fields.append(qMakePair(title,input));
    }
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,dialog);layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);questions_=dialog;asyncQuestion_=true;
    connect(dialog,&QDialog::finished,this,[this,fields](int result) {
        questions_=nullptr;if (result!=QDialog::Accepted || shuttingDown_) return;
        QStringList answers;for (const auto& field:fields) if (!field.second->currentText().trimmed().isEmpty()) answers.append(field.first+QStringLiteral(": ")+field.second->currentText());
        const QString text=answers.join('\n');if (text.isEmpty() || text.size()>4000) return;
        panel_.appendUserMessage(text);if (!backend_->isBusy()) panel_.beginAssistantMessage();backend_->answerAsyncQuestions(text);
    });dialog->open();
}

void PetAssistantController::saveConversation()
{
    if (restorePending_ || restoring_ || persistenceDisabled_ || shuttingDown_) return;
    QString error;
    if (!store_.save(panel_.snapshot(),error)) panel_.setStorageNotice(error);
    else panel_.setStorageNotice(QString());
}
void PetAssistantController::submitToBackend(const QString& text)
{backend_->submit(text,restoredReference_);}
void PetAssistantController::resetConversation()
{
    saveTimer_.stop();restoring_=true;restorePending_=false;panel_.setRestorePending(false);
    backend_->newConversation();restoredReference_=QJsonArray();savedConversation_={};restoreError_.clear();
    panel_.resetConversation();knownTasks_.clear();successful_.clear();lastText_.clear();lastFailed_=false;
    QString error;persistenceDisabled_=!store_.clear(error);panel_.setStorageNotice(error);restoring_=false;
    // Keep unresolved retry IDs in memory only, as before. Never replay them.
}
void PetAssistantController::offerConversationRestore()
{
    if (!restorePending_ || shuttingDown_) return;
    if (restoreChoice_) {restoreChoice_->raise();restoreChoice_->activateWindow();return;}
    auto* dialog=new QDialog(&panel_);dialog->setObjectName(QStringLiteral("petConversationRestore"));dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setAttribute(Qt::WA_QuitOnClose,false);dialog->setMinimumWidth(360);
    dialog->setWindowTitle(QStringLiteral("이전 대화"));restoreChoice_=dialog;
    auto* layout=new QVBoxLayout(dialog);
    auto* label=new QLabel(restoreError_.isEmpty() ? QStringLiteral("이전 대화와 작성 중 입력이 있어. 이어 말할까?\n이전 요청은 자동으로 다시 실행하지 않아.") : restoreError_,dialog);
    label->setTextFormat(Qt::PlainText);label->setWordWrap(true);layout->addWidget(label);
    auto* buttons=new QDialogButtonBox(dialog);layout->addWidget(buttons);
    if (restoreError_.isEmpty()) {
        auto* resume=buttons->addButton(QStringLiteral("이어 말하기"),QDialogButtonBox::AcceptRole);resume->setObjectName(QStringLiteral("resumeConversation"));
        connect(resume,&QPushButton::clicked,dialog,[dialog]() {dialog->done(1);});
    }
    auto* fresh=buttons->addButton(QStringLiteral("새 대화"),QDialogButtonBox::DestructiveRole);fresh->setObjectName(QStringLiteral("startNewConversation"));
    connect(fresh,&QPushButton::clicked,dialog,[dialog]() {dialog->done(2);});
    auto* close=buttons->addButton(QStringLiteral("닫기"),QDialogButtonBox::RejectRole);connect(close,&QPushButton::clicked,dialog,&QDialog::reject);
    connect(dialog,&QDialog::finished,this,[this,dialog](int result) {
        if (restoreChoice_==dialog) restoreChoice_=nullptr;
        if (shuttingDown_ || result==0) return;
        if (result==2) {resetConversation();panel_.showNear(pet_.geometry());return;}
        if (result!=1 || !restoreError_.isEmpty()) return;
        restoring_=true;restorePending_=false;panel_.setRestorePending(false);
        restoredReference_=savedConversation_.messages;panel_.restoreConversation(savedConversation_);savedConversation_={};restoring_=false;
        panel_.showNear(pet_.geometry());saveConversation();
    });
    dialog->open();
}
