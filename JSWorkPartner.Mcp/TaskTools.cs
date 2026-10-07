using System.Globalization;
using System.Text.Json;
using JSWorkPartner.CSharp.Data;
using ModelContextProtocol.Protocol;

namespace JSWorkPartner.Mcp;

internal sealed class TaskTools(TaskRepository repository)
{
    private static JsonElement Schema(string value) => JsonDocument.Parse(value).RootElement.Clone();
    public static IList<Tool> Definitions { get; } =
    [
        new Tool
        {
            Name = "create_task",
            Description = "Create a local JSWorkPartner task. Preserve the complete title exactly, including [Updater]. " +
                "Does not modify Redmine. requestId must be a UUID; reuse the same requestId and input for retries of the same intent.",
            InputSchema = Schema("""
                {"type":"object","additionalProperties":false,"required":["requestId","title"],"properties":{
                  "requestId":{"type":"string","format":"uuid","description":"Stable retry key, UUID in D format."},
                  "title":{"type":"string","minLength":1,"maxLength":500},
                  "details":{"type":"string","maxLength":20000},
                  "importance":{"type":"integer","minimum":1,"maximum":5,"default":1},
                  "deadline":{"type":"string","format":"date","description":"YYYY-MM-DD"},
                  "externalUrl":{"type":"string","format":"uri","maxLength":2048,"description":"Absolute http or https URL"}}}
                """)
        },
        new Tool
        {
            Name = "list_tasks", Description = "List local JSWorkPartner tasks in ascending ID order. No Redmine access. " +
                "Bounded pages (1..100, default 50). Pass nextCursor as cursor with the same status/search. " +
                "Search is a literal title substring (SQLite ASCII case-insensitive). Pages reflect current DB state.",
            InputSchema = Schema("""
                {"type":"object","additionalProperties":false,"properties":{
                  "status":{"type":"string","enum":["pending","completed","all"],"default":"pending"},
                  "limit":{"type":"integer","minimum":1,"maximum":100,"default":50},
                  "cursor":{"type":"string","pattern":"^[1-9][0-9]*$","description":"nextCursor from a previous page"},
                  "search":{"type":"string","minLength":1,"maxLength":500}}}
                """)
        },
        new Tool
        {
            Name = "get_task", Description = "Get one local JSWorkPartner task by its positive decimal string ID. No Redmine access.",
            InputSchema = Schema("""
                {"type":"object","additionalProperties":false,"required":["id"],"properties":{
                  "id":{"type":"string","pattern":"^[1-9][0-9]*$"}}}
                """)
        },
        new Tool
        {
            Name = "complete_task", Description = "Complete a local JSWorkPartner task; preserve the original completion time if already complete. " +
                "Does not modify Redmine. requestId must be a UUID; reuse the same key and ID for retries of the same intent.",
            InputSchema = Schema("""
                {"type":"object","additionalProperties":false,"required":["requestId","id"],"properties":{
                  "requestId":{"type":"string","format":"uuid"},"id":{"type":"string","pattern":"^[1-9][0-9]*$"}}}
                """)
        }
    ];

