using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using JSWorkPartner.CSharp.Models;
using JSWorkPartner.Mcp;

namespace JSWorkPartner.CSharp.Data;

// Compiled only into the MCP server. The GUI's Initialize/migration path is never used.
public sealed partial class TaskRepository
{
    public void ValidateMcpDatabase()
    {
        using var db = OpenMcp();
        using var check = db.Prepare("PRAGMA quick_check(1)");
        if (!check.Step() || check.Text(0) != "ok")
            throw new TaskToolException("invalid_database", "Database integrity check failed.");
    }

    private NativeSqlite OpenMcp()
    {
        if (_allowCreate) throw new InvalidOperationException("MCP requires allowCreate=false.");
        var db = Open();
        try { ValidateTaskSchema(db); return db; }
        catch { db.Dispose(); throw; }
    }

    private static void ValidateTaskSchema(NativeSqlite db)
    {
        var expected = new Dictionary<string, (string Type, int NotNull, int Pk)>
        {
            ["id"] = ("INTEGER", 0, 1), ["sequence"] = ("INTEGER", 1, 0),
            ["title"] = ("TEXT", 1, 0), ["description"] = ("TEXT", 1, 0),
            ["external_url"] = ("TEXT", 0, 0), ["importance"] = ("INTEGER", 1, 0),
            ["deadline"] = ("TEXT", 0, 0), ["status"] = ("TEXT", 1, 0),
            ["created_at"] = ("TEXT", 1, 0), ["completed_at"] = ("TEXT", 0, 0)
        };
        using (var table = db.Prepare("SELECT type FROM sqlite_master WHERE name = 'tasks'"))
            if (!table.Step() || table.Text(0) != "table") InvalidSchema();
        using (var columns = db.Prepare("PRAGMA table_info(tasks)"))
        {
            while (columns.Step())
            {
                if (!expected.Remove(columns.Text(1), out var definition)) continue;
                if (!string.Equals(columns.Text(2), definition.Type, StringComparison.OrdinalIgnoreCase)
                    || columns.Int32(3) != definition.NotNull || columns.Int32(5) != definition.Pk)
                    InvalidSchema();
            }
        }
        if (expected.Count != 0) InvalidSchema();
        var uniqueSequence = false;
        using (var indexes = db.Prepare("PRAGMA index_list(tasks)"))
        {
            while (indexes.Step())
            {
                if (indexes.Int32(2) != 1 || indexes.Int32(4) != 0) continue;
                using var fields = db.Prepare("SELECT name FROM pragma_index_info(?)");
                fields.Bind(1, indexes.Text(1));
                if (fields.Step() && fields.Text(0) == "sequence" && !fields.Step()) uniqueSequence = true;
            }
        }
        if (!uniqueSequence) InvalidSchema();
    }

    private static void InvalidSchema() => throw new TaskToolException("invalid_database",
        "Expected the current JSWorkPartner tasks schema. Open/upgrade the database with the app first.");

    public object GetMcpTask(long id)
    {
        using var db = OpenMcp();
        return View(FindTask(db, id));
    }

    public object ListMcpTasks(string status, int limit, long cursor, string? search)
    {
        using var db = OpenMcp();
        using var query = db.Prepare("""
            SELECT id, sequence, title, description, external_url, importance,
                   deadline, status, created_at, completed_at
            FROM tasks WHERE id > ? AND (? = 'all' OR status = ?)
                AND (? IS NULL OR instr(lower(title), lower(?)) > 0)
            ORDER BY id LIMIT ?
            """);
        query.Bind(1, cursor); query.Bind(2, status); query.Bind(3, status);
        query.Bind(4, search); query.Bind(5, search); query.Bind(6, limit + 1);
        var items = new List<TaskItem>();
        while (query.Step()) items.Add(ReadTask(query));
        var nextCursor = items.Count > limit ? items[limit - 1].Id.ToString(CultureInfo.InvariantCulture) : null;
        return new { tasks = items.Take(limit).Select(View).ToArray(), nextCursor };
    }

    public string CreateMcpTask(string requestId, string title, string details, string? externalUrl,
        int importance, string? deadline, CancellationToken cancellationToken)
    {
        var input = JsonSerializer.Serialize(new { title, details, externalUrl, importance, deadline });
        return ExecuteMcpRequest(requestId, "create_task", input, cancellationToken, db =>
        {
            if (TitleExists(db, title, 0))
                throw new TaskToolException("duplicate_title", "A local task with this title already exists.");
            return InsertTask(db, title, details, externalUrl, importance, deadline);
        });
    }

