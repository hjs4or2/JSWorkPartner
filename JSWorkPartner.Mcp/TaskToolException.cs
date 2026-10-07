namespace JSWorkPartner.Mcp;

internal sealed class TaskToolException(string code, string message) : Exception(message)
{
    public string Code { get; } = code;
}
