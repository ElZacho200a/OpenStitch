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