    public string CompleteMcpTask(string requestId, long id, CancellationToken cancellationToken) =>
        ExecuteMcpRequest(requestId, "complete_task", id.ToString(CultureInfo.InvariantCulture), cancellationToken, db =>
        {
            var task = FindTask(db, id);
            if (task.Completed) return task;
            var time = DateTimeOffset.Now;
            using var update = db.Prepare("UPDATE tasks SET status = 'completed', completed_at = ? WHERE id = ? AND status = 'pending'");
            update.Bind(1, ToDatabaseDateTime(time)); update.Bind(2, id); update.ExecuteNonQuery();
            if (db.Changes() != 1) throw new TaskToolException("invalid_task", "Unexpected task status.");
            task.Completed = true; task.CompletedAt = time;
            return task;
        });

    private string ExecuteMcpRequest(string requestId, string operation, string input,
        CancellationToken cancellationToken, Func<NativeSqlite, TaskItem> action)
    {
        var hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(input)));
        cancellationToken.ThrowIfCancellationRequested();
        using var db = OpenMcp();
        db.Execute("BEGIN IMMEDIATE");
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            db.Execute("""
                CREATE TABLE IF NOT EXISTS mcp_requests (
                    request_id TEXT PRIMARY KEY NOT NULL,
                    operation TEXT NOT NULL,
                    input_hash TEXT NOT NULL,
                    result_json TEXT NOT NULL,
                    task_id INTEGER NOT NULL
                )
                """);
            using (var previous = db.Prepare("SELECT operation, input_hash, result_json FROM mcp_requests WHERE request_id = ?"))
            {
                previous.Bind(1, requestId);
                if (previous.Step())
                {
                    if (previous.Text(0) != operation || previous.Text(1) != hash)
                        throw new TaskToolException("request_conflict", "requestId was already used for different input or operation.");
                    var snapshot = previous.Text(2);
                    previous.Dispose();
                    cancellationToken.ThrowIfCancellationRequested();
                    db.Execute("COMMIT");
                    return snapshot;
                }
            }
            var task = action(db);
            var result = JsonSerializer.Serialize(new { task = View(task) });
            using (var save = db.Prepare("INSERT INTO mcp_requests(request_id, operation, input_hash, result_json, task_id) VALUES (?, ?, ?, ?, ?)"))
            {
                save.Bind(1, requestId); save.Bind(2, operation); save.Bind(3, hash);
                save.Bind(4, result); save.Bind(5, task.Id); save.ExecuteNonQuery();
            }
            cancellationToken.ThrowIfCancellationRequested();
            db.Execute("COMMIT");
            // Never check cancellation after commit: a retry returns this exact snapshot.
            return result;
        }
        catch { try { db.Execute("ROLLBACK"); } catch { } throw; }
    }

    private static TaskItem FindTask(NativeSqlite db, long id)
    {
        using var query = db.Prepare("""
            SELECT id, sequence, title, description, external_url, importance,
                   deadline, status, created_at, completed_at FROM tasks WHERE id = ?
            """);
        query.Bind(1, id);
        if (!query.Step()) throw new TaskToolException("not_found", "Local task was not found.");
        return ReadTask(query);
    }

    private static TaskItem ReadTask(NativeSqlite.Statement query) => new()
    {
        Id = query.Int64(0), Sequence = query.Int64(1), Title = query.Text(2), Details = query.Text(3),
        ExternalUrl = query.IsNull(4) ? null : query.Text(4), Importance = query.Int32(5),
        Deadline = query.IsNull(6) ? null : query.Text(6), Completed = query.Text(7) == "completed",
        CreatedAt = ParseDateTime(query.Text(8)), CompletedAt = query.IsNull(9) ? null : ParseDateTime(query.Text(9))
    };

    private static object View(TaskItem task) => new
    {
        id = task.Id.ToString(CultureInfo.InvariantCulture), sequence = task.Sequence.ToString(CultureInfo.InvariantCulture),
        title = task.Title, details = task.Details, externalUrl = task.ExternalUrl, importance = task.Importance,
        deadline = task.Deadline, status = task.Completed ? "completed" : "pending",
        createdAt = ToDatabaseDateTime(task.CreatedAt),
        completedAt = task.CompletedAt.HasValue ? ToDatabaseDateTime(task.CompletedAt.Value) : null
    };
}
