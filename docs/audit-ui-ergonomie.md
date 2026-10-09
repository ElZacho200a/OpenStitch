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

**Constat retiré** : « l'image apparaît en miniature au chargement » est un artefact de l'outil de
captures (le projet est chargé avant l'affichage de la fenêtre) ; `applyLoadedProject` appelle bien
`fitCanvas()`.

## Deuxième audit — état des correctifs

### Inspecteur et édition sur canevas (branche `claude/ux-inspecteur-edition`)

**Corrigé**
- **Inspecteur périmé (critique)** : la garde « même sélection, pas de reconstruction »
  laissait un formulaire obsolète après annulation, rotation au canevas ou changement de type
  de points qui garde le même id (`ConvertFillGroupCommand`) ; le premier champ touché écrasait
  des `DirectionalFillParams` par un `TatamiParams`. `PropertiesPanel::showsParams` compare le
  formulaire au document à chaque rafraîchissement et `updateInspector` reconstruit en cas
  d'écart. Tests : annulation puis champ, conversion tatami → directionnel puis champ.
- **Bornes (critique)** : espacement des rangées ≥ 0,1 mm, longueurs de point ≥ 0,5–1 mm,
  infobulles avec plage.
- **Écrasement silencieux** : chaque widget ne modifie que son champ (plus de relecture ni
  d'arrondi des autres : angle entier, µm arrondis à 10) ; angle en 0,1°.
- **Coalescence d'annulation** : `ICommand::mergeKey/mergeWith`, fenêtre de 600 ms dans
  `UndoStack` (ajout additif, `breakMergeChain`) ; paramètres, flèches, angle de guide ;
  noms « Modifier : <champ> ».
- **Molette** : `WheelGuard` (focus au clic, transmission au parent sinon).
- **Dépendances de champs** : sous-couches grisées, y ≤ Lmax dynamique, vocabulaire commun
  satin / auto-satin (forme du bout, point d'arrêt), unité et référence de 0° de l'angle des
  guides, clic sur un guide = anneau sur l'ancre.
- **Poignées** : seuil de glisser des nœuds (Mi9), coordonnées et repère d'accroche pendant le
  glisser, Échap annule, Maj ne suspend plus l'accroche, bascule d'accroche dans la barre
  d'état ; redimensionnement : zone 22 px, curseur par coin, planchers 5 % sans miroir, Maj =
  proportions, cadre d'aperçu et « Taille : L × H mm ».
- **Verrou** : renommé « Figer l'ordre » partout (il ne protégeait que de l'optimisation de
  l'ordre) ; le champ `locked` du `.osp` est inchangé.
- **Panneau Ordre** : libellés partagés avec Document, Monter/Descendre grisés aux bornes avec
  infobulles et Alt+Haut/Bas, bouton à bascule sans emoji, plus de clignotement (signaux bloqués
  pendant le rafraîchissement), « Trajet estimé ».
- **Multi-sélection et boîte** : bloc « Appliquer à N objets » (type, espacement, angle) en un
  pas ; Édition ▸ Aligner (6 actions) ; X/Y/L/H en mm + proportions pour un objet vectoriel.
- **Panneau Document** : rang de couture, cases Vis./Figé, renommage au double-clic,
  suppression au clavier, clic sur un groupe = toute la forme, noms accessibles.
- **Historique** : libellés à l'infinitif (« Déplacer la forme », « Supprimer une région »,
  « Déplacer un point »… ; tests mis à jour) et panneau **Historique** (clic = saut).

**Partiel / non fait**
- Type « directionnel » et « satin » absents du bloc multi-sélection (guides propres à chaque
  forme) ; les noms de commandes d'images (« Niveaux de gris »…) restent des noms de menu.
- Les rafales de X/Y/L/H du formulaire d'une forme ne fusionnent pas (un pas par validation).
- Un objet « figé » reste modifiable : si un vrai verrou d'édition est voulu, il faudra un
  champ dédié dans le document.
