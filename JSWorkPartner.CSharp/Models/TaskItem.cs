namespace JSWorkPartner.CSharp.Models;

public sealed class TaskItem
{
    public long Id { get; init; }
    public long Sequence { get; init; }
    public string Title { get; set; } = string.Empty;
    public string Details { get; set; } = string.Empty;
    public string? ExternalUrl { get; set; }
    public int Importance { get; set; } = 1;
    public string? Deadline { get; set; }
    public bool Completed { get; set; }
    public DateTimeOffset CreatedAt { get; init; }
    public DateTimeOffset? CompletedAt { get; set; }

    public string ImportanceDisplay => new('★', Importance);
    public string DeadlineDisplay => string.IsNullOrWhiteSpace(Deadline) ? "X" : Deadline;
    public string DateDisplay => (Completed ? CompletedAt : CreatedAt)?.ToLocalTime().ToString("yyyy-MM-dd HH:mm") ?? "-";
}
