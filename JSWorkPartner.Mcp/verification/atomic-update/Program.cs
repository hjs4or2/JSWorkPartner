using System.Diagnostics;
using System.Text.Json;
using JSWorkPartner.CSharp.Data;
using JSWorkPartner.CSharp.Models;
using JSWorkPartner.CSharp.Services;
using JSWorkPartner.Mcp;

// All DB paths must be explicitly supplied beneath this project's ignored fixture directory.
var artifactRoot = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory,
    "../../../../artifacts"));
if (args.Length != 1 || !Path.IsPathFullyQualified(args[0])) return 2;
var directory = Path.GetFullPath(args[0]);
if (!directory.StartsWith(artifactRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)
    || Directory.Exists(directory)) return 2;
Directory.CreateDirectory(directory);
var passed = new List<string>();
var path = Path.Combine(directory, "atomic-update.db");
var repository = new TaskRepository(path);
repository.Initialize();
var service = new TaskService(repository);
var mcp = new TaskRepository(path, allowCreate: false);
mcp.ValidateMcpDatabase();
const string DuplicateMessage = "같은 제목의 퀘스트가 이미 있습니다. 다른 제목을 입력해 주세요.";
Exception? failure = null;
try
{
    var original = service.Add("A", "original", null, 2, null);
    // Reproduce the exact stale GUI precheck -> external MCP writer -> repository update window.
    Check(!repository.TitleExists("X", original.Id), "GUI precheck passes before MCP insert");
    mcp.CreateMcpTask(Key(), "X", "MCP writer", null, 1, null, CancellationToken.None);
    ExpectDuplicate(() => repository.UpdateTask(original.Id, "X", "must not save", null, 5, null));
    Check(Same(Find(original.Id), original), "stale precheck rename refused and original row unchanged");
    Check(repository.LoadTasks(false).Count(task => task.Title == "X") == 1, "exactly one X remains after stale precheck");

    service.Update(original.Id, " A ", " normalized ", "example.com", 3, new DateTime(2026, 10, 8));
    var updated = Find(original.Id);
    Check(updated.Title == "A" && updated.Details == "normalized" && updated.Importance == 3
        && updated.ExternalUrl == "https://example.com" && updated.Deadline == "2026-10-08",
        "self-title update succeeds and GUI normalization preserved");
    repository.UpdateTask(original.Id, "A", updated.Details, updated.ExternalUrl, updated.Importance, updated.Deadline);
    Check(Same(Find(original.Id), updated), "unchanged self-title repository update succeeds");
    ExpectDuplicate(() => repository.UpdateTask(original.Id, "x", "no", null, 1, null));
    Check(Same(Find(original.Id), updated), "NOCASE duplicate rejection preserves row");

    // A trigger makes SQLite abort after UPDATE work has begun. The entire statement/transaction must roll back.
    using (var db = new NativeSqlite(path, allowCreate: false))
        db.Execute("CREATE TRIGGER fixture_update_failure AFTER UPDATE ON tasks BEGIN SELECT RAISE(ABORT, 'fixture update failure'); END");
    ExpectFailure(() => repository.UpdateTask(original.Id, "trigger rejected", "no", null, 1, null));
    Check(Same(Find(original.Id), updated), "SQLite UPDATE failure rolls back every field");
    using (var db = new NativeSqlite(path, allowCreate: false)) db.Execute("DROP TRIGGER fixture_update_failure");

    using (var locker = new NativeSqlite(path, allowCreate: false))
    {
        locker.Execute("BEGIN IMMEDIATE");
        var timer = Stopwatch.StartNew();
        ExpectFailure(() => repository.UpdateTask(original.Id, "busy rejected", "no", null, 1, null));
        Check(timer.Elapsed.TotalSeconds is >= 4 and <= 9, "UPDATE busy timeout remains finite at five seconds");
        locker.Execute("ROLLBACK");
    }
    Check(Same(Find(original.Id), updated), "busy failure leaves original row unchanged");
    repository.UpdateTask(original.Id, "lock released", "success", null, 4, null);
    Check(Find(original.Id).Title == "lock released", "normal update succeeds after lock released");
    try { repository.UpdateTask(long.MaxValue, "unused missing title", "", null, 1, null); throw new Exception("missing row accepted"); }
    catch (InvalidOperationException error)
    { Check(error.Message == "수정할 퀘스트를 찾지 못했습니다.", "missing-row error contract preserved"); }

    for (var iteration = 0; iteration < 30; iteration++)
    {
        var source = service.Add("race source " + iteration, "source", null, 1, null);
        var target = "race target " + iteration;
        Check(!repository.TitleExists(target, source.Id), "race precheck " + iteration);
        using var start = new ManualResetEventSlim();
        var rename = Task.Run(() =>
        {
            start.Wait();
            try { repository.UpdateTask(source.Id, target, "renamed", null, 1, null); return true; }
            catch (InvalidOperationException error) when (error.Message == DuplicateMessage) { return false; }
        });
        var create = Task.Run(() =>
        {
            start.Wait();
            try { mcp.CreateMcpTask(Key(), target, "created", null, 1, null, CancellationToken.None); return true; }
            catch (TaskToolException error) when (error.Code == "duplicate_title") { return false; }
        });
        start.Set();
        Task.WaitAll(rename, create);
        Check(rename.Result != create.Result && repository.LoadTasks(false).Count(task => task.Title == target) == 1,
            "concurrent MCP create/rename has one winner and one title " + iteration);
        Check(Find(source.Id).Title == (rename.Result ? target : source.Title), "race loser preserves original " + iteration);
    }
}
catch (Exception error) { failure = error; }
File.WriteAllText(Path.Combine(directory, "results.json"), JsonSerializer.Serialize(new
{
    passed, count = passed.Count, success = failure is null, error = failure?.ToString(), fixtureDirectory = directory
}, new JsonSerializerOptions { WriteIndented = true }));
if (failure is not null) { Console.Error.WriteLine(failure); return 1; }
Console.WriteLine(JsonSerializer.Serialize(new { success = true, passed = passed.Count, artifacts = directory }));
return 0;

void Check(bool condition, string label)
{
    if (!condition) throw new InvalidOperationException(label);
    passed.Add(label);
}
void ExpectDuplicate(Action action)
{
    try { action(); }
    catch (InvalidOperationException error) when (error.Message == DuplicateMessage) { passed.Add("duplicate error expression preserved"); return; }
    throw new Exception("Expected duplicate rejection");
}
void ExpectFailure(Action action)
{
    try { action(); }
    catch (InvalidOperationException) { return; }
    throw new Exception("Expected database failure");
}
TaskItem Find(long id) => repository.LoadTasks(false).Single(task => task.Id == id);
static string Key() => Guid.NewGuid().ToString("D");
static bool Same(TaskItem left, TaskItem right) => left.Id == right.Id && left.Sequence == right.Sequence
    && left.Title == right.Title && left.Details == right.Details && left.ExternalUrl == right.ExternalUrl
    && left.Importance == right.Importance && left.Deadline == right.Deadline && left.Completed == right.Completed
    && left.CreatedAt.ToUnixTimeSeconds() == right.CreatedAt.ToUnixTimeSeconds() && left.CompletedAt == right.CompletedAt;
