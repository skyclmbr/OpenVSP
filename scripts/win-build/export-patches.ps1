# Export bry/win-cmake commit(s) as mailbox patches for offline apply.
# Usage: .\scripts\win-build\export-patches.ps1
# Run from branch bry/win-cmake after rebasing onto origin/main.
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$PatchDir = Join-Path $RepoRoot "patches\win-cmake"

Push-Location $RepoRoot
try {
    $upstream = git merge-base HEAD origin/main
    if (-not $upstream) {
        Write-Error "Could not find merge-base with origin/main. Run: git fetch origin"
    }

    $ahead = git rev-list --count "${upstream}..HEAD"
    if ([int]$ahead -eq 0) {
        Write-Error "HEAD has no commits ahead of origin/main. Nothing to export."
    }

    if (-not (Test-Path $PatchDir)) {
        New-Item -ItemType Directory -Path $PatchDir | Out-Null
    }

    Get-ChildItem -Path $PatchDir -Filter "*.patch" -ErrorAction SilentlyContinue | Remove-Item -Force

    git format-patch origin/main -o $PatchDir --no-stat
    if ($LASTEXITCODE -ne 0) { exit 1 }

    Write-Host "Exported $ahead patch(es) to $PatchDir"
    Get-ChildItem $PatchDir -Filter "*.patch" | ForEach-Object { Write-Host "  $($_.Name)" }
}
finally {
    Pop-Location
}
