# Audit UI et ergonomie — 2026-10

Méthode : trois relectures de code indépendantes (structure et raccourcis, parcours
utilisateur, accessibilité et rendu) + captures d'écran headless de la fenêtre réelle
(`openstitch-ui-shots`, `QT_QPA_FONTDIR=C:\Windows\Fonts`). « Lu » = vu dans le code ;
« capturé » = vu sur une capture ; « supposé » = non vérifié. Aucun correctif appliqué par cet
audit. Efforts : S < 1 h, M < 1 jour, L > 1 jour.

## Bloquants (perte de travail)

| # | Constat | Où | Correctif | Effort |
|---|---|---|---|---|
| B1 | `openImage()` écrase le projet sans demander d'enregistrer (new/recent/fermeture le font). Idem `openSvg()`. | `main_window.cpp:1030-1089` | `confirmDiscardChanges` en début de fonction | S |
| B2 | `importDst()` demande « remplacer le document ? » sans proposer d'enregistrer ni tester `isWindowModified`. | `main_window.cpp:6620-6640` | même garde | S |

## Majeurs

| # | Constat | Où | Correctif | Effort |
|---|---|---|---|---|
| M1 | **Fenêtre trop haute** (capturé) : demandée à 1024×640, elle reste à ≈ 935 px de haut ; ne tient pas sur un portable 1366×768. Au défaut, le canevas ne fait que ≈ 380 px de large à 1024 px (liste « Document » à gauche, inspecteur à droite). | `main_window.cpp` (docks, `restoreState`) | tailles minimales des docks, liste Document plus étroite, regrouper Filtres dans l'inspecteur | M |
| M2 | Opérations longues synchrones, sans progression ni annulation (segmentation, vectorisation, auto-numérisation) : simple curseur d'attente. | `main_window.cpp:3412, 3475` | `QProgressDialog` indéterminé court terme ; tâche asynchrone sur snapshot à terme | M / L |
| M3 | Export DST : le sélecteur de fichier s'ouvre avant le résumé ; aucun lien avec l'analyse (erreurs non montrées avant export). | `main_window.cpp:6550-6590` | résumé + état d'analyse d'abord, puis sélecteur | S/M |
| M4 | Messages d'erreur bruts du moteur dans une boîte titrée « Erreur ». | `main_window.cpp:1042, 1078, 2413` | titre précis, phrase d'aide, détail technique en `setDetailedText` | S |
| M5 | Boutons de la barre principale = actions distinctes des menus : jamais grisés, pas de raccourci ni d'info-bulle de raccourci (Enregistrer cliquable alors que le menu est grisé). | `main_window.cpp:4991-5009` | réutiliser les `QAction` membres | S |
| M6 | Actions grisées sans explication (Créer un tatami, Éditer les points, Exporter…). | `updateActions` `:7303-7340` | `statusTip`/`toolTip` dynamiques avec la raison | M |
| M7 | Menus peu lisibles par workflow : trois segmentations dans deux menus, « Supprimer » (Suppr) rangé dans Segmentation, Dupliquer/Décaler seulement au menu contextuel, menu Analyse d'une seule entrée. | `main_window.cpp:584-822, 5825` | regrouper, déplacer vers Édition | M |
| M8 | Icônes 32×32 sans `devicePixelRatio` affichées à 18/20 px : floues en HiDPI ; encre unique 5C626A = 2,21:1 en thème sombre (quasi invisibles). | `ui_icons.cpp:15-36` | rendu à la taille × dpr, recoloration selon le thème | M |
| M9 | Accessibilité : 2 `setAccessibleName` sur 600+ widgets, aucun `setTabOrder` ; champs d'inspecteur sans nom pour lecteurs d'écran. | `properties_panel.cpp` | `QFormLayout::addRow(label)` + `setBuddy`, `setTabOrder` | M |

## Mineurs

