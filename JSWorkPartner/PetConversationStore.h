#pragma once
#include <QtCore/QJsonArray>
#include <QtCore/QString>

// Only display text is persisted. Never protocol, auth, thread IDs or tool calls.
struct PetConversationSnapshot
{
    QJsonArray messages;
    QString draft;
    bool interrupted=false;
    bool limited=false;
    bool isEmpty() const { return messages.isEmpty() && draft.isEmpty(); }
};

class PetConversationStore final
{
public:
    enum class LoadResult { Missing, Loaded, Invalid };
    static constexpr int MaximumMessages=100;
    static constexpr int MaximumMessageLength=32768;
    static constexpr int MaximumDraftLength=4000;
    static constexpr int MaximumPlainBytes=256*1024;
    static constexpr int MaximumFileBytes=MaximumPlainBytes+16*1024;
    explicit PetConversationStore(const QString& database);
    LoadResult load(PetConversationSnapshot& snapshot,QString& error) const;
    bool save(const PetConversationSnapshot& snapshot,QString& error) const;
    bool clear(QString& error) const;
    QString path() const { return path_; }
    static bool validate(const PetConversationSnapshot& snapshot);
private:
    QString path_;
};
