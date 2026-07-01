# Upstream release runbook (Rob / openvsp)

**Use this document** when Rob ships a new OpenVSP release on `openvsp/openvsp` `main`.
Follow steps **in order**. Use **rebase, not merge** (Rob's preference).

> **Agent note:** If the user asks to update for a new upstream release, read this file
> and execute every step below from repo root `OpenVSP/`. Do not skip verification or
> patch export. Force-push rewritten branches to `skyclmbr` only after a successful build.

## Repository layout

| Item | Value |
|------|-------|
| Repo root | `F:\AirplaneTools\OpenVSPCode\OpenVSP` |
| Upstream remote | `origin` → `https://github.com/openvsp/openvsp` |
| Fork remote | `skyclmbr` → `https://github.com/skyclmbr/OpenVSP.git` |
| Upstream branch | `origin/main` |
| Libraries build dir | `VSP_Libraries_BryAI` |
| Desktop build dir | `VSP_Build_BryAI` |
| VR build dir | `VSP_Build_VR` |

## Branch stack (bottom → top)

```
origin/main (Rob)
    → bry/win-cmake   (+1 squashed Windows/CMake commit)
        → dev_BryAI   (non-VR feature work)
        → bryVR       (VR commits; sibling of dev_BryAI, not on top of it)
```

| Branch | Commits ahead of parent | Patch export base |
|--------|-------------------------|-------------------|
| `bry/win-cmake` | 1 (win fixes) | `origin/main` → `patches/win-cmake/` |
| `dev_BryAI` | feature commits | (no standard export; rebase only) |
| `bryVR` | VR + workflow commits | `bry/win-cmake` → `patches/vr-build/` |

Related detail:

- Windows layer: `docs/win-build/WIN_BUILD.md`
- VR layer: `docs/vr-build/VR_BUILD.md`

---

## Checklist (copy for each release)

### 0. Preflight

```powershell
cd F:\AirplaneTools\OpenVSPCode\OpenVSP
git fetch origin
git log -1 --oneline origin/main
```

Note the new release tag/commit (e.g. `OpenVSP 3.52.0`). Ensure working tree is clean
(`git status`).

---

### 1. Rebase Windows/CMake layer onto Rob

```powershell
.\scripts\win-build\rebase-on-upstream.ps1
```

**If rebase stops for conflicts**, fix only win-cmake files:

- `Libraries/cmake/External_CodeEli.cmake`
- `Libraries/cmake/External_GLEW.cmake`
- `Libraries/cmake/External_LibXml2.cmake`
- `Libraries/cmake/External_STEPCode.cmake`
- `Libraries/cmake/PatchSTEPCodeForCMake4.cmake`
- `scripts/build-vsp.cmd` (if touched)

If Rob absorbed a fix upstream, **drop the duplicate hunk** instead of keeping both.

```powershell
git add -A
git rebase --continue
# or abort: git rebase --abort
```

**Refresh win mailbox patches** (amend into the single win commit):

```powershell
.\scripts\win-build\export-patches.ps1
git add patches/win-cmake/
git commit --amend --no-edit
```

**Verify desktop build:**

```powershell
cmake -S Libraries -B VSP_Libraries_BryAI -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build VSP_Libraries_BryAI -j 8

cmake -S . -B VSP_Build_BryAI -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DVSP_LIBRARY_PATH=F:/AirplaneTools/OpenVSPCode/OpenVSP/VSP_Libraries_BryAI `
  -DVSP_NO_HELP=ON
.\scripts\build-vsp.cmd F:\AirplaneTools\OpenVSPCode\OpenVSP\VSP_Build_BryAI
```

**Push win layer:**

```powershell
git push -f skyclmbr bry/win-cmake
```

**Optional:** copy win patches to parent backup folder:

```powershell
Copy-Item patches\win-cmake\*.patch F:\AirplaneTools\OpenVSPCode\patches\win-cmake\ -Force
```

---

### 2. Rebase non-VR feature branch (`dev_BryAI`)

```powershell
git checkout dev_BryAI
git rebase bry/win-cmake
```

Resolve feature conflicts, then:

```powershell
git add -A
git rebase --continue
```

Rebuild/spot-check if feature commits touched build-sensitive code.

```powershell
git push -f skyclmbr dev_BryAI
```

---

### 3. Rebase VR branch (`bryVR`)

```powershell
.\scripts\vr-build\rebase-on-upstream.ps1
```

**If rebase stops for conflicts**, fix VR touch list:

- `src/CMakeLists.txt`
- `src/vsp/CMakeLists.txt`
- `src/vsp/main.cpp`
- `src/cmake/FindOpenXR.cmake`
- `src/vr/**`

Do **not** put Windows/CMake fixes in VR commits.

```powershell
git add -A
git rebase --continue
```

**Refresh VR mailbox patches:**

```powershell
.\scripts\vr-build\export-patches.ps1
git add patches/vr-build/
git commit -m "chore(vr): refresh patch export after upstream rebase"
```

**Verify VR build:**

```powershell
cmake -S . -B VSP_Build_VR -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DVSP_LIBRARY_PATH=F:/AirplaneTools/OpenVSPCode/OpenVSP/VSP_Libraries_BryAI `
  -DVSP_NO_HELP=ON -DVSP_VR=ON
.\scripts\vr-build\build-vr.ps1 F:\AirplaneTools\OpenVSPCode\OpenVSP\VSP_Build_VR
.\VSP_Build_VR\src\vsp\vsp.exe --vr
```

**Push VR branch:**

```powershell
git push -f skyclmbr bryVR
```

**Optional:** copy VR patches to parent backup folder:

```powershell
Copy-Item patches\vr-build\*.patch F:\AirplaneTools\OpenVSPCode\patches\vr-build\ -Force
```

---

### 4. Post-release summary

Record for the user:

- New `origin/main` commit
- `bry/win-cmake` tip after rebase
- `dev_BryAI` tip (if rebased)
- `bryVR` tip and VR commit count: `git rev-list --count bry/win-cmake..bryVR`
- Any conflicts resolved and where
- Build results (desktop + VR)

---

## Fresh tree (no branches): apply patches only

Win layer on clean upstream:

```powershell
git fetch origin
git reset --hard origin/main
.\scripts\win-build\apply-patches.ps1
```

Add VR on top of win layer:

```powershell
.\scripts\vr-build\apply-patches.ps1
```

---

## Troubleshooting

| Problem | Action |
|---------|--------|
| `git rebase` conflict | Fix files, `git add`, `git rebase --continue` |
| Win script says branch not found | `git checkout bry/win-cmake` or create from last known good |
| VR export: no commits ahead | Checkout `bryVR`; ensure rebase finished |
| Duplicate upstream fix | Remove hunk from win-cmake commit during rebase |
| Libraries fail after upstream bump | Rebuild `VSP_Libraries_BryAI` from scratch |
| Need old VR history | Local backup branch `bryVR-legacy` (if still present) |

---

## One-liner order (after preflight)

```powershell
.\scripts\win-build\rebase-on-upstream.ps1
.\scripts\win-build\export-patches.ps1; git add patches/win-cmake/; git commit --amend --no-edit
# verify desktop build
git push -f skyclmbr bry/win-cmake

git checkout dev_BryAI; git rebase bry/win-cmake
# verify if needed
git push -f skyclmbr dev_BryAI

.\scripts\vr-build\rebase-on-upstream.ps1
.\scripts\vr-build\export-patches.ps1; git add patches/vr-build/; git commit -m "chore(vr): refresh patch export after upstream rebase"
# verify VR build
git push -f skyclmbr bryVR
```