- Inspecteur satin : 20 champs à plat sans regroupement (espacement / fractionnement /
  compensation / sous-couches / terminaisons / fixations / guides) ; liste de guides vide sans
  texte d'aide ; jargon « Terminaison », « Fixation », « Décalé » (capturé).
- Liste Document : 100+ lignes « Tatami — Remplissage Région 1624 » : noms redondants et
  numéros sans sens, pas de recherche ni de vignette (capturé).
- Libellés : accents manquants dans les actions directionnelles (« Generer un guide »,
  `main_window_directional.cpp:116-143`) ; « colonne satin » vs « satin » mélangés ; info-bulles
  de la palette « … (Maj = cercle) (O) » mal formées (`main_window.cpp:5242`).
- Raccourcis : aucun pour Exporter DST, Statistiques, Numérisation auto ; `F` (QShortcut) non
  affiché dans le menu ; touches simples (V, H, M, R, O…) possiblement déclenchées depuis les
  docks (supposé).
- Historique : noms sans verbe (« Objet de broderie », « Segmentation ») → « Annuler Objet de
  broderie ».
- Contrastes : bordures de champs 1,3-1,44:1 (WCAG 1.4.11 demande 3:1) ; saut ambre 2,86:1 ;
  repère d'aimantation orange codé en dur 1,96:1 ; rouge/vert sans doublon de forme.
- Poignées de nœud 14×14 px (trop petit pour le tactile) ; pixels codés en dur
  (barres de défilement 12 px, pastilles 24 px).
- Pas de « Réinitialiser la disposition » si un `restoreState` est corrompu.
- Autosave invisible (aucun message « sauvegarde auto 14:32 ») ; récupération : un dialogue par
  candidat, sans aperçu.
- Doc ≠ UI : le guide utilisateur ne mentionne ni « Convertir en satin », ni le satin par
  squelette, ni les guides ; `Ctrl+Y` documenté, raccourci réel = `QKeySequence::Redo`.
- À l'ouverture d'un projet l'image apparaît en miniature au centre d'un canevas vide (capturé,
  à confirmer à la main : pas de « ajuster au canevas » automatique).

## À faire en premier

1. **B1 + B2** (S) : garde « modifications non enregistrées » sur ouvrir image/SVG et import DST.
2. **M1** (M) : fenêtre utilisable à 1366×768, canevas prioritaire.
3. **M2 + M3 + M4** (S/M) : progression, export précédé du résumé, erreurs lisibles.
4. Lot d'harmonisation S : M5, accents, libellés, raccourcis, noms d'historique.

## État des correctifs (2026-10, branche de recherche)

**Corrigé** : B1, B2 (garde « modifications non enregistrées » sur ouvrir image/SVG et importer
DST) · M1 (panneaux défilants : hauteur minimale 934 → 374 px, fenêtre utilisable à 1024×640 ;
Filtres en onglet derrière Propriétés ; tailles de panneaux par défaut ; « Réinitialiser la
disposition ») · M3 (résumé + analyse avant le sélecteur d'export) · M4 (erreurs d'ouverture et
d'import : titre précis, explication, détail technique repliable) · M5 (barre principale =
actions des menus) · M6 (raison de désactivation dans l'infobulle et la barre d'état) · M7
(Supprimer/Dupliquer/Décaler dans Édition, IA dans Segmentation, Statistiques dans Analyse) ·
M8 (icônes 1×/2× et encre ≥ 3:1 sur clair et sombre) · inspecteur satin regroupé avec texte
d'aide · recherche et libellés allégés dans le panneau Document · accents, infobulles de
palette, noms d'historique avec verbe · Ctrl+E, Ctrl+0 + F sur la même action · contrastes des
bordures (≈ 3:1) et du saut (4,0:1) · poignées 22 px · repère d'aimantation thémé ·
message d'autosave · guide utilisateur mis à jour.

