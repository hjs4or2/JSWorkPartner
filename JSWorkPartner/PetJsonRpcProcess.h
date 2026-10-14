#pragma once
#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QTextCodec>
#include <functional>

// Both protocols use newline-delimited UTF-8 JSON. Never log protocol payloads.
class PetJsonRpcProcess final : public QObject
{
public:
    using Reply = std::function<void(const QJsonObject&, const QString&)>;
    explicit PetJsonRpcProcess(QObject* parent = nullptr) : QObject(parent) {}
    ~PetJsonRpcProcess() override { stop(); }
    std::function<void()> started;
    std::function<void(const QJsonObject&)> notification;
    std::function<void(const QString&)> failed;
    std::function<void()> stopped;
    bool running() const { return process_ && process_->state() != QProcess::NotRunning; }
    void start(const QString& program, const QStringList& arguments, const QString& cwd,
               const QProcessEnvironment& environment = QProcessEnvironment::systemEnvironment())
    {
        if (process_) return;
        buffer_.clear(); stopping_ = false;
        auto* p = new QProcess;
        process_ = p;
        p->setWorkingDirectory(cwd); p->setProcessEnvironment(environment);
#ifdef Q_OS_WIN
        p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
            args->flags |= 0x08000000; // CREATE_NO_WINDOW; owned console child only.
        });
#endif
        connect(p, &QProcess::started, this, [this,p]() { if (p == process_ && started) started(); });
        connect(p, &QProcess::readyReadStandardError, this, [p]() { p->readAllStandardError(); });
        connect(p, &QProcess::readyReadStandardOutput, this, [this,p]() {
            if (p != process_ || stopping_) { p->readAllStandardOutput(); return; }
            buffer_ += p->readAllStandardOutput();
            while (true) {
                const int end = buffer_.indexOf('\n');
                if (end < 0) break;
                if (end > MaximumLine) { fail(QStringLiteral("프로토콜 응답 크기 제한을 초과했습니다.")); return; }
                const QByteArray line = buffer_.left(end).trimmed(); buffer_.remove(0,end+1);
                if (line.isEmpty()) continue;
                QTextCodec::ConverterState state;
                QTextCodec::codecForName("UTF-8")->toUnicode(line.constData(),line.size(),&state);
                QJsonParseError error; const auto doc = QJsonDocument::fromJson(line,&error);
                if (state.invalidChars || error.error != QJsonParseError::NoError || !doc.isObject()) {
                    fail(QStringLiteral("잘못된 UTF-8 또는 JSON 응답입니다.")); return;
                }
                const auto obj=doc.object();
                const QString key=idKey(obj.value(QStringLiteral("id")));
                if (!obj.contains(QStringLiteral("method")) && pending_.contains(key)) {
                    auto reply=pending_.take(key);
                    reply(obj.value(QStringLiteral("result")).toObject(),
                          obj.contains(QStringLiteral("error")) ? QStringLiteral("요청을 처리하지 못했습니다.") : QString());
                } else if (obj.contains(QStringLiteral("method")) && notification) notification(obj);
                if (p != process_ || stopping_) return;
            }
            if (buffer_.size() > MaximumLine) fail(QStringLiteral("프로토콜 줄 버퍼 제한을 초과했습니다."));
        });
        connect(p, static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),
                this, [this,p](int,QProcess::ExitStatus) {
            if (p != process_) return;
            process_=nullptr; p->deleteLater();
            const bool expected=stopping_; stopping_=false;
            drain(QStringLiteral("연결이 종료되었습니다. 전송한 업무 변경의 결과는 확인이 필요합니다."));
            if (!expected && failed) failed(QStringLiteral("도우미 연결이 예기치 않게 종료되었습니다."));
            if (stopped) stopped();
        });
        connect(p, &QProcess::errorOccurred, this, [this,p](QProcess::ProcessError e) {
            if (p == process_ && e == QProcess::FailedToStart) {
                process_=nullptr; p->deleteLater(); drain(QStringLiteral("실행 파일을 시작하지 못했습니다."));
                if (failed) failed(QStringLiteral("실행 파일을 시작하지 못했습니다."));
                if (stopped) stopped();
            }
        });
        p->start(program,arguments);
    }
    QString request(const QString& method, const QJsonObject& params, Reply reply, int timeoutMs=30000)
    {
        const QString id=QString::number(++serial_);
        pending_.insert(id,std::move(reply));
        send({{QStringLiteral("jsonrpc"),QStringLiteral("2.0")},{QStringLiteral("id"),id},
              {QStringLiteral("method"),method},{QStringLiteral("params"),params}});
        QTimer::singleShot(timeoutMs,this,[this,id]() {
            if (!pending_.contains(id)) return;
            auto reply=pending_.take(id); reply({},QStringLiteral("응답 대기 시간이 초과되었습니다. 전송 후 업무 변경 여부는 확인이 필요합니다."));
        });
        return id;
    }
    void notify(const QString& method, const QJsonObject& params={})
    { send({{QStringLiteral("jsonrpc"),QStringLiteral("2.0")},{QStringLiteral("method"),method},{QStringLiteral("params"),params}}); }
    void respond(const QJsonValue& id,const QJsonObject& result)
    { send({{QStringLiteral("jsonrpc"),QStringLiteral("2.0")},{QStringLiteral("id"),id},{QStringLiteral("result"),result}}); }
    void reject(const QJsonValue& id)
    { send({{QStringLiteral("jsonrpc"),QStringLiteral("2.0")},{QStringLiteral("id"),id},{QStringLiteral("error"),QJsonObject{{QStringLiteral("code"),-32601},{QStringLiteral("message"),QStringLiteral("Tool not permitted")}}}}); }
    void closeInput()
    { if (process_) { stopping_=true;const QPointer<QProcess> owned=process_;process_->closeWriteChannel(); QTimer::singleShot(1500,this,[this,owned]() { if (owned && process_==owned) owned->kill(); }); } }
    void stop()
    {
        if (!process_) return;
        stopping_=true; auto* p=process_; process_=nullptr;
        disconnect(p,nullptr,this,nullptr);
        connect(p,static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),p,&QObject::deleteLater);
        p->kill(); if (p->state()==QProcess::NotRunning) p->deleteLater();
        drain(QStringLiteral("연결을 종료했습니다. 전송한 업무 변경의 결과는 확인이 필요합니다."));
    }
    static QString idKey(const QJsonValue& id)
    { return id.isString() ? id.toString() : (id.isDouble() ? QString::number(id.toDouble(),'g',17) : QString()); }
private:
    void send(const QJsonObject& value)
    { if (running() && !stopping_) process_->write(QJsonDocument(value).toJson(QJsonDocument::Compact)+'\n'); }
    void drain(const QString& error)
    { const auto pending=pending_; pending_.clear(); for (const auto& cb:pending) cb({},error); }
    void fail(const QString& error) { stop(); if (failed) failed(error); }
    static constexpr int MaximumLine=1024*1024;
    QProcess* process_=nullptr;
    QByteArray buffer_;
    QHash<QString,Reply> pending_;
    quint64 serial_=0;
    bool stopping_=false;
};
