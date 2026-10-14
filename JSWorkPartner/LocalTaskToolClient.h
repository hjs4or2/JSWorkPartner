#pragma once
#include "PetJsonRpcProcess.h"
#include <QtCore/QSet>

class LocalTaskToolClient final : public QObject
{
    Q_OBJECT
public:
    explicit LocalTaskToolClient(QObject* parent=nullptr);
    void configure(const QString& executable,const QString& existingDatabase);
    void start();
    void shutdown();
    bool isReady() const { return ready_; }
    QJsonArray dynamicTools() const { return tools_; }
    void call(const QString& name,const QJsonObject& arguments,PetJsonRpcProcess::Reply reply);
    static bool permitted(const QString& name);
signals:
    void ready();
    void connectionError(QString message);
    void stopped();
private:
    PetJsonRpcProcess rpc_;
    QString executable_,database_;
    QJsonArray tools_;
    bool ready_=false;
};
