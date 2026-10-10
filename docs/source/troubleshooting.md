# Dépannage

Public : utilisateur (première partie), développeur (seconde partie). Pour chaque
problème : symptôme, cause probable, diagnostic, solution. Les messages d'erreur
de l'application et de la ligne de commande disent en général quoi vérifier ;
la ligne de commande préfixe les siens par `openstitch-cli <sous-commande> :` et
ajoute une ligne « Piste : » (voir [Ligne de commande](cli.md)).

## Partie 1 — Utilisation

### Installation et lancement

| Symptôme | Cause probable | Solution |
|---|---|---|
| Windows affiche « a protégé votre PC » / « Éditeur inconnu » (SmartScreen) | binaires non signés | **Informations complémentaires** puis **Exécuter quand même** (voir [Installation](installation.md)) |
| « `Qt6Core.dll` / `opencv_*.dll` / `VCRUNTIME140.dll` est introuvable » | ZIP non extrait en entier, `openstitch.exe` copié sans ses voisins, ou Visual C++ Redistributable absent | Extraire tout le ZIP (ou utiliser l'installeur) ; installer le Redistributable Visual C++ 2015-2022 x64 |
| L'application ne s'ouvre pas, aucun message | erreur au démarrage visible seulement en console | Lancer `openstitch.exe` depuis un terminal : les messages sortent sur la sortie d'erreur |
| Disposition cassée, panneaux introuvables | préférences de disposition corrompues ou fenêtre hors écran | Affichage ▸ Panneaux ▸ Réinitialiser la disposition ; sinon supprimer `HKCU\Software\OpenStitch` |

### Image, régions et formes

| Symptôme | Cause probable | Diagnostic | Solution |
|---|---|---|---|
| Image non chargée | format/fichier corrompu | `openstitch-cli info fichier` | Réexporter l'image en PNG |
| Segmentation « incorrecte » | trop/peu de couleurs, petites régions | carte des régions | Ajuster N et la taille min, fusionner/supprimer |
| Motif hors cadre | taille physique trop grande | règles / cadre rouge | Réduire la taille à l'import, ou déplacer ; ajuster le cadre (Affichage ▸ Taille du cadre) |
| Le fond est brodé, ou une partie du motif manque | mauvais réglage de « Ignorer la plus grande région » | dialogue de numérisation (couleur et part de l'image affichées) | Relancer avec la case cochée (fond) ou décochée (image plein cadre) |
| Remplissage qui déborde de la forme | contour vectorisé trop approximatif | zoom sur la zone ; SVG `openstitch-cli osp2svg --outlines` | Re-numériser avec un niveau de détail plus élevé ; ajuster le retrait du tatami |

### Satin et auto-satin

| Symptôme | Cause probable | Solution |
|---|---|---|
| « Satin impossible » : l'auto-satin refuse la forme | forme compacte (disque, blob), trop large, ou sans axe exploitable : le moteur par squelette n'a pas pu construire de colonne | Accepter le **tatami** proposé ; ou simplifier/épaissir la forme ; le message donne la raison (voir *Satin par squelette*) |
| Colonne satin trop large (avertissement) | largeur au-delà de la recommandation | Préférer un tatami, ou fractionner les traversées longues |
| Le satin est irrégulier ou orienté bizarrement | guides ou squelette de la forme | Poser un guide d'orientation (inspecteur ▸ « Placer un guide sur le canevas… ») ; sinon tatami. L'auto-satin est **expérimental et non validé sur machine** |
| Satins automatiques qui débordent dans un ancien projet | colonnes satin générées par une ancienne version | Broderie ▸ Convertir les satins auto en tatami |

### Points, couleurs et export

| Symptôme | Cause probable | Diagnostic | Solution |
|---|---|---|---|
| Aucun point généré | objet sans géométrie, tous invisibles | barre d'état / Statistiques | Vérifier qu'un objet vectoriel source existe et est visible |
| Points trop longs | longueur de point élevée | Analyse (F5) | Réduire la longueur de point dans l'inspecteur |
| Export DST refusé | séquence vide | message | Générer des points d'abord |
| Fichier DST « vide » | aucun objet visible | `openstitch-cli stats fichier.dst` | Vérifier les objets, leur visibilité et l'ordre |
| **La machine ne lit pas le DST** | extension/format attendu par la machine différent, support (clé USB, FAT32), nom trop long, ou en-tête/convention non gérée par ce modèle | `openstitch-cli stats` relit le fichier ; `dst2svg` l'affiche | Essayer un nom de fichier court sur une clé vide formatée comme l'attend la machine ; ouvrir le DST dans un autre logiciel de transfert pour confirmer qu'il est valide ; **signaler** le modèle de machine : aucune validation sur machine réelle n'a encore été faite (voir [Limitations](limitations.md)) |
| La machine ne **coupe pas** le fil entre les objets ou en fin de motif | certaines machines ignorent ou suppriment les sauts de coupe de déplacement nul | voir *Format DST* (coupes à sauts non nuls, coupe finale) | Régler le seuil de coupe (Broderie ▸ Options de génération…) ; **tester sur la machine** : le seuil de trois sauts peut devoir être augmenté selon le modèle |
| Couleurs incorrectes à la broderie | l'application ne gère que des couleurs RGB par objet, pas de références de fil | — | Choisir les fils sur la machine dans l'ordre des changements de couleur affiché (voir [Palettes et fils](palettes-and-threads.md)) |
| Projet impossible à ouvrir | `.osp` corrompu / version inconnue | message d'erreur | Vérifier le fichier ; version de schéma supportée = 1 ; essayer la sauvegarde automatique (voir ci-dessous) |
| Crash | entrée inattendue | — | Aucune entrée utilisateur ne devrait faire planter ; signaler avec un cas minimal et le projet `.osp` |

### Perte de travail et sauvegarde automatique

**Où est l'autosave ?** Dans `%APPDATA%\OpenStitch\OpenStitch Studio\autosave\`
(fichiers `<empreinte>.osp` et `.json`), toutes les 2 minutes quand le document est
modifié. Au démarrage suivant un arrêt anormal, un dialogue **Récupération après un
arrêt anormal** propose **Récupérer** ou **Ignorer** (l'entrée traitée n'est plus
reproposée). Une fermeture normale supprime la sauvegarde, et « Ignorer » la supprime aussi :
enregistrez régulièrement avec **Ctrl+S**.

### Segmentation par IA

| Symptôme | Cause probable | Solution |
|---|---|---|
| « La segmentation par IA n'est pas activée » | case désactivée dans les préférences | Édition ▸ Préférences — Intelligence artificielle… |
| L'IA ne démarre pas / « le worker n'a pas pu démarrer » | worker Python introuvable ou incomplet : chemins vides, distribution WSL absente, `torch`/`sam2` non installés dans le venv, dossier de modèles sans checkpoint | Dans les préférences IA, remplir tous les chemins puis **Tester la configuration** (le journal affiche l'erreur exacte). Prérequis détaillés dans le [Guide utilisateur](user-guide.md) ; la fonction n'est **pas** livrée avec les binaires de release |
| Le test dit « Configuration incomplète » | un chemin du formulaire est vide | Renseigner Python du venv, script du worker et dossier des modèles |
| Très lent | exécution sur CPU, grosse résolution | Choisir un modèle plus petit, un GPU (CUDA) ou baisser la résolution maximale d'analyse |

### Journaux

L'application et la ligne de commande écrivent leurs messages (spdlog) sur la
**sortie d'erreur** de la console : **aucun fichier de log persistant**. Pour les
voir, lancez `openstitch.exe` ou `openstitch-cli.exe` depuis un terminal
(`openstitch.exe 2> journal.txt` pour les garder). Le worker IA a son propre
niveau de journalisation dans les préférences IA.

## Partie 2 — Compilation (développeurs)

| Symptôme | Cause probable | Solution |
|---|---|---|
| « Qt6 introuvable » | `QT_ROOT` absent/incorrect | Pointer sur `...\msvc2022_64`, reconfigurer |
| Erreur toolchain vcpkg | `VCPKG_ROOT` absent, bootstrap manquant | Définir la variable, relancer `bootstrap-vcpkg.bat` |
| `build.ps1` refuse d'installer (« non confirmée ») | console non interactive | Relancer avec `-Yes`, ou avec `-SkipBootstrap` après installation manuelle |
| DLL manquante au lancement d'un build local | déploiement Qt/OpenCV incomplet | Lancer depuis le dossier `Debug`/`Release` (windeployqt a copié les DLL) |
| `LNK1168` à l'édition de lien | l'application tourne encore | Fermer `openstitch.exe` puis recompiler |
| OpenCV très longue à compiler | premier configure vcpkg | Normal ; les runs suivants utilisent le cache |
| Génération du PDF de documentation impossible | Python/venv absent | Python 3.10+ ; `docs\.venv` est créé par `docs\scripts\build-docs.ps1` (voir [Guide de contribution](contributing.md)) |
| `ctest` échoue sur les tests du bureau | affichage absent | Les tests Qt tournent en **headless** : `QT_QPA_PLATFORM=offscreen` |

## Implémentation associée

- `libs/core/src/log.cpp` — initialisation du logger (sortie d'erreur).
- `apps/desktop/autosave.cpp`, `apps/desktop/main_window.cpp` — sauvegarde automatique et récupération.
- `apps/desktop/ai_preferences_dialog.cpp` — préférences IA et test de configuration.
- `libs/*/src/*` — les erreurs renvoyées via `Result<T>` avec un message montrable.
