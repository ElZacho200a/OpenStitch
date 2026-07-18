# Compilation sous Windows

## Prérequis

| Outil | Version | Notes |
|---|---|---|
| Visual Studio | 2022 ou plus récent, charge de travail « Développement Desktop en C++ » | MSVC est le compilateur principal |
| CMake | ≥ 3.27 | inclus dans VS ou séparé |
| Git | récent | |
| vcpkg | cloné et bootstrappé | voir ci-dessous |
| Qt | 6.8 LTS, binaires officiels `msvc2022_64` | LGPL — voir THIRD_PARTY_LICENSES.md |
| Espace disque | ~10 Go | vcpkg compile OpenCV au premier configure (10–30 min) |

## 1. Installer vcpkg

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat -disableMetrics
setx VCPKG_ROOT C:\dev\vcpkg
```

## 2. Installer Qt 6.8 (binaires officiels, sans compte)

Avec Python installé :

```powershell
python -m pip install --user aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt -m qtimageformats
setx QT_ROOT C:\Qt\6.8.3\msvc2022_64
```

(L'installeur officiel Qt fonctionne aussi ; seul compte le chemin `...\msvc2022_64` dans `QT_ROOT`.)

## 3. Configurer, compiler, tester

Ouvrir un **nouveau** terminal (pour que `VCPKG_ROOT`/`QT_ROOT` soient pris en compte) :

```powershell
cmake --preset msvc            # premier lancement long : vcpkg compile les dépendances
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

Exécutables produits :

- `build\msvc\apps\desktop\Debug\openstitch.exe` — application graphique
- `build\msvc\apps\cli\Debug\openstitch-cli.exe` — ex. : `openstitch-cli info image.png`

Le build Release : `cmake --build --preset msvc-release`.

## Dépannage

- **« Qt6 introuvable »** : vérifier `QT_ROOT` (doit pointer sur le dossier contenant `bin/`, `lib/`, `include/`), puis reconfigurer.
- **Erreur toolchain vcpkg** : vérifier `VCPKG_ROOT` et que `bootstrap-vcpkg.bat` a été exécuté.
- **Compilation OpenCV très longue** : normal au premier configure uniquement ; les binaires sont mis en cache par vcpkg.
