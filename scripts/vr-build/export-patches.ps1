# Export bryVR commits (ahead of bry/win-cmake) as mailbox patches.
# Usage: .\scripts\vr-build\export-patches.ps1
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$PatchDir = Join-Path $RepoRoot "patches\vr-build"

Push-Location $RepoRoot
try {
    if (-not (git show-ref --verify --quiet refs/heads/bry/win-cmake)) {
        Write-Error "Branch bry/win-cmake not found."
    }

    $ahead = git rev-list --count bry/win-cmake..HEAD
    if ([int]$ahead -eq 0) {
        Write-Error "No commits on current branch ahead of bry/win-cmake."
    }

    if (-not (Test-Path $PatchDir)) {
        New-Item -ItemType Directory -Path $PatchDir | Out-Null
    }

    Get-ChildItem -Path $PatchDir -Filter "*.patch" -ErrorAction SilentlyContinue | Remove-Item -Force

    git format-patch bry/win-cmake -o $PatchDir --no-stat
    if ($LASTEXITCODE -ne 0) { exit 1 }

    Write-Host "Exported $ahead VR patch(es) to $PatchDir"
    Get-ChildItem $PatchDir -Filter "*.patch" | ForEach-Object { Write-Host "  $($_.Name)" }
}
finally {
    Pop-Location
}
