# Rebase bry/win-cmake onto latest upstream OpenVSP main.
# Usage: .\scripts\win-build\rebase-on-upstream.ps1
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..\..")

Push-Location $RepoRoot
try {
    Write-Host "Fetching origin (openvsp/openvsp) ..."
    git fetch origin
    if ($LASTEXITCODE -ne 0) { exit 1 }

    $branch = git branch --show-current
    if ($branch -ne "bry/win-cmake") {
        Write-Host "Checking out bry/win-cmake ..."
        git checkout bry/win-cmake
    }

    Write-Host "Rebasing onto origin/main ($(git log -1 --oneline origin/main)) ..."
    git rebase origin/main
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "Rebase stopped. Typical conflict files:"
        Write-Host "  Libraries/cmake/External_*.cmake"
        Write-Host "  Libraries/cmake/PatchSTEPCodeForCMake4.cmake"
        Write-Host ""
        Write-Host "After fixing: git add -A; git rebase --continue"
        Write-Host "Or abort: git rebase --abort"
        exit 1
    }

    Write-Host ""
    Write-Host "bry/win-cmake rebased: $(git log -1 --oneline)"
    Write-Host ""
    Write-Host "Next steps:"
    Write-Host "  1. Rebuild libraries + vsp to verify"
    Write-Host "  2. .\scripts\win-build\export-patches.ps1"
    Write-Host "  3. git add patches/win-cmake/; git commit --amend --no-edit"
    Write-Host "  4. git push -f skyclmbr bry/win-cmake"
    Write-Host "  5. git checkout dev_BryAI; git rebase bry/win-cmake"
    Write-Host "  6. git push -f skyclmbr dev_BryAI"
}
finally {
    Pop-Location
}
