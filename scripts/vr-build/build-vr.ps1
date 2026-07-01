# Build vsp from a VR-enabled CMake build directory.
param(
    [string]$BuildDir = "",
    [string]$Config = "RelWithDebInfo"
)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..\..")

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot "VSP_Build_VR"
}

$cache = Join-Path $BuildDir "CMakeCache.txt"
if (-not (Test-Path $cache)) {
    Write-Error "Build dir not configured: $BuildDir`nRun cmake with -DVSP_VR=ON first (see docs/vr-build/VR_BUILD.md)."
}

$cacheText = Get-Content $cache -Raw
if ($cacheText -notmatch "VSP_VR:BOOL=ON") {
    Write-Warning "VSP_VR does not appear ON in $BuildDir. Reconfigure with -DVSP_VR=ON"
}

& (Join-Path $RepoRoot "scripts\build-vsp.cmd") $BuildDir $Config vsp
exit $LASTEXITCODE
