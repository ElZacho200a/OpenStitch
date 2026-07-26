# Installation et premier démarrage

Public : utilisateur débutant, développeur.

À ce jour, **aucun binaire pré-compilé n'est distribué** : l'installation se fait
par **compilation depuis les sources**. Cette section décrit la mise en place
minimale ; la compilation détaillée est traitée dans *Compilation et
développement*.

## Systèmes supportés

| Système | Statut |
|---|---|
| Windows 10 / 11 (x64) | Cible principale (application complète) |
| Linux | Cœur + CLI uniquement (garde-fou de portabilité, vérifié en CI) — **pas** l'interface Qt à ce stade |
| macOS | Prévu, non vérifié |

## Prérequis (Windows)

| Outil | Version | Rôle |
|---|---|---|
| Visual Studio 2022 ou plus récent (charge « Développement Desktop C++ ») | MSVC v143+ | Compilateur |
| CMake | ≥ 3.27 | Configuration du build |
| Git | récent | Récupération du code et de vcpkg |
| vcpkg | bootstrappé | Dépendances (OpenCV, Clipper2, …) |
| Qt | 6.8 LTS, binaires `msvc2022_64` | Interface graphique |
| Espace disque | ~10 Go | vcpkg compile OpenCV au premier configure |

## Mise en place

1. Installer vcpkg et définir `VCPKG_ROOT` :

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat -disableMetrics
setx VCPKG_ROOT C:\dev\vcpkg
```

2. Installer Qt 6.8 (sans compte, via `aqtinstall`) et définir `QT_ROOT` :

```powershell
python -m pip install --user aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt -m qtimageformats
setx QT_ROOT C:\Qt\6.8.3\msvc2022_64
```

3. Configurer, compiler, tester (nouveau terminal pour prendre les variables) :

```powershell
cmake --preset msvc
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

Les exécutables produits :

- `build\msvc\apps\desktop\Debug\openstitch.exe` — application graphique ;
- `build\msvc\apps\cli\Debug\openstitch-cli.exe` — outil en ligne de commande.

## Démarrage

Lancez `openstitch.exe`. La fenêtre principale s'ouvre sur un **canevas** vide,
avec des règles graduées en millimètres et un **cadre** de broderie de
100 × 100 mm par défaut. Utilisez **Fichier → Ouvrir une image…** pour commencer.

## Emplacement des données, configuration, désinstallation

- **Données** : le logiciel ne stocke rien en dehors des fichiers projet `.osp`
  et des fichiers exportés que vous choisissez. *Information non déterminée dans
  le dépôt* : aucun répertoire de configuration utilisateur n'est créé (pas de
  persistance de préférences repérée dans le code).
- **Configuration** : les variables d'environnement `VCPKG_ROOT` et `QT_ROOT`
  concernent la **compilation**, pas l'exécution.
- **Désinstallation** : supprimez le dossier de build et, le cas échéant, vcpkg
  et Qt. Aucune entrée de registre n'est écrite par l'application.

## Vérification de l'installation

- `ctest --preset msvc-debug` doit rapporter **100 % des tests réussis**.
- `openstitch-cli.exe --version` affiche la version.
- `openstitch-cli.exe info une-image.png` affiche les métadonnées d'une image.

Avertissement : au **premier** `cmake --preset msvc`, vcpkg compile OpenCV et
d'autres dépendances, ce qui peut prendre 10 à 30 minutes. Les compilations
suivantes réutilisent le cache binaire de vcpkg.

## Implémentation associée

- `docs/build-windows.md` — guide de compilation d'origine.
- `CMakePresets.json` — presets `msvc` et `linux-core`.
- `vcpkg.json` — dépendances verrouillées par baseline.
- `apps/cli/main.cpp` — sous-commande `info`, drapeau `--version`.
