# OpenVSP VR branch (Bryan fork)

VR work lives on **`bryVR`**, rebased onto **`bry/win-cmake`** (which is rebased onto
Rob's `origin/main`). No merges — linear history only.

## Branch layout

```
origin/main (Rob)
    → bry/win-cmake (+1 Windows/CMake commit)
        → bryVR (+VR commits)
```

| Branch | Purpose |
|--------|---------|
| `origin/main` | Upstream OpenVSP |
| `bry/win-cmake` | Windows compile layer (see `docs/win-build/WIN_BUILD.md`) |
| `bryVR` | All VR implementation commits (`src/vr/`, OpenXR, `--vr`, etc.) |
| `bryVR-legacy` | Backup of pre-rebase VR branch (local); safe to delete after verify |
| `dev_BryAI` | Non-VR feature work; does not include VR |

## VR touch list (expect conflicts here on rebase)

- `src/CMakeLists.txt` — `VSP_VR` option, `add_subdirectory(vr)`
- `src/vsp/CMakeLists.txt` — link `vsp_vr`
- `src/vsp/main.cpp` — `--vr` dispatch
- `src/cmake/FindOpenXR.cmake`
- `src/vr/**` — entire VR module

Do **not** put Windows/CMake fixes in VR commits (stay on `bry/win-cmake`).

## When Rob releases a new version

**Full step-by-step checklist:** `docs/UPSTREAM_RELEASE_RUNBOOK.md`

**Step 1 — refresh Windows layer:**

```powershell
.\scripts\win-build\rebase-on-upstream.ps1
```

**Step 2 — rebase VR onto updated win-cmake:**

```powershell
.\scripts\vr-build\rebase-on-upstream.ps1
```

Resolve conflicts in the VR touch list above, then:

```powershell
.\scripts\vr-build\export-patches.ps1
git add patches/vr-build/
git commit -m "chore(vr): refresh patch export after upstream rebase"
git push -f skyclmbr bryVR
```

## Local VR build

Prerequisites: libraries built (see `docs/win-build/WIN_BUILD.md`), OpenXR runtime
(Meta Link, SteamVR, etc.).

```powershell
# Configure with VR enabled (separate build dir recommended)
cmake -S . -B VSP_Build_VR -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DVSP_LIBRARY_PATH=F:/AirplaneTools/OpenVSPCode/OpenVSP/VSP_Libraries_BryAI `
  -DVSP_NO_HELP=ON -DVSP_VR=ON

.\scripts\vr-build\build-vr.ps1 F:\AirplaneTools\OpenVSPCode\OpenVSP\VSP_Build_VR

# Run desktop (unchanged)
.\VSP_Build_VR\src\vsp\vsp.exe

# Run VR mode
.\VSP_Build_VR\src\vsp\vsp.exe --vr
```

Or use `scripts\build-vsp.cmd` with a VR-configured build tree.

## Fresh tree: apply VR without the branch

After `bry/win-cmake` is in place (branch checkout or win patches applied):

```powershell
.\scripts\vr-build\apply-patches.ps1
```

## Commit hygiene

- Keep VR commits focused (`feat(vr):`, `fix(vr):`, etc.).
- Squash fixup commits locally before push if you prefer a shorter series.
- Phase/milestone commits are fine to keep for bisect history.

## Related docs

- Windows/CMake layer: `docs/win-build/WIN_BUILD.md`
- VR spec (design): `VR_integration/OpenVSP_VR_Spec_v2.md` (parent repo, if present)
