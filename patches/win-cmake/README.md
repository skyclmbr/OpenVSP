# Windows build patches

Mailbox patches exported from branch `bry/win-cmake` (one commit ahead of `origin/main`).

## Regenerate

```powershell
git checkout bry/win-cmake
.\scripts\win-build\rebase-on-upstream.ps1   # if upstream moved
.\scripts\win-build\export-patches.ps1
git add patches/win-cmake/
git commit -m "chore(win): refresh patch export"
```

## Apply on a clean upstream tree

```powershell
git reset --hard origin/main
.\scripts\win-build\apply-patches.ps1
```