    public CallToolResult Call(CallToolRequestParams request, CancellationToken token)
    {
        try
        {
            token.ThrowIfCancellationRequested();
            var input = new Input(request.Arguments);
            object result;
            switch (request.Name)
            {
                case "create_task":
                    input.Only("requestId", "title", "details", "importance", "deadline", "externalUrl");
                    var requestId = input.RequestId();
                    var title = input.Text("title", 500, required: true)!;
                    if (string.IsNullOrWhiteSpace(title)) Input.Fail("title must contain a non-whitespace character.");
                    var details = input.Text("details", 20000, allowEmpty: true) ?? "";
                    var importance = input.Integer("importance", 1, 1, 5);
                    var deadline = input.Text("deadline", 10);
                    if (deadline is not null && !DateOnly.TryParseExact(deadline, "yyyy-MM-dd", CultureInfo.InvariantCulture,
                            DateTimeStyles.None, out _)) Input.Fail("deadline must be a valid YYYY-MM-DD date.");
                    var url = input.Text("externalUrl", 2048);
                    if (url is not null && (url.Any(char.IsWhiteSpace) || !Uri.TryCreate(url, UriKind.Absolute, out var uri)
                        || (uri.Scheme != "https" && uri.Scheme != "http") || string.IsNullOrWhiteSpace(uri.Host)
                        || !string.IsNullOrEmpty(uri.UserInfo))) Input.Fail("externalUrl must be an absolute http/https URL without credentials.");
                    result = Schema(repository.CreateMcpTask(requestId, title, details, url, importance, deadline, token));
                    break;
                case "list_tasks":
                    input.Only("status", "limit", "cursor", "search");
                    var status = input.Text("status", 9) ?? "pending";
                    if (status is not ("pending" or "completed" or "all")) Input.Fail("status must be pending, completed or all.");
                    result = repository.ListMcpTasks(status, input.Integer("limit", 50, 1, 100),
                        input.Id("cursor", optional: true), input.Text("search", 500));
                    break;
                case "get_task":
                    input.Only("id"); result = new { task = repository.GetMcpTask(input.Id("id")) }; break;
                case "complete_task":
                    input.Only("requestId", "id");
                    result = Schema(repository.CompleteMcpTask(input.RequestId(), input.Id("id"), token)); break;
                default: throw new TaskToolException("unknown_tool", "Unknown tool.");
            }
            return Result(result, false);
        }
        catch (TaskToolException error) { return Result(new { error = new { code = error.Code, message = error.Message } }, true); }
        catch (OperationCanceledException)
        {
            return Result(new { error = new { code = "cancelled", message = "Request cancelled. Retry mutations with the same requestId and input." } }, true);
        }
        catch (Exception)
        {
            return Result(new { error = new { code = "database_error", message = "Database operation failed. Check schema, permissions or a lock; retry mutations with the same requestId and input." } }, true);
        }
    }

    private static CallToolResult Result(object value, bool isError)
    {
        var json = JsonSerializer.SerializeToElement(value);
        return new CallToolResult
        {
            IsError = isError, StructuredContent = json,
            Content = [new TextContentBlock { Text = json.GetRawText() }]
        };
    }

    private sealed class Input(IDictionary<string, JsonElement>? values)
    {
        private readonly IDictionary<string, JsonElement> _values = values ?? new Dictionary<string, JsonElement>();
        public static void Fail(string message) => throw new TaskToolException("invalid_input", message);
        public void Only(params string[] names)
        {
            if (_values.Keys.Any(key => !names.Contains(key, StringComparer.Ordinal))) Fail("Unknown argument field.");
        }
        public string? Text(string name, int max, bool required = false, bool allowEmpty = false)
        {
            if (!_values.TryGetValue(name, out var value))
            {
                if (required) Fail($"{name} is required.");
                return null;
            }
            if (value.ValueKind != JsonValueKind.String) Fail($"{name} must be a string.");
            var text = value.GetString()!;
            if ((!allowEmpty && text.Length == 0) || text.Length > max || text.Any(c => char.IsControl(c) && c is not ('\r' or '\n' or '\t')))
                Fail($"{name} must contain 1..{max} characters without invalid control characters.");
            // Reject unpaired surrogates rather than let UTF-8 conversion silently alter the title.
            for (var i = 0; i < text.Length; i++)
                if (char.IsSurrogate(text[i]) && (!char.IsHighSurrogate(text[i]) || ++i >= text.Length || !char.IsLowSurrogate(text[i])))
                    Fail($"{name} contains invalid Unicode.");
            return text;
        }
        public int Integer(string name, int fallback, int min, int max)
        {
            if (!_values.TryGetValue(name, out var value)) return fallback;
            if (value.ValueKind != JsonValueKind.Number || !value.TryGetInt32(out var number) || number < min || number > max)
                Fail($"{name} must be an integer between {min} and {max}.");
            return value.GetInt32();
        }
        public string RequestId()
        {
            var text = Text("requestId", 36, required: true)!;
            if (!Guid.TryParseExact(text, "D", out var id) || id == Guid.Empty) Fail("requestId must be a nonempty UUID in D format.");
            return id.ToString("D");
        }
        public long Id(string name, bool optional = false)
        {
            var text = Text(name, 19, required: !optional);
            if (text is null) return 0;
            if (text[0] == '0' || text.Any(c => c is < '0' or > '9') || !long.TryParse(text, NumberStyles.None,
                    CultureInfo.InvariantCulture, out var id) || id <= 0) Fail($"{name} must be a positive decimal string within Int64 range.");
            return long.Parse(text, CultureInfo.InvariantCulture);
        }
    }
}
