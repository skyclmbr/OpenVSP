# Windows / CMake 4.x build layer (Bryan fork)

OpenVSP upstream (`openvsp/openvsp` `main`) does not always build on current Windows
toolchains (CMake 4.x, VS 2022+) without small changes to **external library** CMake
scripts under `Libraries/cmake/`.

This fork keeps those changes in a **single commit** on branch `bry/win-cmake`, replayed
on top of each upstream release via **rebase** (not merge).

## Branch layout

```
origin/main (Rob)  →  bry/win-cmake (+1 win-cmake commit)  →  dev_BryAI (non-VR work)
                                                      └──→  bryVR (VR commits)
```

| Branch | Purpose |
|--------|---------|
| `origin/main` | Upstream OpenVSP releases (read-only) |
| `bry/win-cmake` | Exactly one commit: Windows/CMake external-lib fixes |
| `dev_BryAI` | Non-VR feature work; rebase onto `bry/win-cmake` |
| `bryVR` | VR work; rebase onto `bry/win-cmake` (see `docs/vr-build/VR_BUILD.md`) |

## Files touched by the win-cmake commit

- `Libraries/cmake/External_CodeEli.cmake` — `CMAKE_POLICY_VERSION_MINIMUM=3.5`
- `Libraries/cmake/External_GLEW.cmake` — same
- `Libraries/cmake/External_LibXml2.cmake` — MSVC path escaping; manual lib install
- `Libraries/cmake/External_STEPCode.cmake` — policy + build type; `PATCH_COMMAND`
- `Libraries/cmake/PatchSTEPCodeForCMake4.cmake` — STEPCODE CMake 4.x source patch
- `scripts/build-vsp.cmd` — build `vsp` with `vcvars64` from any shell

Do **not** mix these edits into feature commits.

## When Rob releases a new version

From repo root (`OpenVSP/`):

```powershell
.\scripts\win-build\rebase-on-upstream.ps1
```

That fetches upstream, rebases `bry/win-cmake` onto `origin/main`, and reminds you to
rebase `dev_BryAI`. Resolve conflicts only in the files listed above.

Then refresh the mailbox patch baked into the same commit:

```powershell
.\scripts\win-build\export-patches.ps1
git add patches/win-cmake/
git commit --amend --no-edit
```

## Fresh clone: apply without the branch

```powershell
git fetch origin
git reset --hard origin/main
.\scripts\win-build\apply-patches.ps1
```

Or: `git am patches/win-cmake/*.patch`

## Build (after patches applied)

```powershell
# Libraries (once per clean tree or after cmake script changes)
cmake -S Libraries -B VSP_Libraries_BryAI -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build VSP_Libraries_BryAI -j 8

# OpenVSP
cmake -S . -B VSP_Build_BryAI -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DVSP_LIBRARY_PATH=F:/AirplaneTools/OpenVSPCode/OpenVSP/VSP_Libraries_BryAI -DVSP_NO_HELP=ON
.\scripts\build-vsp.cmd F:\AirplaneTools\OpenVSPCode\OpenVSP\VSP_Build_BryAI
```

Adjust paths as needed. `build-vsp.cmd` runs `vcvars64` automatically.

## Upstream overlap

If Rob absorbs a fix upstream, **drop the duplicate hunk** from the win-cmake commit
during rebase rather than fighting to keep it.
