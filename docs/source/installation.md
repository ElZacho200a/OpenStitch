# Installation et premier démarrage

Public : utilisateur débutant (sections 1 à 4), développeur (section 5).

OpenStitch Studio se **télécharge sans rien compiler** depuis la page *Releases*
du dépôt GitHub (<https://github.com/ElZacho200a/OpenStitch/releases>). La
compilation depuis les sources ne concerne que les développeurs
(section « 5. Compiler depuis les sources » plus bas et [Compilation et développement](build-system.md)).

## Systèmes supportés

| Système | Statut |
|---|---|
| Windows 10 / 11 (x64) | Cible principale (application complète) |
| Linux | Cœur + CLI uniquement (garde-fou de portabilité, vérifié en CI) — **pas** l'interface Qt à ce stade |
| macOS | Prévu, non vérifié |

## 1. Installer sans compiler

Sur la page *Releases* du dépôt, trois sortes de téléchargements existent :

| Pièce jointe | Quand | Pour qui |
|---|---|---|
| `OpenStitchStudio-Setup-X.Y.Z.exe` (installeur) | publiée sur chaque version `vX.Y.Z` | **recommandé** : installation par utilisateur (sans droits administrateur), raccourci Bureau en option, désinstallation dans « Applications installées » |
| `openstitch-windows-x64.zip` | release « Dernier build (main) », remplacée à chaque validation de `main` | essayer la version la plus récente sans installer |
| ZIP `openstitch-beta-windows-x64` | onglet *Actions* → exécution concernée → *Artifacts* (30 jours) | builds bêta, **pas forcément validés par les tests** |

**Avec l'installeur** : lancez-le, choisissez la langue et le dossier, puis
démarrez *OpenStitch Studio* depuis le menu Démarrer.

**Avec un ZIP** : **décompressez tout le ZIP** (clic droit → *Extraire tout* ; ne
lancez pas l'exécutable depuis l'aperçu de l'archive), puis lancez
`desktop\Release\openstitch.exe`. Le dossier contient les DLL et les plugins Qt
dont l'application a besoin : **ne déplacez pas `openstitch.exe` seul**. L'outil
en ligne de commande `openstitch-cli.exe` est dans `cli\Release\` (voir
[Ligne de commande](cli.md)).

### Windows SmartScreen et « éditeur inconnu »

Les binaires ne sont **pas signés** (aucun certificat d'éditeur). Au premier
lancement, Windows SmartScreen peut afficher « Windows a protégé votre PC » ou
« Éditeur inconnu » : cliquez sur **Informations complémentaires**, puis
**Exécuter quand même**. Si vous préférez ne pas faire confiance au binaire,
compilez depuis les sources (section 5) : le code est entièrement public. Un
antivirus peut aussi mettre en quarantaine un ZIP téléchargé ; rien n'est
envoyé sur Internet par l'application (100 % local, sans compte ni télémétrie).

### Si une DLL est introuvable

« Le programme ne peut pas démarrer car `Qt6Core.dll` (ou `opencv_*.dll`,
`VCRUNTIME140.dll`…) est introuvable » signifie en général que l'exécutable a été
copié **sans ses voisins** ou lancé depuis l'archive non extraite. Extrayez le
ZIP en entier, ou réinstallez avec l'installeur. Pour `VCRUNTIME140*.dll` /
`MSVCP140.dll`, installez le *Microsoft Visual C++ Redistributable 2015-2022
(x64)*.

## 2. Premier démarrage

Lancez `openstitch.exe`. La fenêtre principale s'ouvre sur un **canevas** vide,
avec des règles graduées en millimètres et un **cadre** de broderie de
100 × 100 mm par défaut. L'écran d'accueil propose **Ouvrir une image**, **Ouvrir
un projet** et **Importer un DST**. Continuez avec le
[Guide de prise en main](getting-started.md).

## 3. Où l'application range ses données

| Quoi | Où (Windows) | Contenu |
|---|---|---|
| Vos fichiers | là où vous les enregistrez | projets `.osp`, DST exportés : **jamais** ailleurs sans votre accord |
| Préférences (QSettings) | registre, clé `HKEY_CURRENT_USER\Software\OpenStitch\OpenStitch Studio` | disposition des panneaux, géométrie de fenêtre, thème, densité, navigation, projets récents, préférences IA, accrochage des nœuds |
| Sauvegarde automatique | `%APPDATA%\OpenStitch\OpenStitch Studio\autosave\` (sous `%USERPROFILE%\AppData\Roaming`) | un instantané `<empreinte>.osp` + un petit fichier `.json` (chemin d'origine, horodatage) par document modifié |
| Journaux | sortie d'erreur de la console uniquement | **aucun fichier de log** n'est écrit : lancez `openstitch.exe` depuis un terminal pour voir les messages |

**Sauvegarde automatique** : toutes les 2 minutes, si le document est modifié et
non vide. Elle est supprimée à la fermeture normale de l'application ;
après un arrêt anormal, un dialogue **Récupération après un arrêt anormal**
propose de **Récupérer** ou d'**Ignorer** au démarrage suivant.

**Réinitialiser les préférences** : fermez l'application et supprimez la clé de
registre ci-dessus (`reg delete "HKCU\Software\OpenStitch" /f`). **Remettre la
disposition** : menu Affichage ▸ Panneaux ▸ Réinitialiser la disposition.

## 4. Désinstaller

- **Installeur** : *Paramètres → Applications → Applications installées* →
  OpenStitch Studio → Désinstaller.
- **ZIP** : supprimez le dossier extrait.
- Dans les deux cas, supprimez si vous le souhaitez la clé de registre
  `HKCU\Software\OpenStitch` et le dossier `%APPDATA%\OpenStitch` (préférences et
  sauvegardes automatiques). Vos projets `.osp` ne sont jamais supprimés.

## 5. Compiler depuis les sources (développeurs)

À réserver aux contributeurs, ou pour ne pas utiliser de binaire non signé. Deux
voies :

**Voie rapide : `scripts\build.ps1`.** Elle **installe tout ce qui manque** : le
script liste d'abord les outils absents (CMake, Visual Studio Build Tools et son
workload C++ — environ 5 Go —, vcpkg, Qt 6.8.3 via `aqtinstall`), **demande
confirmation**, puis les installe et écrit les variables utilisateur
`VCPKG_ROOT` et `QT_ROOT` de façon permanente avant de compiler Debug et Release.

```powershell
.\scripts\build.ps1 -Test            # confirmer l'installation, tout compiler, lancer les tests
.\scripts\build.ps1 -Yes             # confirme d'avance (aucune invite)
.\scripts\build.ps1 -SkipBootstrap   # poste déjà configuré : n'installe rien
```

**Voie manuelle** (équivalent détaillé de ce que fait le script) :

| Outil | Version | Rôle |
|---|---|---|
| Visual Studio 2022 ou plus récent (charge « Développement Desktop C++ ») | MSVC v143+ | Compilateur |
| CMake | ≥ 3.27 | Configuration du build |
| Git | récent | Récupération du code et de vcpkg |
| vcpkg | bootstrappé | Dépendances (OpenCV, Clipper2, …) |
| Qt | 6.8 LTS, binaires `msvc2022_64` | Interface graphique |
| Espace disque | ~10 Go | vcpkg compile OpenCV au premier configure |

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

Avertissement : au **premier** `cmake --preset msvc`, vcpkg compile OpenCV et
d'autres dépendances, ce qui peut prendre 10 à 30 minutes. Les compilations
suivantes réutilisent le cache binaire de vcpkg. Les problèmes de compilation
sont traités dans [Dépannage](troubleshooting.md) ; l'organisation du build dans
[Compilation et développement](build-system.md).

**Vérification** : `ctest --preset msvc-debug` doit rapporter **100 % de tests
réussis** ; `openstitch-cli.exe --version` affiche la version ;
`openstitch-cli.exe info une-image.png` affiche les métadonnées d'une image.

## Implémentation associée

- `.github/workflows/ci.yml`, `.github/workflows/release.yml` — builds bêta,
  release « latest » et installeur.
- `packaging/windows/installer.iss` — installeur Inno Setup (par utilisateur).
- `apps/desktop/main.cpp` — nom d'organisation et d'application (clé QSettings).
- `apps/desktop/autosave.cpp`, `apps/desktop/recent_files.cpp` — sauvegarde
  automatique et projets récents.
- `scripts/build.ps1` — voie rapide ; `CMakePresets.json` — presets `msvc` et
  `linux-core` ; `vcpkg.json` — dépendances verrouillées par baseline.
