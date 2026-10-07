[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Database,
    [string]$PublishExe = (Join-Path $PSScriptRoot '..\JSWorkPartner.Mcp\bin\Release\net8.0\win-x64\publish\JSWorkPartner.Mcp.exe'),
    [switch]$Apply,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
if ($Apply -and $DryRun) { throw 'Choose -Apply or -DryRun, not both.' }
# Windows PowerShell 5.1 has no Path.IsPathFullyQualified(). Require a drive/UNC absolute file path.
if ($Database -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+\\)') {
    throw '-Database must be an absolute existing file path. No default database is guessed or created.'
}
if (-not (Test-Path -LiteralPath $Database -PathType Leaf)) { throw 'Database file does not exist.' }
if (-not (Test-Path -LiteralPath $PublishExe -PathType Leaf)) { throw 'Published MCP executable does not exist. Publish first.' }
$databasePath = (Resolve-Path -LiteralPath $Database).ProviderPath
$exePath = (Resolve-Path -LiteralPath $PublishExe).ProviderPath
$codex = Get-Command codex -ErrorAction Stop
# List only reads configuration; it never starts the registered MCP servers.
$raw = & $codex mcp list --json 2>$null
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect existing MCP registrations.' }
$registrations = @((($raw -join "`n") | ConvertFrom-Json))
if ($registrations | Where-Object { $_.name -eq 'jsworkpartner' }) {
    throw 'An MCP named jsworkpartner already exists. Inspect/remove it explicitly before registering; this script never overwrites it.'
}
$arguments = @('mcp', 'add', 'jsworkpartner', '--', $exePath, '--database', $databasePath)
$display = 'codex ' + (($arguments | ForEach-Object { "'" + $_.Replace("'", "''") + "'" }) -join ' ')
[pscustomobject]@{
    DryRun = -not $Apply
    Executable = $exePath
    Database = $databasePath
    Command = $display
    Arguments = $arguments
}
if ($Apply) {
    & $codex @arguments
    if ($LASTEXITCODE -ne 0) { throw 'codex mcp add failed.' }
}
