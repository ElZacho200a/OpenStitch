# OpenStitch Studio

> Nom temporaire — voir ADR-001.

Logiciel de bureau **libre et gratuit** de numérisation pour broderie machine : de l'image matricielle au fichier de broderie (DST en premier), avec contrôle manuel à chaque étape.

- **Langage** : C++ moderne (C++23, minimum requis C++20 pour les contributeurs)
- **Plateforme prioritaire** : Windows 10/11 (cœur portable, build Linux vérifié en CI)
- **Licence** : [Apache-2.0](LICENSE) — voir [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)
- **Interface** : Qt 6 Widgets (LGPL, liaison dynamique) — le cœur ne dépend jamais de Qt
- **Fonctionnement** : 100 % local, sans compte, sans cloud, sans télémétrie

## Installer sans compiler

Les binaires Windows sont sur la page [Releases](https://github.com/ElZacho200a/OpenStitch/releases) :
installeur `OpenStitchStudio-Setup-X.Y.Z.exe` pour chaque version `vX.Y.Z`, et release « Dernier build (main) »
(ZIP). Windows SmartScreen peut avertir (binaires non signés) : *Informations complémentaires → Exécuter quand même*.
Détails, emplacement des préférences et de la sauvegarde automatique, désinstallation :
[docs/source/installation.md](docs/source/installation.md).

**Builds bêta** : dès que la compilation Windows réussit, les workflows CI et Release déposent un ZIP
`openstitch-beta-windows-x64` dans **Actions → exécution concernée → Artifacts** (conservé 30 jours, même si les
tests échouent ensuite). Décompressez **tout** le ZIP et lancez `desktop/Release/openstitch.exe` (les DLL et plugins
voisins sont nécessaires). Ces builds ne sont pas forcément validés par les tests.

## État du projet

**Chaîne complète fonctionnelle, de l'image au fichier DST**, avec contrôle manuel à chaque étape :

1. import PNG/JPEG/BMP/TIFF/SVG, taille physique en millimètres ;
2. prétraitement non destructif (recadrage, symétries, rotations, luminosité/contraste, débruitage, quantification) ;
3. segmentation perceptuelle (CIELAB) en régions éditables (fusion, suppression, recoloration), segmentation par IA facultative ;
4. vectorisation (contours, trous, simplification, édition de nœuds) et outils de dessin (rectangle, ellipse, polygone, Bézier, main levée) ;
5. objets de broderie : **point droit/triple, remplissage tatami (dont directionnel), colonne satin** ;
6. **numérisation automatique** (image → objets éditables), en *formes pleines* ou en mode **Contours / Line Art** (dessins au trait) ;
7. **auto-satin par squelette** (« Créer un satin automatique ») — **expérimental, non validé sur machine** : il refuse
   les formes qui ne s'y prêtent pas et propose un tatami ;
8. **édition par groupes** : sélection multiple d'objets et de régions (Maj/Ctrl, rectangle), fusion, recoloration, déplacement, duplication et suppression d'un bloc ; **opérations sur les formes** (unir, soustraire, intersecter, couteau) ;
9. ordre de couture manuel et optimisé (par couleur / proximité, verrous) ;
10. **coupes DST** visibles par la machine (seuil réglable, coupe avant changement de fil, points d'arrêt, coupe finale) —
    **à tester sur votre machine** ;
11. analyse pré-export (points trop courts/longs, sauts, hors cadre) et simulation de couture animée ;
12. export/import **DST**, export SVG de diagnostic, échange DXF ;
13. format de projet **`.osp`** (sauvegarde/chargement complet), projets récents, sauvegarde automatique et récupération.

Undo/redo sur toutes les opérations. Un outil en ligne de commande, `openstitch-cli`, expose le même moteur sans
interface ([docs/source/cli.md](docs/source/cli.md)).

Le nombre de tests évolue à chaque commit : le chiffre à jour est celui de `ctest --preset msvc-release` (ou du dernier run CI).

Voir la [feuille de route de parité avec Hatch](docs/roadmap-parite-hatch.md) (état de chaque fonctionnalité), le [plan de livraison](specs/implementation-roadmap.md), la [roadmap d'origine](docs/phase0/08-roadmap-adr.md) (historique) et l'[étude de cadrage](docs/phase0/README.md).

> **Note honnête** : ce socle est complet et testé, mais n'a pas encore été validé sur une machine à broder réelle. Les heuristiques de compensation (tirage, densité), l'auto-satin par squelette et les conventions DST de certaines machines (coupes) demandent des essais terrain avant un usage en production.

## Compilation (développeurs)

Seul le chapitre [docs/source/installation.md](docs/source/installation.md) décrit la compilation (le fichier
[docs/build-windows.md](docs/build-windows.md) y renvoie). Deux voies :

**Voie rapide — `scripts\build.ps1` installe tout ce qui manque.** Le script liste les outils absents (CMake, Visual Studio
Build Tools + workload C++ ≈ 5 Go, vcpkg, Qt 6.8.3 via aqtinstall), **demande confirmation**, les installe — il écrit les variables
utilisateur `VCPKG_ROOT` et `QT_ROOT` — puis configure et compile Debug **et** Release :

```powershell
.\scripts\build.ps1                       # confirmer, installer l'outillage manquant, tout compiler
.\scripts\build.ps1 -Test                 # ... puis lancer les tests
.\scripts\build.ps1 -Yes                  # confirme d'avance (aucune invite)
.\scripts\build.ps1 -SkipBootstrap -Configuration Debug -Test   # poste déjà configuré : n'installe rien
```

**Voie manuelle** (vous installez vous-même l'outillage) :

```powershell
# Prérequis : Visual Studio 2022+ (C++), CMake ≥ 3.27, Git, vcpkg, Qt 6 (binaires officiels)
$env:VCPKG_ROOT = "C:\chemin\vers\vcpkg"
$env:QT_ROOT    = "C:\chemin\vers\Qt\6.8.3\msvc2022_64"

cmake --preset msvc
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

## Structure

```
apps/desktop   Application Qt (seule cible dépendant de Qt)
apps/cli       openstitch-cli : info / stats / digitize / dst2svg / osp2dst + diagnostics (sans interface graphique)
libs/
  core            unités fortes (µm/mm/px), ids, Result, logging
  geometry        chemins, simplification (Douglas-Peucker), booléens/offsets (Clipper2)
  image           chargement + prétraitement non destructif (OpenCV encapsulé)
  segmentation    quantification CIELAB, régions connexes à ids stables
  ai_segmentation catalogue de modèles et protocole du worker de segmentation par IA
  vectorization   régions → contours vectoriels propres
  document        modèle métier (projet, objets vectoriels et de broderie)
  stitch          commandes machine, statistiques
  stitch_generation  point droit/triple, tatami, satin, routage, retouches manuelles
  auto_satin      squelette, satinabilité, auto-satin par traversées orientées
  stitch_analysis    moteur de règles de validation, mesures de qualité
  thread_palette  catalogue de fils (pas encore relié à l'interface)
  optimization    ordre de couture (coût, stratégies)
  autodigitize    image → objets éditables automatiquement
  commands        undo/redo (Command pattern)
  formats         codec DST maison, SVG de diagnostic, DXF
  project_io      format projet .osp (JSON + ZIP)
tests/         tests unitaires par bibliothèque, intégration, CLI (ctest -R cli_) et gardes structurelles
docs/          Documentation (source/*.md compilée en PDF par docs/scripts/build-docs.ps1)
```

Le cœur (tout sauf `apps/desktop`) ne dépend jamais de Qt — vérifiable en compilant la CLI et via le job Linux de la CI.

## Contribuer

Le projet est conçu pour être contribuable : dépendances via vcpkg (manifeste versionné), presets CMake, CI. Voir [docs/source/contributing.md](docs/source/contributing.md) (prérequis, dont Python pour régénérer la documentation). Règle absolue : **aucun code copié d'un logiciel propriétaire ni d'une source incompatible Apache-2.0**.