**Partiel** : M2 (une fenêtre « en cours » sans bouton Annuler : le calcul reste synchrone et non
interruptible, la tâche de fond reste à faire) · M9 (les champs de l'inspecteur sont nommés par le
libellé du `QFormLayout` ; pas de `setTabOrder` explicite) · noms d'historique : « Suppression de
région » et « Déplacement de point » gardés tels quels (des tests s'y réfèrent).

**Non fait** : double codage par forme pour le daltonisme (rouge/vert, rails G/D) · tailles de
police pilotées par les jetons et pixels codés en dur restants · vignettes dans la liste Document ·
raccourcis pour Statistiques et Numérisation automatique · aperçu dans le dialogue de récupération
d'autosave · vérification clavier réelle des touches simples depuis les docks (supposé).

### UX globale et Analyse (branche claude/ux-globale-analyse)

Traité à partir de l'audit transversal de `apps/desktop` (hors autosave et récupération,
traités ailleurs) :

- **Langue de Qt** : `windeployqt --translations fr` (au lieu de `--no-translations`) et
  `QTranslator` `qtbase_fr` chargé dans `main.cpp` (dossier de Qt, puis `translations` à côté
  de l'exécutable ; repli silencieux sur l'anglais de Qt si absent).
- **Ouverture** : argument de ligne de commande et glisser-déposer routés par extension
  (`MainWindow::openPath`, `openImageFile`, `importDstFile`) avec les gardes existantes ; reporté
  tant qu'un dialogue modal est ouvert (récupération d'autosave).
