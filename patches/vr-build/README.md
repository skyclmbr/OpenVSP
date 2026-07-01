# VR patches (series)

Mailbox patches for all commits on **`bryVR`** that are ahead of **`bry/win-cmake`**.

Requires the Windows layer first (`bry/win-cmake` branch or `patches/win-cmake/`).

## Regenerate

```powershell
git checkout bryVR
.\scripts\win-build\rebase-on-upstream.ps1    # if Rob released
.\scripts\vr-build\rebase-on-upstream.ps1
.\scripts\vr-build\export-patches.ps1
git add patches/vr-build/
git commit -m "chore(vr): refresh patch export"
```

## Apply (on top of win-cmake)

```powershell
git checkout bry/win-cmake
.\scripts\vr-build\apply-patches.ps1
```

Or checkout branch `bryVR` directly.
