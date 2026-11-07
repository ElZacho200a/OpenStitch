# Recette Studio de bout en bout

## CX13 — parcours 1 à 12

Base de préparation : `origin/claude/studio-lot1-parcours-edition`, initialement `15942b0`,
puis reprise sur `06780ae` qui ajoute le compagnon H et les corrections de focus.
Cette recette utilise la vraie `Main.qml`, ses contrôleurs de production et le vrai moteur.
Les actions testées entrent par des événements souris/clavier QTest. Les API cœur servent
uniquement à préparer certaines fixtures et à vérifier les résultats. Le sélecteur de fichier
reçoit un dossier/une sélection initiale déterministe ; l'acceptation passe par le clavier et
le gestionnaire QML de production. Les sorties machine sont comparées en octets DST, après
annulation et sauvegarde/relecture OSP. Aucun projet ou point simulé ne remplace le moteur.

| Parcours | Fichier | Vérification prévue | État des preuves |
|---|---|---|---|
| 01 | journey_01_image.cpp | Tentabrode importé à 300 mm via réglages QML, aperçu de segmentation sans mutation, OSP/DST | À exécuter |
| 02 | journey_02_drawing.cpp | Projet vierge, dessin souris, broderie réelle, undo/redo et sauvegarde clavier | À exécuter |
| 03 | journey_03_svg.cpp | SVG physique, objets/points, sélection/déplacement, retour exact | À exécuter |
| 04 | journey_04_machine.cpp | Lecture DST et ouverture panneau Points | BLOCKED pour édition de séquence importée : pas de cible éditable dans cette base ; PES et réexport après édition non couverts |
| 05 | journey_05_regions.cpp | Sélection/recoloration de région, régénération et deux annulations exactes | À exécuter |
| 06 | journey_06_satin.cpp | Fixture satin réelle, édition d'un rail par champs QML, undo et persistance | À exécuter |
| 07 | journey_07_parameters.cpp | Densité par saisie clavier, refus hors bornes, OSP et undo | À exécuter |
| 08 | journey_08_multiselect.cpp | Sélection multiple, déplacement clavier, miroir souris, undo et persistance | À exécuter |
| 09 | journey_09_nodes_knife.cpp | Couteau et nœud par gestes canevas, undo exact | À exécuter |
| 10 | journey_10_fade.cpp | Création d'un fondu et déplacement de son axe via QML | À exécuter |
| 11 | journey_11_pull_density.cpp | Compensation puis calcul de densité sans changer les octets compensés | À exécuter |
| 12 | journey_12_order.cpp | Proposition sur Tentabrode, absence de mutation avant approbation, application/undo | À exécuter |

La couverture annoncée ici est précise : par exemple, le parcours 01 ne valide pas encore toute
la chaîne de modifications de régions, couverte séparément au parcours 05 ; le parcours 06
teste les champs des rails et ne constitue pas une validation de poignées graphiques absentes.
Un parcours partiel n'est pas une recette finale validée. Les états TESTED/VALIDATED des fonctions
restent dans leurs fichiers propriétaires ; ce document ne les duplique pas dans la matrice.

### Exécution

```powershell
cmake --build --preset msvc-debug --target test_studio_e2e --parallel 1
ctest --preset msvc-debug -R studio_e2e --output-on-failure
cmake --build --preset msvc-release --target test_studio_e2e --parallel 1
ctest --preset msvc-release -R studio_e2e --repeat until-fail:5 --output-on-failure
```

Chaque parcours est un test CTest distinct. Un `QSKIP("BLOCKED: ...")` indique une dépendance
manquante et doit être lu comme **bloqué**, même si CTest considère techniquement le processus
réussi. Les journaux doivent être consultés ; un résultat CTest vert seul ne prouve pas que les
douze parcours ont tous été exécutés. Aucun résultat Debug/Release n'est revendiqué avant
exécution et publication de la provenance correspondante.

## CX14 — parcours 13 à 23

Préparation sur `06780ae`, branche `codex/studio-cx14-e2e-b`. Aucun résultat d'exécution
n'est revendiqué avant compilation et tests Debug/Release.

| Parcours | Vérification préparée | Limite connue sur la base |
|---|---|---|
| 13 | Retouche d'un point mobile par clic canevas et champs QML, OSP/DST et undo | À exécuter |
| 14 | Accès à l'outil texte ; contrôle d'absence de mutation avant validation | BLOCKED : parcours appliqué Studio incomplet |
| 15 | Recoloration, coloris et recherche de nuancier | À exécuter ; stock personnel CX10 indépendant |
| 16 | Défilement point/couleur/fin et sortie de simulation, octets invariants | À exécuter |
| 17 | Ouverture des contraintes d'export | BLOCKED : contrôle avant écriture dépend de CX06 non intégrée |
| 18 | Proposition MCP réelle, aperçus avant/après, approbation souris, OSP/DST et undo | À exécuter sur H ; seul le fournisseur de langage externe est scripté hors réseau |
| 19 | Archives de schéma 1 à 9, édition de rail QML, sauvegarde temporaire et source immuable | Fixtures de compatibilité synthétiques documentées, pas des fichiers historiques d'utilisateurs |
| 20 | Processus enfant qui sauvegarde puis appelle _Exit sans destructeurs ; nouveau processus documentaire et récupération QML | Le même autosave de production est appelé explicitement ; délai périodique non mesuré |
| 21 | Vrai rendu logiciel, même snapshot après zoom ; variante CTest D3D11 WARP | La variante WARP peut être BLOCKED si D3D11 indisponible |
| 22 | Noms accessibles, parcours Tab et focus après fermeture du panneau | Toute lacune est listée et classée BLOCKED pour la famille UX/H |
| 23 | Tentabrode 300 mm : lecture OSP, construction de scène, capture de frame et édition clavier | Mesures réelles seulement après exécution ; aucune assertion de FPS |

Le parcours 20 lance le même exécutable de recette dans un processus distinct, provoque une
édition par clavier et une sauvegarde automatique, puis quitte via `std::_Exit(86)`. L'application
ne détruit donc ni ses contrôleurs ni sa session. Le parent ouvre ensuite une nouvelle scène,
clique sur « Récupérer », vérifie les octets machine et sauvegarde. Cette interruption contrôlée
ne prétend pas reproduire toutes les causes de crash système.

Au parcours 23, `load_project_wall_ms` et `build_studio_scene_wall_ms` mesurent les appels de
production avec une horloge murale. `capture_frame_readback_wall_p50_ms` inclut le rendu ET
la lecture de l'image. `resource_creation_ms` est la mesure du canevas. Ces valeurs ne sont
ni des FPS affichés, ni un temps GPU pur. La configuration et le backend sont inscrits au journal.
Les cinq répétitions doivent être réalisées en Release, après intégration des dépendances.

Les CSV CX13/CX14 restent sans doublons d'IDs : ces parcours transversaux n'inventent pas de
nouveaux IDs V1 et ne prennent pas la propriété des fonctions déjà suivies dans les autres familles.

