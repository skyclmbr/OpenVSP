# Rebase bryVR onto latest bry/win-cmake (after Rob release + win-cmake rebase).
# Usage: .\scripts\vr-build\rebase-on-upstream.ps1
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..\..")

Push-Location $RepoRoot
try {
    if (-not (git show-ref --verify --quiet refs/heads/bry/win-cmake)) {
        Write-Error "Branch bry/win-cmake not found. Create it first (see docs/win-build/WIN_BUILD.md)."
    }

    $winAhead = git rev-list --count origin/main..bry/win-cmake 2>$null
    if ([int]$winAhead -lt 1) {
        Write-Warning "bry/win-cmake may not include win patches. Run .\scripts\win-build\rebase-on-upstream.ps1 first."
    }

    Write-Host "Checking out bryVR ..."
    git checkout bryVR

    Write-Host "Rebasing bryVR onto bry/win-cmake ($(git log -1 --oneline bry/win-cmake)) ..."
    git rebase bry/win-cmake
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "Rebase stopped. Typical VR conflict files:"
        Write-Host "  src/CMakeLists.txt, src/vsp/CMakeLists.txt, src/vsp/main.cpp"
        Write-Host "  src/cmake/FindOpenXR.cmake, src/vr/**"
        Write-Host ""
        Write-Host "After fixing: git add -A; git rebase --continue"
        Write-Host "Or abort: git rebase --abort"
        exit 1
    }

    Write-Host ""
    Write-Host "bryVR rebased: $(git log -1 --oneline)"
    Write-Host "VR commits on stack: $(git rev-list --count bry/win-cmake..HEAD)"
    Write-Host ""
    Write-Host "Next steps:"
    Write-Host "  1. Build: .\scripts\vr-build\build-vr.ps1 (or configure VSP_Build_VR manually)"
    Write-Host "  2. .\scripts\vr-build\export-patches.ps1"
    Write-Host "  3. git add patches/vr-build/; git commit -m 'chore(vr): refresh patch export'"
    Write-Host "  4. git push -f skyclmbr bryVR"
}
finally {
    Pop-Location
}
