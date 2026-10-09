# Compilation sous Windows

Ce fichier n'est plus un guide à part : la **seule source** pour installer ou
compiler OpenStitch Studio sous Windows est le chapitre
[`docs/source/installation.md`](source/installation.md) (« Installer sans compiler »
en tête, puis la voie rapide `scripts\build.ps1` et la voie manuelle).

Rappel express (détails dans ce chapitre) :

```powershell
cmake --preset msvc
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

Le dépannage de compilation est dans
[`docs/source/troubleshooting.md`](source/troubleshooting.md) (partie « Compilation »).
