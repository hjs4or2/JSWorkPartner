$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$repository = Split-Path $project -Parent
$fixture = Join-Path $PSScriptRoot ('artifacts\registration-' + [guid]::NewGuid().ToString('D'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$database = Join-Path $fixture "space and 'quote.db"
[IO.File]::WriteAllText($database, 'path-only fixture')
$register = Join-Path $repository 'scripts\Register-JSWorkPartnerMcp.ps1'
$global:JSWorkPartnerMcpMockListed = '[]'
$global:JSWorkPartnerMcpMockAdded = @()
function codex {
    $global:LASTEXITCODE = 0
    if ($args[0] -eq 'mcp' -and $args[1] -eq 'list') { $global:JSWorkPartnerMcpMockListed; return }
    if ($args[0] -eq 'mcp' -and $args[1] -eq 'add') { $global:JSWorkPartnerMcpMockAdded = @($args); return }
    throw 'Unexpected mock invocation'
}
$preview = & $register -Database $database
if (-not $preview.DryRun -or $global:JSWorkPartnerMcpMockAdded.Count -ne 0 -or $preview.Database -ne $database) { throw 'Default dry-run failed' }
$explicit = & $register -Database $database -DryRun
if (-not $explicit.DryRun) { throw 'Explicit dry-run failed' }
$applied = & $register -Database $database -Apply
if ($applied.DryRun -or $global:JSWorkPartnerMcpMockAdded.Count -ne 7 -or $global:JSWorkPartnerMcpMockAdded[6] -ne $database) {
    throw ('Argument-array forwarding failed: ' + ($global:JSWorkPartnerMcpMockAdded -join '|'))
}
$global:JSWorkPartnerMcpMockListed = '[{"name":"jsworkpartner"}]'
$refused = $false
try { & $register -Database $database -Apply | Out-Null } catch { $refused = $true }
if (-not $refused) { throw 'Existing registration was not refused' }
$refused = $false
try { & $register -Database 'relative.db' | Out-Null } catch { $refused = $true }
if (-not $refused) { throw 'Relative database path was not refused' }
$refused = $false
try { & $register -Database (Join-Path $fixture 'absent.db') | Out-Null } catch { $refused = $true }
if (-not $refused) { throw 'Missing database was not refused' }
Write-Output 'Registration script checks passed (mock CLI only; no actual config write).'
Remove-Variable JSWorkPartnerMcpMockListed,JSWorkPartnerMcpMockAdded -Scope Global
