using System.Globalization;
using System.IO;
using JSWorkPartner.CSharp.Models;

namespace JSWorkPartner.CSharp.Data;

public sealed class TaskRepository
{
    private const string DatabaseFileName = "work_manager.db";

    public TaskRepository(string? databasePath = null) =>
        DatabasePath = databasePath ?? GetDefaultDatabasePath();

    public string DatabasePath { get; }

    public void Initialize()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(DatabasePath)
            ?? throw new InvalidOperationException("DB 저장 폴더를 확인할 수 없습니다."));
        using var database = Open();
        database.Execute("PRAGMA cache_size = -512");
        using (var check = database.Prepare("PRAGMA quick_check(1)"))
        {
            if (!check.Step() || !string.Equals(check.Text(0), "ok", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("DB 파일이 손상되었을 수 있습니다.");
        }
        database.Execute("""
            CREATE TABLE IF NOT EXISTS tasks (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                sequence INTEGER NOT NULL UNIQUE,
                title TEXT NOT NULL,
                description TEXT NOT NULL DEFAULT '',
                external_url TEXT NULL,
                importance INTEGER NOT NULL DEFAULT 1,
                deadline TEXT NULL,
                status TEXT NOT NULL DEFAULT 'pending'
                    CHECK(status IN ('pending', 'completed')),
                created_at TEXT NOT NULL,
                completed_at TEXT NULL
            )
            """);
        EnsureColumn(database, "title", "ALTER TABLE tasks ADD COLUMN title TEXT NOT NULL DEFAULT ''");
        database.Execute("UPDATE tasks SET title = description WHERE title = '' AND description <> ''");
        EnsureColumn(database, "external_url", "ALTER TABLE tasks ADD COLUMN external_url TEXT NULL");
        database.Execute("CREATE INDEX IF NOT EXISTS idx_tasks_status_sequence ON tasks(status, sequence)");
    }

    public IReadOnlyList<TaskItem> LoadTasks(bool completed)
    {
        using var database = Open();
        using var statement = database.Prepare("""
            SELECT id, sequence, title, description, external_url, importance,
                   deadline, status, created_at, completed_at
              FROM tasks WHERE status = ? ORDER BY sequence ASC
            """);
        statement.Bind(1, completed ? "completed" : "pending");
        var tasks = new List<TaskItem>();
        while (statement.Step())
        {
            tasks.Add(new TaskItem
            {
                Id = statement.Int64(0), Sequence = statement.Int64(1),
                Title = statement.Text(2), Details = statement.Text(3),
                ExternalUrl = statement.IsNull(4) ? null : statement.Text(4),
                Importance = statement.Int32(5),
                Deadline = statement.IsNull(6) ? null : statement.Text(6),
                Completed = statement.Text(7) == "completed",
                CreatedAt = ParseDateTime(statement.Text(8)),
                CompletedAt = statement.IsNull(9) ? null : ParseDateTime(statement.Text(9))
            });
        }
        return tasks;
    }

    public TaskItem AddTask(string title, string details, string? externalUrl,
        int importance, string? deadline)
    {
        using var database = Open();
        database.Execute("BEGIN IMMEDIATE");
        try
        {
            long sequence;
            using (var sequenceStatement = database.Prepare(
                       "SELECT COALESCE(MAX(sequence), 0) + 1 FROM tasks"))
            {
                if (!sequenceStatement.Step()) throw new InvalidOperationException("접수 순번을 생성하지 못했습니다.");
                sequence = sequenceStatement.Int64(0);
            }
            var createdAt = DateTimeOffset.Now;
            using (var statement = database.Prepare("""
                INSERT INTO tasks
                    (sequence, title, description, external_url, importance, deadline, status, created_at)
                VALUES (?, ?, ?, ?, ?, ?, 'pending', ?)
                """))
            {
                statement.Bind(1, sequence);
                statement.Bind(2, title);
                statement.Bind(3, details);
                statement.Bind(4, externalUrl);
                statement.Bind(5, importance);
                statement.Bind(6, deadline);
                statement.Bind(7, ToDatabaseDateTime(createdAt));
                statement.ExecuteNonQuery();
            }
            var id = database.LastInsertRowId;
            database.Execute("COMMIT");
            return new TaskItem
            {
                Id = id, Sequence = sequence, Title = title, Details = details,
                ExternalUrl = externalUrl, Importance = importance, Deadline = deadline,
                CreatedAt = createdAt
            };
        }
        catch
        {
            try { database.Execute("ROLLBACK"); } catch { }
            throw;
        }
    }

    public void UpdateTask(long id, string title, string details, string? externalUrl,
        int importance, string? deadline)
    {
        using var database = Open();
        using var statement = database.Prepare("""
            UPDATE tasks SET title = ?, description = ?, external_url = ?, importance = ?, deadline = ?
             WHERE id = ?
            """);
        statement.Bind(1, title);
        statement.Bind(2, details);
        statement.Bind(3, externalUrl);
        statement.Bind(4, importance);
        statement.Bind(5, deadline);
        statement.Bind(6, id);
        ExecuteForSingleRow(database, statement, "수정할 퀘스트를 찾지 못했습니다.");
    }

    public void CompleteTask(long id, DateTimeOffset completedAt)
    {
        using var database = Open();
        using var statement = database.Prepare(
            "UPDATE tasks SET status = 'completed', completed_at = ? WHERE id = ? AND status = 'pending'");
        statement.Bind(1, ToDatabaseDateTime(completedAt));
        statement.Bind(2, id);
        ExecuteForSingleRow(database, statement, "완료할 퀘스트를 찾지 못했습니다.");
    }

    public void ReopenTask(long id)
    {
        using var database = Open();
        using var statement = database.Prepare(
            "UPDATE tasks SET status = 'pending', completed_at = NULL WHERE id = ? AND status = 'completed'");
        statement.Bind(1, id);
        ExecuteForSingleRow(database, statement, "복원할 완료 퀘스트를 찾지 못했습니다.");
    }

    public void DeleteTask(long id)
    {
        using var database = Open();
        using var statement = database.Prepare("DELETE FROM tasks WHERE id = ?");
        statement.Bind(1, id);
        ExecuteForSingleRow(database, statement, "삭제할 퀘스트를 찾지 못했습니다.");
    }

    public bool TitleExists(string title, long excludedId = 0)
    {
        using var database = Open();
        using var statement = database.Prepare(
            "SELECT 1 FROM tasks WHERE title = ? COLLATE NOCASE AND id <> ? LIMIT 1");
        statement.Bind(1, title.Trim());
        statement.Bind(2, excludedId);
        return statement.Step();
    }

    private NativeSqlite Open() => new(DatabasePath);

    private static void ExecuteForSingleRow(NativeSqlite database,
        NativeSqlite.Statement statement, string missingMessage)
    {
        statement.ExecuteNonQuery();
        if (database.Changes() != 1) throw new InvalidOperationException(missingMessage);
    }

    private static void EnsureColumn(NativeSqlite database, string column, string migrationSql)
    {
        using var statement = database.Prepare("PRAGMA table_info(tasks)");
        while (statement.Step())
        {
            if (string.Equals(statement.Text(1), column, StringComparison.OrdinalIgnoreCase)) return;
        }
        statement.Dispose();
        database.Execute(migrationSql);
    }

    private static DateTimeOffset ParseDateTime(string value) =>
        DateTimeOffset.TryParse(value, CultureInfo.InvariantCulture,
            DateTimeStyles.AllowWhiteSpaces, out var result) ? result : DateTimeOffset.MinValue;

    private static string ToDatabaseDateTime(DateTimeOffset value) =>
        value.ToString("yyyy-MM-ddTHH:mm:sszzz", CultureInfo.InvariantCulture);

    private static string GetDefaultDatabasePath()
    {
        var overrideDirectory = Environment.GetEnvironmentVariable("JSWORKPARTNER_DATA_DIR");
        if (string.IsNullOrWhiteSpace(overrideDirectory))
            overrideDirectory = Environment.GetEnvironmentVariable("JSWORKMANAGER_DATA_DIR");
        var directory = string.IsNullOrWhiteSpace(overrideDirectory)
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "JSWorkManager", "JSWorkManager") // Keep the existing shared DB location.
            : overrideDirectory;
        return Path.Combine(directory, DatabaseFileName);
    }
}
