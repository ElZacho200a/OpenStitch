# Dépannage

Public : utilisateur, développeur. Pour chaque problème : symptôme, cause
probable, diagnostic, solution.

## Compilation

| Symptôme | Cause probable | Solution |
|---|---|---|
| « Qt6 introuvable » | `QT_ROOT` absent/incorrect | Pointer sur `...\msvc2022_64`, reconfigurer |
| Erreur toolchain vcpkg | `VCPKG_ROOT` absent, bootstrap manquant | Définir la variable, relancer `bootstrap-vcpkg.bat` |
| DLL manquante au lancement | déploiement Qt/OpenCV incomplet | Lancer depuis le dossier `Debug`/`Release` (windeployqt a copié les DLL) |
| `LNK1168` à l'édition de lien | l'application tourne encore | Fermer `openstitch.exe` puis recompiler |
| OpenCV très longue à compiler | premier configure vcpkg | Normal ; les runs suivants utilisent le cache |

## Usage

| Symptôme | Cause probable | Diagnostic | Solution |
|---|---|---|---|
| Image non chargée | format/fichier corrompu | `openstitch-cli info fichier` | Réexporter l'image en PNG |
| Segmentation « incorrecte » | trop/peu de couleurs, petites régions | carte des régions | Ajuster N et la taille min, fusionner/supprimer |
| Aucun point généré | objet sans géométrie, tous invisibles | barre d'état / Statistiques | Vérifier qu'un objet vectoriel source existe et est visible |
| Satin impossible | rails non constructibles / forme non allongée | message à la création | Préférer un tatami |
| Remplissage qui déborde | trou perdu à la vectorisation | zoom sur la zone | Corrigé : tolérance adaptative + routage ; re-numériser |
| Motif hors cadre | taille physique trop grande | règles / cadre rouge | Réduire la taille à l'import, ou déplacer |
| Points trop longs | longueur de point élevée | Analyse (F5) | Réduire `stitch_length` |
| Export DST refusé | séquence vide | message | Générer des points d'abord |
| Fichier DST « vide » | aucun objet visible | `openstitch-cli stats` | Vérifier les objets et l'ordre |
| Couleurs incorrectes | pas de palette de fils | — | Fonctionnalité prévue ; RGB par objet en attendant |
| Projet impossible à ouvrir | `.osp` corrompu / version inconnue | message d'erreur | Vérifier le fichier ; version de schéma supportée = 1 |
| Performances faibles | très gros motif | — | Corrigé : rendu deux couches, pastilles limitées |
| Crash | entrée inattendue | — | Aucune entrée utilisateur ne devrait faire planter ; signaler avec un cas minimal |

## Logs

Le logger (spdlog) écrit sur la **sortie standard/erreur** de la console. Lancer
la CLI ou l'application depuis un terminal pour voir les messages. *Information
non déterminée* : aucun fichier de log persistant n'est écrit par défaut.

## Implémentation associée

- `libs/core/src/log.cpp` — initialisation du logger.
- `libs/*/src/*` — les erreurs renvoyées via `Result<T>` avec un message montrable.
