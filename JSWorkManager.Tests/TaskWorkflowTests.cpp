#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>

#include <iostream>

#include "AppConstants.h"
#include "TaskQueueViewModel.h"
#include "TaskRepository.h"

namespace
{
class TestContext
{
public:
    void verify(bool condition, const QString& message)
    {
        if (condition)
        {
            return;
        }

        ++failureCount_;
        std::cerr
            << "[FAIL] "
            << message.toUtf8().constData()
            << std::endl;
    }

    int failureCount() const
    {
        return failureCount_;
    }

private:
    int failureCount_ = 0;
};

bool createLegacyDatabase(
    const QString& databasePath,
    bool includeEmptyTitleColumn = false)
{
    const QString connectionName = QStringLiteral("legacy_test_connection");
    bool succeeded = false;
    {
        QSqlDatabase database =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        if (database.open())
        {
            QSqlQuery query(database);
            QString schema = QStringLiteral(
                "CREATE TABLE tasks ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "sequence INTEGER NOT NULL UNIQUE,");
            if (includeEmptyTitleColumn)
            {
                schema += QStringLiteral(
                    "title TEXT NOT NULL DEFAULT '',");
            }
            schema += QStringLiteral(
                "description TEXT NOT NULL DEFAULT '',"
                "importance INTEGER NOT NULL DEFAULT 1,"
                "deadline TEXT NULL,"
                "status TEXT NOT NULL DEFAULT 'pending',"
                "created_at TEXT NOT NULL,"
                "completed_at TEXT NULL"
                ")");
            succeeded = query.exec(schema);
            if (succeeded)
            {
                succeeded = query.exec(QStringLiteral(
                    "INSERT INTO tasks "
                    "(sequence, description, importance, deadline, status, created_at) "
                    "VALUES (1, 'Legacy Task', 2, NULL, 'pending', "
                    "'2026-01-01T09:00:00')"));
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return succeeded;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("JSWorkManager.Tests"));

    TestContext test;
    QTemporaryDir dataDirectory;
    test.verify(dataDirectory.isValid(), QStringLiteral("임시 DB 폴더 생성"));
    if (!dataDirectory.isValid())
    {
        return 1;
    }

    qputenv(
        "JSWORKMANAGER_DATA_DIR",
        dataDirectory.path().toUtf8());

    {
        TaskRepository repository;
        const bool initialized = repository.initialize();
        test.verify(initialized, QStringLiteral("저장소 초기화"));
        if (!initialized)
        {
            std::cerr
                << repository.lastError().toUtf8().constData()
                << std::endl;
            return 1;
        }

        TaskQueueViewModel pending(repository);
        TaskQueueViewModel completed(repository);
        test.verify(pending.loadPendingTasks(), QStringLiteral("진행 목록 로드"));
        test.verify(completed.loadCompletedTasks(), QStringLiteral("완료 목록 로드"));
        test.verify(
            pending.taskCount() == 0 && completed.taskCount() == 0,
            QStringLiteral("새 DB의 목록은 비어 있음"));

        test.verify(
            pending.addTask(
                QStringLiteral("  Alpha Task  "),
                QStringLiteral("  첫 번째 상세 설명  "),
                QStringLiteral("example.com/issues/1"),
                3,
                QStringLiteral("2030-12-31")),
            QStringLiteral("프로토콜 없는 링크를 포함한 퀘스트 등록"));
        test.verify(pending.taskCount() == 1, QStringLiteral("등록 후 목록 개수"));

        const Task alpha = pending.taskAt(0);
        test.verify(alpha.id > 0, QStringLiteral("등록 ID 생성"));
        test.verify(
            alpha.title == QStringLiteral("Alpha Task"),
            QStringLiteral("제목 공백 정규화"));
        test.verify(
            alpha.details == QStringLiteral("첫 번째 상세 설명"),
            QStringLiteral("상세 설명 공백 정규화"));
        test.verify(
            alpha.externalUrl == QStringLiteral("https://example.com/issues/1"),
            QStringLiteral("HTTPS 프로토콜 자동 보완"));
        test.verify(
            alpha.importance == 3 &&
                alpha.deadline == QStringLiteral("2030-12-31"),
            QStringLiteral("중요도와 마감일 저장"));

        test.verify(
            !pending.addTask(
                QStringLiteral("alpha task"),
                QString(),
                QString(),
                1,
                QString()),
            QStringLiteral("대소문자가 다른 중복 제목 거부"));
        test.verify(
            !pending.addTask(
                QStringLiteral("Invalid Importance"),
                QString(),
                QString(),
                6,
                QString()),
            QStringLiteral("범위를 벗어난 중요도 거부"));
        test.verify(
            !pending.addTask(
                QStringLiteral("Invalid Link"),
                QString(),
                QStringLiteral("ftp://example.com/file"),
                1,
                QString()),
            QStringLiteral("HTTP가 아닌 외부 링크 거부"));

        test.verify(
            pending.addTask(
                QStringLiteral("Beta Task"),
                QStringLiteral("두 번째 상세 설명"),
                QString(),
                1,
                QString()),
            QStringLiteral("두 번째 퀘스트 등록"));
        const qint64 betaId =
            pending.taskAt(pending.rowForTaskId(alpha.id) == 0 ? 1 : 0).id;

        pending.sort(
            TaskQueueViewModel::TitleColumn,
            Qt::DescendingOrder);
        const int alphaSortedRow = pending.rowForTaskId(alpha.id);
        test.verify(alphaSortedRow >= 0, QStringLiteral("정렬 후 ID로 행 탐색"));
        test.verify(
            pending.updateTask(
                alphaSortedRow,
                QStringLiteral("Gamma Task"),
                QStringLiteral("수정된 상세 설명"),
                QStringLiteral("https://example.com/issues/2"),
                5,
                QString()),
            QStringLiteral("정렬 후 정확한 퀘스트 수정"));

        const int gammaRow = pending.rowForTaskId(alpha.id);
        const Task gamma = pending.taskAt(gammaRow);
        test.verify(
            gamma.title == QStringLiteral("Gamma Task") &&
                gamma.importance == 5 &&
                gamma.deadline.isEmpty(),
            QStringLiteral("수정 결과 확인"));

        test.verify(
            pending.completeTask(gammaRow),
            QStringLiteral("진행 퀘스트 완료 처리"));
        test.verify(completed.loadCompletedTasks(), QStringLiteral("완료 목록 갱신"));
        test.verify(
            pending.taskCount() == 1 && completed.taskCount() == 1,
            QStringLiteral("완료 처리 후 목록 이동"));

        const int completedGammaRow = completed.rowForTaskId(alpha.id);
        test.verify(
            completedGammaRow >= 0 &&
                completed.taskAt(completedGammaRow).completedAt.isValid(),
            QStringLiteral("완료 시각 기록"));
        test.verify(
            completed.reopenTask(completedGammaRow),
            QStringLiteral("완료 퀘스트 진행 중으로 복원"));
        test.verify(pending.loadPendingTasks(), QStringLiteral("복원 후 진행 목록 갱신"));
        test.verify(
            pending.taskCount() == 2 && completed.taskCount() == 0,
            QStringLiteral("복원 후 목록 이동"));
        test.verify(
            !pending.taskAt(pending.rowForTaskId(alpha.id)).completedAt.isValid(),
            QStringLiteral("복원 시 완료 시각 초기화"));

        const int betaRow = pending.rowForTaskId(betaId);
        test.verify(betaRow >= 0, QStringLiteral("삭제 대상 ID 탐색"));
        test.verify(pending.deleteTask(betaRow), QStringLiteral("퀘스트 삭제"));
        test.verify(
            pending.taskCount() == 1 &&
                pending.rowForTaskId(betaId) == -1,
            QStringLiteral("삭제 결과 확인"));
    }

    QTemporaryDir legacyDataDirectory;
    test.verify(
        legacyDataDirectory.isValid(),
        QStringLiteral("마이그레이션 테스트 폴더 생성"));
    if (legacyDataDirectory.isValid())
    {
        const QString legacyDatabasePath =
            QDir(legacyDataDirectory.path())
                .filePath(AppConstants::DatabaseFileName);
        test.verify(
            createLegacyDatabase(legacyDatabasePath),
            QStringLiteral("이전 버전 DB 생성"));
        qputenv(
            "JSWORKMANAGER_DATA_DIR",
            legacyDataDirectory.path().toUtf8());

        TaskRepository legacyRepository;
        test.verify(
            legacyRepository.initialize(),
            QStringLiteral("이전 버전 DB 마이그레이션"));
        const QList<Task> migratedTasks =
            legacyRepository.loadTasks(false);
        test.verify(
            migratedTasks.size() == 1 &&
                migratedTasks.first().title == QStringLiteral("Legacy Task") &&
                migratedTasks.first().externalUrl.isEmpty(),
            QStringLiteral("누락 컬럼 추가 및 제목 데이터 이전"));
    }

    QTemporaryDir corruptDataDirectory;
    test.verify(
        corruptDataDirectory.isValid(),
        QStringLiteral("손상 DB 테스트 폴더 생성"));
    if (corruptDataDirectory.isValid())
    {
        const QString corruptDatabasePath =
            QDir(corruptDataDirectory.path())
                .filePath(AppConstants::DatabaseFileName);
        QFile corruptFile(corruptDatabasePath);
        const bool corruptFileCreated =
            corruptFile.open(QIODevice::WriteOnly) &&
            corruptFile.write("not a sqlite database") > 0;
        corruptFile.close();
        test.verify(
            corruptFileCreated,
            QStringLiteral("손상된 DB 파일 준비"));
        qputenv(
            "JSWORKMANAGER_DATA_DIR",
            corruptDataDirectory.path().toUtf8());

        TaskRepository corruptRepository;
        test.verify(
            !corruptRepository.initialize() &&
                !corruptRepository.lastError().isEmpty(),
            QStringLiteral("손상된 DB 시작 차단"));
    }

    QTemporaryDir interruptedMigrationDirectory;
    test.verify(
        interruptedMigrationDirectory.isValid(),
        QStringLiteral("불완전 마이그레이션 테스트 폴더 생성"));
    if (interruptedMigrationDirectory.isValid())
    {
        const QString interruptedDatabasePath =
            QDir(interruptedMigrationDirectory.path())
                .filePath(AppConstants::DatabaseFileName);
        test.verify(
            createLegacyDatabase(interruptedDatabasePath, true),
            QStringLiteral("빈 제목 컬럼이 남은 DB 생성"));
        qputenv(
            "JSWORKMANAGER_DATA_DIR",
            interruptedMigrationDirectory.path().toUtf8());

        TaskRepository repairedRepository;
        test.verify(
            repairedRepository.initialize(),
            QStringLiteral("불완전 제목 마이그레이션 복구"));
        const QList<Task> repairedTasks =
            repairedRepository.loadTasks(false);
        test.verify(
            repairedTasks.size() == 1 &&
                repairedTasks.first().title == QStringLiteral("Legacy Task"),
            QStringLiteral("빈 제목을 기존 설명에서 복구"));
    }

    if (test.failureCount() != 0)
    {
        std::cerr
            << test.failureCount()
            << " task workflow test(s) failed."
            << std::endl;
        return 1;
    }

    std::cout << "All task workflow tests passed." << std::endl;
    return 0;
}
