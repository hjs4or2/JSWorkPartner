using JSWorkPartner.CSharp.Data;
using JSWorkPartner.CSharp.Models;

namespace JSWorkPartner.CSharp.Services;

public sealed class TaskService(TaskRepository repository)
{
    public IReadOnlyList<TaskItem> LoadPending() => repository.LoadTasks(false);
    public IReadOnlyList<TaskItem> LoadCompleted() => repository.LoadTasks(true);

    public TaskItem Add(string title, string details, string? externalUrl,
        int importance, DateTime? deadline)
    {
        var input = Validate(title, details, externalUrl, importance);
        return repository.AddTask(input.Title, input.Details, input.ExternalUrl,
            importance, ToDeadline(deadline));
    }

    public void Update(long id, string title, string details, string? externalUrl,
        int importance, DateTime? deadline)
    {
        var input = Validate(title, details, externalUrl, importance);
        if (repository.TitleExists(input.Title, id))
        {
            throw new InvalidOperationException(
                "같은 제목의 퀘스트가 이미 있습니다. 다른 제목을 입력해 주세요.");
        }
        repository.UpdateTask(id, input.Title, input.Details, input.ExternalUrl,
            importance, ToDeadline(deadline));
    }

    public void Complete(long id) => repository.CompleteTask(id, DateTimeOffset.Now);
    public void Reopen(long id) => repository.ReopenTask(id);
    public void Delete(long id) => repository.DeleteTask(id);

    private static (string Title, string Details, string? ExternalUrl) Validate(
        string title, string details, string? externalUrl, int importance)
    {
        var normalizedTitle = title.Trim();
        if (normalizedTitle.Length == 0)
        {
            throw new InvalidOperationException("퀘스트 제목을 입력해 주세요.");
        }
        if (importance is < 1 or > 5)
        {
            throw new InvalidOperationException("중요도는 1부터 5 사이여야 합니다.");
        }

        var normalizedUrl = NormalizeUrl(externalUrl);
        if (normalizedUrl is not null &&
            (!Uri.TryCreate(normalizedUrl, UriKind.Absolute, out var uri) ||
             (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps) ||
             string.IsNullOrWhiteSpace(uri.Host)))
        {
            throw new InvalidOperationException("외부 링크에 올바른 웹 주소를 입력해 주세요.");
        }
        return (normalizedTitle, details.Trim(), normalizedUrl);
    }

    private static string? NormalizeUrl(string? value)
    {
        var normalized = value?.Trim();
        if (string.IsNullOrEmpty(normalized)) return null;
        return normalized.Contains("://", StringComparison.Ordinal)
            ? normalized
            : $"https://{normalized}";
    }

    private static string? ToDeadline(DateTime? date) => date?.ToString("yyyy-MM-dd");
}
