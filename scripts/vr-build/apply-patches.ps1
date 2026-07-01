# Apply VR mailbox patches on top of bry/win-cmake (current HEAD must include win layer).
# Usage: .\scripts\vr-build\apply-patches.ps1
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$PatchDir = Join-Path $RepoRoot "patches\vr-build"

if (-not (Test-Path $PatchDir)) {
    Write-Error "Patch directory not found: $PatchDir`nRun export-patches.ps1 on branch bryVR first."
}

$Patches = Get-ChildItem -Path $PatchDir -Filter "*.patch" | Sort-Object Name
if ($Patches.Count -eq 0) {
    Write-Error "No .patch files in $PatchDir"
}

Push-Location $RepoRoot
try {
    foreach ($p in $Patches) {
        Write-Host "Applying $($p.Name) ..."
        git am $p.FullName
        if ($LASTEXITCODE -ne 0) {
            Write-Host ""
            Write-Host "git am failed. Fix conflicts, then: git add -A; git am --continue"
            Write-Host "Or abort: git am --abort"
            exit 1
        }
    }
    Write-Host "VR patches applied. HEAD: $(git log -1 --oneline)"
}
finally {
    Pop-Location
}