- **Touches** : Entrée / Retour arrière désactivés hors tracé (`updateShortcutsState`) ;
  Échap reste armé (il abandonne aussi un geste du canevas) ; filtre `ShortcutOverride` qui rend
  lettres seules aux `QComboBox` / vues et Entrée / Retour arrière / Échap aux champs.
  Rétablir = Ctrl+Y et Ctrl+Maj+Z ; Ctrl+= zoome ; F6 Statistiques ; Ctrl+Maj+A numérisation
  automatique. **Non changé** : Ctrl+O reste « Ouvrir une image » (changement non justifié sans
  concertation sur la garde d'ouverture de projet).
- **Disposition** : le mode « Masquer les panneaux » est levé avant `saveState` ; état versionné
  (`saveState(1)`), repli sur l'état historique puis sur la disposition par défaut ; fenêtre
  bornée à l'écran disponible (0,92 au premier lancement).
- **Thème et accessibilité** : contrastes ≥ 4,5:1 vérifiés par test sur les trois fonds
  (avertissement clair `#8F5A0F`, succès clair `#35703F`, succès/erreur/info sombres éclaircis),
  `#8a5a00` en dur remplacé par le jeton, texte désactivé distinct (`textDisabled`) et bouton
  désactivé lisible, anneau de focus des boutons d'outil et onglets, barres de défilement 14 px,
  textes d'aide en couleur secondaire (`markSecondaryText`) au lieu de `setEnabled(false)`,
  pastilles de couleur nettes en HiDPI avec liseré (`icons::colorSwatch`), noms accessibles
  (nombre de côtés, curseur et vitesse de simulation, type de points), ordre de tabulation,
  écran d'accueil sans cadre sur les libellés et limité à 5 récents, icône d'application, thème
  initial suivant le système.
- **Calculs longs** : segmentation, vectorisation et numérisation automatique tournent dans un
  fil de travail (`BusyIndicator::run`), l'interface repeint la fenêtre d'attente ; les minuteurs
  qui lisent le document (autosave, simulation) sont suspendus.
- **Analyse** : nombres sans `std::to_string` (« 3,5 mm »), objet fautif nommé, gravité en mots,
  compteurs et filtre, indice de correction (`Finding::hint`), clic / Entrée = sélection +
  centrage (position (0, 0) correcte), menu contextuel, plafond par catégorie rendu visible
  (`analyze_detailed`), résultat périmé signalé et ré-analyse à 300 ms.
- **Simulation** : rendu incrémental (O(n) au total), vitesse ×0,25 à ×16, réinitialisation à
  chaque changement du document, couleur et objet courants.
- **Messages d'état** : effacés après 10 s ; indicateur permanent « Enregistré à HH:mm ».

Reste à faire : Ctrl+O / Ctrl+I, menu Récents avec dossier, Préférences regroupées, dialogue
« modifications non enregistrées » nommant le projet, double codage des rails pour le
daltonisme, étapes du workflow qui lancent l'action.

**Constat retiré** : « l'image apparaît en miniature au chargement » est un artefact de l'outil de
captures (le projet est chargé avant l'affichage de la fenêtre) ; `applyLoadedProject` appelle bien
`fitCanvas()`.

### Segmentation et workflow (audit A, branche `claude/ux-segmentation-workflow`)

Numérotation du rapport d'audit « import → prétraitement → segmentation → vectorisation ».

**Corrigé** :

- **A4/A5** — « Vectoriser la sélection » (F7) traite **toutes** les régions sélectionnées en un
  `CompositeCommand` ; une région déjà vectorisée n'est plus dupliquée (Remplacer / Ignorer ou
  sélectionner l'objet existant / Annuler). Dernier niveau de détail mémorisé.
- **A7** — mode « Fusionner avec… » : message d'état, curseur « main », clic sur une région déjà
  sélectionnée ou dans le vide expliqué (le mode reste actif, Échap annule).
- **A8** — surbrillance de la région sous le curseur (un item raster réutilisé, masque indexé) et
  glissière d'**opacité de la carte** (20–100 %, mémorisée) dans le menu Segmentation.
- **A9** — outil `M` renommé « Recadrer l'image (glisser un cadre) », message d'état au choix de
  l'outil, confirmation si des objets existent (ils ne suivent pas l'image).
- **A10** — cadre de sélection avec objets vectoriels affichés : il choisit les régions s'il ne
  touche aucun objet, sinon un message dit de masquer les objets.
- **A11/A12** — la segmentation IA peut créer des **régions éditables** (segmentation du document,
  régions recolorées à leur couleur moyenne) ou des objets directs ; options *ignorer le fond* et
  *détail de vectorisation*, question « objets déjà présents » sur le chemin direct.
- **A13–A17** — dialogue IA : « Annuler l'analyse » fiable pendant le démarrage du worker
  (identifiant de requête périmé effacé) ; « Valider » grisé + curseur d'attente + message ;
  confirmation avant d'écraser des masques en revue (relance, fermeture) ; réglages mémorisés ;
  erreurs en rouge avec « Ouvrir les préférences… » et « Afficher le détail » ; en-têtes
  expliqués, tableau trié numériquement, aperçus redimensionnés avec la fenêtre.
- **A18** — aperçu du masque sélectionné écrit par `scanLine` (au lieu de `setPixelColor`).
- **A19/A20** — import : taille initiale ≤ cadre, « Ajuster au cadre », dpi affiché, bornes des
  champs qui préservent le ratio, alerte d'étirement quand les proportions sont libres.
- **A21** — quantification avec aperçu en direct (différé de 150 ms) et valeur mémorisée.
- **A23/A24** — Numérisation automatique et IA grisées avec leur raison ; les étapes du panneau
  Workflow lancent l'action (ou donnent la raison) avec les vrais noms de menus.
- **Mineurs** — A26 (dossier du dernier import), A28 (message après suppression de régions),
  A29 (fusion/suppression d'une région vectorisée : conserver ou supprimer l'objet, un seul pas
  d'annulation), A30 (la barre contextuelle réutilise les actions *Vectoriser* et *Supprimer*),
  A32 (F6 Segmenter, F7 Vectoriser, F8 Numérisation automatique), A34 (l'IA s'enchaîne après
  l'activation dans les préférences), A36 (derniers réglages de segmentation, vectorisation,
  numérisation mémorisés), A38 (taille minimale de région avec son équivalent mm²), A39 (infobulle
  IA), A40 (nombre de régions en barre d'état permanente).

**Non traité ici** : A1–A3 (confirmations de destruction, agent « sécurité »), A6 (calcul asynchrone
annulable), A22 (erreurs du moteur), A25 (glisser-déposer), A27, A31 (touches simples depuis les
docks), A33, A35, A37 (aperçu du contour de vectorisation).

## Deuxième audit — état des correctifs

Périmètre « perte de travail et fichiers » (branche `claude/ux-securite-donnees`).

**Corrigé**
- **Ouvrir un projet** : `loadProject` passe par `confirmDiscardChanges` (comme les Récents) et
  repart du dossier du projet courant. La question « non enregistré » nomme le projet concerné.
- **Récupération d'autosave** : Échap, la croix et « Décider plus tard » ne suppriment plus rien ;
  seul « Supprimer la sauvegarde » détruit. Un seul candidat est récupéré par session (les autres
  sont reproposés), après `confirmDiscardChanges`. Le créneau récupéré n'est supprimé qu'à
  l'enregistrement ou au premier tick d'autosave réussi (`pendingRecoveryOsp_`). Le chemin
  d'origine est restauré (Ctrl+S réécrit le bon fichier ; pas d'ajout aux Récents).
  Plusieurs instances : chaque instance verrouille son créneau (`QLockFile`, `claimAutosaveSlot`) ;
  un créneau tenu par une instance vivante n'est pas proposé, celui d'un processus mort l'est.
- **`load_project`** : aucune exception ne fuit (JSON valide mais structure invalide → erreur
  `InvalidFile`) ; message « créé par une version plus récente, mettez à jour OpenStitch » ;
  `LoadInfo` (version lue, migration) affichée dans la barre d'état ; au premier enregistrement par
  dessus un fichier migré (v1 à v4), copie `<nom>.vN.osp.bak` (annule l'enregistrement si la copie
  échoue).
- **Écritures atomiques** : l'export DST passe par `.tmp` puis renommage (l'ancien fichier survit à
  un échec) ; le repli de renommage de `save_project` garde l'ancien projet (`.old`) et cite le
  `.tmp` dans l'erreur ; chemins d'erreur et chemins minizip en UTF-8 (`u8string`).
- **Marqueur « modifié »** : `UndoStack::markClean/isClean` ; `setWindowModified(!isClean())` après
  undo/redo et dans `refreshImage` ; l'enregistrement et l'import DST rendent le document propre.
- **Segmentation** : opérations d'image et resegmentation demandent confirmation (« Continuer »
  / « Annuler », Ctrl+Z restitue) ; numérisation automatique : Ajouter / Remplacer / Annuler,
  « Remplacer » retire les broderies à choix automatique non verrouillées et non retouchées et leurs
  formes issues de la segmentation en une seule entrée d'annulation.
- **Erreurs** : `showFriendlyError` pour ouverture/enregistrement `.osp`, récupération, exports
  DST/DXF, segmentation, vectorisation, numérisation, opérations d'image.
- **Exports** : nom suggéré `<projet>.dst|dxf` dans le dossier du projet ou le dernier dossier
  utilisé, extension forcée, bouton par défaut « Annuler » et « Voir les problèmes » (ouvre
  l'analyse) s'il y a des erreurs d'analyse, action « Ouvrir le dossier » après un export réussi.
  « Enregistrer sous » : nom suggéré et accepte un document réduit à un design importé.
- **Récents** : chemin complet en info-bulle, mnémoniques `&1` à `&9`/`1&0`, purge
  (`QFileInfo::exists`) à l'ouverture du menu et après un échec d'ouverture seulement, plus à
  chaque enregistrement.

**Reporté / limites**
- Le verrou d'instance repose sur `QLockFile` : deux instances sur des profils utilisateur
  différents ne se voient pas. Le sidecar ne porte pas le PID.
- « Remplacer » ne conserve pas les formes vectorielles que l'utilisateur a re-liées à une région
  à la main (elles portent `source_region` comme les formes automatiques).
- Pas d'aperçu du contenu dans le dialogue de récupération.
