using JSWorkPartner.CSharp.Data;
using JSWorkPartner.Mcp;
using ModelContextProtocol.Protocol;
using ModelContextProtocol.Server;

if (args.Length != 2 || args[0] != "--database" || !Path.IsPathFullyQualified(args[1]) || !File.Exists(args[1]))
{
    Console.Error.WriteLine("Usage: JSWorkPartner.Mcp.exe --database <absolute existing database file>");
    return 2;
}
try
{
    var repository = new TaskRepository(Path.GetFullPath(args[1]), allowCreate: false);
    repository.ValidateMcpDatabase();
    var tools = new TaskTools(repository);
    var options = new McpServerOptions
    {
        ServerInfo = new Implementation { Name = "jsworkpartner", Version = "1.0.0" },
        ServerInstructions = "Manage local JSWorkPartner tasks only. Preserve the user's full title, including [Updater]. " +
            "Use a UUID requestId for each create/complete intent and reuse it unchanged for retries, including after a lost response. " +
            "Never edit Redmine. Task IDs and list cursors are decimal strings.",
        Capabilities = new ServerCapabilities { Tools = new ToolsCapability { ListChanged = false } },
        Handlers = new McpServerHandlers
        {
            ListToolsHandler = (_, _) => ValueTask.FromResult(new ListToolsResult { Tools = TaskTools.Definitions }),
            CallToolHandler = async (request, token) => await Task.Run(() => tools.Call(request.Params!, token), token)
        }
    };
    using var shutdown = new CancellationTokenSource();
    Console.CancelKeyPress += (_, e) => { e.Cancel = true; shutdown.Cancel(); };
    // SDK owns lifecycle, protocol negotiation, cancellation and stdio framing.
    // No logger factory: SDK debug logs can contain task input and must never leak to stdout/stderr.
    await using var server = McpServer.Create(new StdioServerTransport(options), options);
    await server.RunAsync(shutdown.Token);
    return 0;
}
catch (OperationCanceledException) { return 0; }
catch (Exception)
{
    // Deliberately omit paths, SQL, task input and exception messages.
    Console.Error.WriteLine("JSWorkPartner MCP could not start or continue. Check the existing database schema, permissions and lock state.");
    return 1;
}
