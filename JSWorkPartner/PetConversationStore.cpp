#include "PetConversationStore.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QTextCodec>
#define NOMINMAX
#include <Windows.h>
#include <wincrypt.h>

namespace {
const QByteArray Magic("JWPCHAT1\0",9);
QByteArray protect(QByteArray& input,bool encrypt,bool& ok)
{
    DATA_BLOB source{static_cast<DWORD>(input.size()),reinterpret_cast<BYTE*>(input.data())},output{};
    QByteArray entropy("JSWorkPartner.PetConversation.v1");
    DATA_BLOB extra{static_cast<DWORD>(entropy.size()),reinterpret_cast<BYTE*>(entropy.data())};
    ok=(encrypt ? CryptProtectData(&source,L"JSWorkPartner conversation",&extra,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)
                : CryptUnprotectData(&source,nullptr,&extra,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output))!=FALSE;
    QByteArray result;
    if (ok) {if (output.cbData<=PetConversationStore::MaximumFileBytes) result=QByteArray(reinterpret_cast<char*>(output.pbData),static_cast<int>(output.cbData));else ok=false;SecureZeroMemory(output.pbData,output.cbData);LocalFree(output.pbData);}
    if (!input.isEmpty()) SecureZeroMemory(input.data(),input.size());
    return result;
}
QByteArray encode(const PetConversationSnapshot& s)
{return QJsonDocument(QJsonObject{{QStringLiteral("version"),1},{QStringLiteral("messages"),s.messages},{QStringLiteral("draft"),s.draft},{QStringLiteral("interrupted"),s.interrupted},{QStringLiteral("limited"),s.limited}}).toJson(QJsonDocument::Compact);}
}
PetConversationStore::PetConversationStore(const QString& database)
    :path_(QFileInfo(database).absolutePath()+QStringLiteral("/pet-conversation.dpapi")) {}
bool PetConversationStore::validate(const PetConversationSnapshot& s)
{
    if (s.messages.size()>MaximumMessages || s.draft.size()>MaximumDraftLength) return false;
    for (const auto value:s.messages) {
        if (!value.isObject()) return false;
        const auto m=value.toObject();const auto role=m.value(QStringLiteral("role"));
        if (m.size()!=3 || !role.isString() || (role.toString()!=QStringLiteral("user") && role.toString()!=QStringLiteral("assistant")) ||
            !m.value(QStringLiteral("text")).isString() || m.value(QStringLiteral("text")).toString().size()>(role.toString()==QStringLiteral("user") ? MaximumDraftLength : MaximumMessageLength) ||
            !m.value(QStringLiteral("interrupted")).isBool() || (role.toString()==QStringLiteral("user") && m.value(QStringLiteral("interrupted")).toBool())) return false;
    }
    return encode(s).size()<=MaximumPlainBytes;
}
PetConversationStore::LoadResult PetConversationStore::load(PetConversationSnapshot& s,QString& error) const
{
    s={};error.clear();QFile f(path_);
    if (!f.exists()) return LoadResult::Missing;
    const auto invalid=[&]() {error=QStringLiteral("이전 대화를 읽을 수 없어. 새 대화를 시작할 수 있어. (파일 손상·버전·복호화 또는 읽기 오류)");return LoadResult::Invalid;};
    if (f.size()<Magic.size()+1 || f.size()>MaximumFileBytes || !f.open(QIODevice::ReadOnly)) return invalid();
    QByteArray encrypted=f.read(MaximumFileBytes+1);if (!encrypted.startsWith(Magic) || encrypted.size()>MaximumFileBytes) return invalid();
    encrypted.remove(0,Magic.size());bool ok=false;QByteArray plain=protect(encrypted,false,ok);
    if (!ok || plain.size()>MaximumPlainBytes) return invalid();
    QTextCodec::ConverterState utf8;QTextCodec::codecForName("UTF-8")->toUnicode(plain.constData(),plain.size(),&utf8);
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(plain,&parse);if (!plain.isEmpty()) SecureZeroMemory(plain.data(),plain.size());
    if (utf8.invalidChars || parse.error!=QJsonParseError::NoError || !doc.isObject()) return invalid();
    const auto o=doc.object();
    if (o.size()!=5 || !o.value(QStringLiteral("version")).isDouble() || o.value(QStringLiteral("version")).toDouble()!=1 ||
        !o.value(QStringLiteral("messages")).isArray() || !o.value(QStringLiteral("draft")).isString() ||
        !o.value(QStringLiteral("interrupted")).isBool() || !o.value(QStringLiteral("limited")).isBool()) return invalid();
    PetConversationSnapshot candidate{o.value(QStringLiteral("messages")).toArray(),o.value(QStringLiteral("draft")).toString(),o.value(QStringLiteral("interrupted")).toBool(),o.value(QStringLiteral("limited")).toBool()};
    if (!validate(candidate)) return invalid();s=candidate;return s.isEmpty() ? LoadResult::Missing : LoadResult::Loaded;
}
bool PetConversationStore::save(const PetConversationSnapshot& s,QString& error) const
{
    error.clear();if (!validate(s)) {error=QStringLiteral("대화 저장 한도를 넘었어. 최근 대화만 보관할 수 있어.");return false;}
    if (s.isEmpty()) return clear(error);
    QByteArray plain=encode(s);bool ok=false;const auto encrypted=protect(plain,true,ok);
    if (!ok) {error=QStringLiteral("대화를 암호화하지 못했어. 이번 변경은 다음 실행에서 복원되지 않을 수 있어.");return false;}
    QSaveFile file(path_);file.setDirectWriteFallback(false);const auto bytes=Magic+encrypted;
    if (bytes.size()>MaximumFileBytes || !file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) {
        error=QStringLiteral("대화를 저장하지 못했어. 이번 변경은 다음 실행에서 복원되지 않을 수 있어.");return false;
    }
    return true;
}
bool PetConversationStore::clear(QString& error) const
{
    error.clear();if (!QFile::exists(path_) || QFile::remove(path_)) return true;
    error=QStringLiteral("이전 대화 파일을 지우지 못했어. 새 대화는 가능하지만 다음 실행에 이전 기록이 남을 수 있어.");return false;
}
