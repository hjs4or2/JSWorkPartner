param(
    [Parameter(Mandatory = $true)]
    [string]$RepositoryRoot,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$baseVersion = "1.0.0"
$baseCommit = "3beca48"
$revision = 0

$gitDirectory = $null
$localGitDirectory = Join-Path $RepositoryRoot ".git-local"
$standardGitDirectory = Join-Path $RepositoryRoot ".git"
if (Test-Path (Join-Path $localGitDirectory "HEAD")) {
    $gitDirectory = $localGitDirectory
}
elseif (Test-Path (Join-Path $standardGitDirectory "HEAD")) {
    $gitDirectory = $standardGitDirectory
}

if ($null -ne $gitDirectory) {
    $revisionTarget = "origin/main"
    & git --git-dir=$gitDirectory --work-tree=$RepositoryRoot rev-parse --verify --quiet $revisionTarget *> $null
    if ($LASTEXITCODE -ne 0) {
        $revisionTarget = "HEAD"
    }

    $count = & git --git-dir=$gitDirectory --work-tree=$RepositoryRoot rev-list --count "$baseCommit..$revisionTarget" 2>$null
    if ($LASTEXITCODE -eq 0 -and $count -match "^\d+$") {
        $revision = [int]$count
    }
}

$version = "$baseVersion.$revision"
$content = @"
#pragma once

// Generated from the number of commits pushed after Release $baseVersion.
#define JSWORKPARTNER_VERSION "$version"
"@

$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$existingContent = if (Test-Path $OutputPath) {
    Get-Content -Raw -Path $OutputPath
} else {
    ""
}

if ($existingContent -ne $content) {
    Set-Content -Path $OutputPath -Value $content -Encoding UTF8
}

Write-Output "JSWorkPartner version: $version"
