# CX15 — Distribution Windows de Studio

L’exécutable existant `openstitch_v2_studio.exe` est conservé. Le bundle et les raccourcis
de l’installateur désignent Studio. Les anciens bureaux restent dans le dépôt et leurs
cibles CMake restent distinctes. Aucun nom de cible n’a été changé.

## Construire et déployer

```powershell
./scripts/build.ps1 -SkipBootstrap -Configuration Release
./packaging/windows/package-studio.ps1 -BinaryDir build/msvc/apps/desktop_v2/studio/Release -OutputDir dist/windows-studio
```

Le script crée un nouveau répertoire `openstitch-studio-windows-x64`, déploie Qt avec
`windeployqt --release --qmldir apps/desktop_v2/studio/qml`, ajoute les DLL voisines,
les polices Vera et leur licence, puis produit un ZIP et un SHA256. Il vérifie les modules
Quick/Controls/Dialogs, le plugin Windows et les polices. Choisir une nouvelle destination
pour une nouvelle exécution ; le script ne remplace pas un bundle existant.

Le `qt.conf` garde plugins et imports QML dans le bundle. Le démarrage de Studio cherche
les polices dans `resources/fonts` près de l’exécutable, indépendamment du répertoire courant.
Pour forcer le repli CPU : `openstitch_v2_studio.exe --software projet.osp`.

## Recette sur archive extraite

Générer le vrai projet Tentabrode à 300 mm selon `18-studio-benchmarks.md`, extraire le ZIP
puis lancer :

```powershell
./tests/packaging/smoke-studio.ps1 -BundleDir C:/verification/openstitch-studio-windows-x64 -Project build/tentabrode-300mm.osp -OutputDir build/studio-smoke
```

Le test retire Qt du PATH et les variables de développement Qt, utilise un répertoire
courant distinct, ouvre le projet avec une vraie fenêtre, capture un PNG natif puis quitte
avec code 0, en rendu natif puis `--software`. Un ancien PNG fait échouer le test. Les journaux,
PNG, SHA256 du binaire/projet et durées du processus sont conservés dans `smoke.json`.
Ces durées sont des temps de processus et ne mesurent pas des FPS.

## Release et installateur

**CI modifiée :** `.github/workflows/release.yml` produit le bundle Studio, génère Tentabrode,
extrait l’archive, exécute la recette avec PATH nettoyé et conserve ses preuves. L’installateur
Inno Setup consomme ce bundle et lance Studio. La release inclut aussi le ZIP Studio et son
SHA256. Les étapes MCP gardent leur propre bundle.

Les sources de l’ancien desktop et de V2 Widgets sont conservées. Le workflow est seulement
modifié dans la branche de travail ; aucun lancement distant ni publication n’a été demandé.

Validation locale : scripts PowerShell parsés sans erreur ; génération Release, lancement
sur archive propre, captures et compilation de l’installateur restent à exécuter. Le simple
contrôle syntaxique ne valide pas la recette d’installation.

Arret demande par le proprietaire : sauvegarde et push en l'etat. Les scripts ont ete
verifies sur un ancien binaire baseline34d6843 uniquement. Les sources CX15 actuelles,
le build Release, l'installateur et l'installation sur Windows vierge restent non valides.
