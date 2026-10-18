# Audit UX/UI d'OpenStitch Studio — 2026-10-18

*État : audit **statique** (code lu) complété par **une mesure** hors écran de la fenêtre
principale. L'interface n'a pas été pilotée interactivement. Aucun code applicatif n'est modifié ;
les corrections sont planifiées dans la roadmap (`docs/roadmap-parite-hatch.md` §20 et lots A à E
de [`strategie-interface.md`](strategie-interface.md)).*

Référence : `main` à `c4dba99` (branche `claude/openstitch-ux-audit-d65b5d`). Ce rapport
**complète** [l'audit du 2026-10-13](../ui-ux-audit-2026-10-13.md) (registre `UX-02…UX-46`) et
[son lot de correctifs](../ui-ux-correctifs-2026-10-13.md) ; il ne les remplace pas.

## 1. Méthode, preuves et limites

### Niveaux de preuve

| Classe | Sens |
|---|---|
| **A — constaté** | chemin exécuté **lu dans le code actuel**, ou mesure/capture réalisée pendant cet audit. |
| **B — risque probable** | déduit de l'implémentation, non reproduit. Les tailles minimales de dialogues sont dans ce cas. |
| **C — proposition** | amélioration ergonomique sans dysfonctionnement existant. |

Un constat de classe A signifie « le code fait cela », pas « un utilisateur l'a observé » : aucune
session d'usage n'a eu lieu.

### Ce qui a été fait

- Quatre analyses indépendantes en lecture seule (sous-agents), puis **confrontées** : parcours
  utilisateur (`PAR-`), structure Qt et responsive (`QT-`), ergonomie broderie et canevas
  (`BRO-`), QA et cartographie de la roadmap.
- Vérification personnelle dans le code des affirmations dont dépendent des recommandations :
  Ctrl+O (`main_window.cpp:699-701`), Échap (`main_window.cpp:6481-6489`), sémantique de
  `locked` (`embroidery_object.hpp:393`), garde du rendu réaliste (`main_window_realistic.cpp:140`,
  `simStep_ = -1` seulement à `main_window.cpp:1229`, `7790`, `7796`), hit-test
  (`selection_hit_test.cpp:94-100`).
- **Mesure** avec `openstitch-ui-shots` (hors écran, projet `tentabrode.png` numérisé, 158 objets) :

| Mesure | Résultat |
|---|---|
| `minimumSizeHint` de la fenêtre, docks par défaut | **523 × 441** |
| Fenêtre 1024 × 640 : canevas | **360 × 421** |
| Fenêtre 1600 × 1000 : canevas | 936 × 792 |
| Palette d'outils à 640 px de haut | déborde dans un menu `»` (visible sur la capture) |
| Dock Document / Propriétés (minimum) | 127 × 143 / 68 × 84 |

### Ce qui n'a PAS été fait

- Pas de pilotage interactif, pas de NVDA/UIA, pas d'étude utilisateurs.
- **Pas de mesure à 1366 × 768 en 125/150/200 %** : les budgets d'écran ci-dessous sont calculés,
  pas mesurés. Les captures hors écran utilisent une police de repli étroite : les largeurs de
  texte ne sont pas représentatives.
- Les `minimumSizeHint` des dialogues (§4) sont des **estimations** (classe B) ; la première
  tâche de validation est de les mesurer (`HP-QA-008`).
- Les numéros de ligne citent `main` à `c4dba99` et **dérivent vite** : relire avant d'agir.

### Budget d'écran calculé (barre des tâches déduite)

| Écran | Zone logique utile | Contenu d'un dialogue (titre déduit) |
|---|---|---|
| 1366 × 768 à 125 % | ≈ 1093 × 576 | ≈ 545 px de haut |
| 1366 × 768 à 150 % | ≈ 911 × 480 | ≈ 449 px de haut |
| 1366 × 768 à 200 % | ≈ 683 × 360 | ≈ 329 px de haut |
| 1920 × 1080 à 150 % | ≈ 1280 × 680 | ≈ 649 px de haut |

## 2. État réel des constats de l'audit du 2026-10-13

Vérifié dans le code actuel (le blocage de compilation C3493 est **corrigé** :
`kFlattenTolerance` est un `static constexpr`, `main_window.cpp:5526`). Les lignes citées par
l'ancien audit ont dérivé de +20 à +800.

| Statut | Constats |
|---|---|
| **Corrigés** | UX-02 (points sans raster), UX-06 (type actif), UX-08 (fichier machine seul), UX-35/36 (masques IA), UX-37 (sélection + survol), UX-38 (grille), UX-39/41 (poignées, focus), UX-40 (Espace) |
| **Partiels** | UX-04 (calculs synchrones : l'analyse, la densité, le rendu PNG et le mélange de couleurs passent par `BusyIndicator::run` ; la génération et l'autosave restent sur le fil UI), UX-07 (champ hauteur grisé honnêtement, mais `placement()` n'applique que la largeur), UX-10 (rapport partagé ; Entrée poursuit encore l'export), UX-29 (vitesse faite, navigation par couleur absente) |
| **Ouverts** | UX-13, 14, 15, 16, 26, 30, 32, 33, 34, 43, 45, 46 ; UX-17, 25, 28, 31, 42, 44 déduits, non revérifiés |

Aucun `UX-xx` n'était cité dans la roadmap : les entrées ci-dessous les rattachent enfin à des
tâches (`docs/roadmap-parite-hatch.md` §20).

## 3. Inventaire des constats

Priorités : **P0** perte de travail/blocage ; **P1** friction majeure et fréquente ; **P2** gain
important ; **P3** finition. « Tâche » renvoie à l'entrée de la roadmap qui porte la correction
(les critères d'acceptation y figurent). Les identifiants `PAR-`, `QT-`, `BRO-` sont propres à
ce rapport. **Aucun constat P0 nouveau** : les deux P0 de l'audit du 13/10 sont corrigés
(UX-02) ou partiels (UX-04, porté par `HP-PERF-001`).

### 3.1 Parcours (`PAR-`)

| ID | Cl. | Prio | Constat et conséquence | Preuve | Correction proposée (alternative) | Tâche |
|---|---|---|---|---|---|---|
| PAR-01 | A | P1 | **Ctrl+O n'ouvre que des images** ; un `.osp` tapé là donne « Image illisible ». `openPath` sait déjà router `.osp/.dst/.svg/image`. | `main_window.cpp:699-701`, `main_window_ux.cpp:83-89` | Ctrl+O = « Ouvrir… » universel ; « Importer une image… » garde un autre raccourci. | HP-UX-017 |
| PAR-02 | A | P1 | Une forme **sans couture** n'a aucun choix de type dans l'inspecteur ; 2 formes ou plus ont un combo + « Appliquer » : incohérent. | `properties_panel.cpp:452-550, 597-634`, `main_window.cpp:7139` | Bloc « Type de points » (4 boutons) identique pour 1 ou N formes. | HP-UX-028 |
| PAR-03 | A | P1 | Trois chemins différents pour « Satin » (dialogue, immédiat, rien) ; « Directionnel » absent pour une forme neuve (4 clics et plus). | `main_window.cpp:6232-6246, 6289-6301, 4228-4254` | Mêmes 4 boutons pour tout état de sélection, création immédiate. | HP-UX-002, HP-UX-028 |
| PAR-04 | A | P2 | Le **menu contextuel** d'une forme non cousue ne propose pas « Type de points ». | `main_window.cpp:5317-5328, 5374-5386` | Réutiliser `setStitchTypeForSelection`. | HP-UX-028 |
| PAR-05 | A | P3 | Échec du satin traité différemment selon l'entrée (repli tatami proposé ou simple avertissement) ; deux entrées de menu pour une intention. | `main_window_satin_auto.cpp:603-615`, `main_window.cpp:965-967, 5221-5223` | Bouton « Utiliser un tatami » dans le message ; une seule entrée. | HP-UX-016 |
| PAR-06 | A | P2 | **Numérisation automatique : deux modales puis ~25 réglages d'expert**, aucun préréglage, résumé final limité au nombre d'objets. | `main_window.cpp:4320-4342, 4369-4768, 4929-4937` | Préréglages + « Avancé » replié + question fusionnée + résumé chiffré. | HP-UX-030 |
| PAR-07 | A | P2 | Chaque F7 ouvre une modale « Détail » pour un seul curseur, valeur déjà mémorisée. | `main_window_workflow.cpp:175-206` | F7 applique la dernière valeur ; Maj+F7 ouvre le réglage. | HP-UX-021 |
| PAR-08 | A | P2 | L'import signale un dépassement du cadre mais **ne permet pas de le changer** : annuler, menu, réimporter. | `import_dialog.cpp:211-218`, `main_window.cpp:1095, 6347` | Sélecteur de cadre dans l'import. | HP-UX-033 |
| PAR-09 | A | P2 | Pas de parcours guidé au premier lancement ; l'accueil n'offre ni « Dessiner » ni « Texte ». | `empty_state_widget.cpp:49-51, 78`, `main_window.cpp:5915` | Liens « Guide de 2 minutes », « Dessiner », « Texte ». | HP-UX-012, UX-43 |
| PAR-10 | A | P1 | **Après une simulation, le rendu réaliste ne s'affiche plus** tant que le document n'est pas régénéré ; aucun bouton de sortie. | `main_window_realistic.cpp:140`, `main_window.hpp:1395`, `main_window.cpp:1229, 7790, 7796` | Bouton « Quitter la simulation » + sortie en fin de lecture. | HP-UX-022 |
| PAR-11 | A | P2 | Barre de simulation pauvre : compteur = indice de commande, pas de saut par couleur, pas de durée. | `main_window.cpp:7699-7735, 7807` | « point n/N · mm:ss · fil 3/7 », couleur précédente/suivante. | HP-UX-036 |
| PAR-12 | A | P2 | **Export en 4 écrans**, décision implicite, « Voir les problèmes » ferme le dialogue et n'apparaît qu'en cas d'erreur, aucun enregistrement `.osp` proposé ; l'analyse est recalculée à chaque case cochée. | `machine_export_dialog.cpp:63-187, 318`, `main_window.cpp:8316, 8378, 8422-8443` | Export rapide, « Enregistrer le projet d'abord », aperçu différé. | HP-UX-032 |
| PAR-13 | A | P2 | Qualité dispersée : dock Analyse masqué, statistiques en `QMessageBox`, trois dialogues texte séparés, corrections une à une. | `main_window_analysis.cpp:177, 211-311, 338-393`, `main_window.cpp:7892` | Panneau « Qualité » unique, « Corriger tout ce qui est sûr » annulable. | HP-UX-031 |
| PAR-14 | A | P2 | Deux listes d'objets concurrentes (Document / Ordre de couture), réordonnancement par boutons, un cran à la fois. | `main_window.cpp:6745-6747, 7224-7295`, `document_panel.cpp:237` | Fusion dans Document, glisser-déposer. | HP-UX-037 |
| PAR-15 | A | P2 | Couleur de départ imposée (80,120,200) à toute forme dessinée ou importée. | `main_window.cpp:2975, 1449, 8593` | Couleur courante mémorisée, pipette au tracé. | HP-OBJ-018 |
| PAR-16 | B | P3 | La réduction de couleurs existe à 5 endroits sans guide. | `main_window_workflow.cpp:426`, `segmentation_dialog.cpp:65`, `main_window.cpp:843, 4415` | Une entrée « Palette » unique avec aperçu. | stratégie §6 (non planifié) |
| PAR-17 | A | P1 | Inspecteur plat : tatami ~13 lignes, directionnel ~25, satin ~20, sans « Avancé ». | `properties_panel.cpp:916-998, 1016-1237, 1312-1338` | Essentiels en tête, sections repliables mémorisées. | HP-UX-003 |
| PAR-18 | A | P3 | Coupes réglées à deux endroits (Options de génération et export). | `generation_options_dialog.cpp:43-60`, `machine_export_dialog.cpp:63, 129` | Vocabulaire commun et renvois. | HP-UX-032 |
| PAR-19 | B | P3 | 6 des 8 opérations d'image s'appliquent au clic sans aperçu (seules Luminosité/Contraste et Quantifier en ont un). | `main_window.cpp:798-810, 1468-1488` | Aperçu direct pour toutes. | HP-VIEW-012 |
| PAR-20 | A | P3 | Le cadre n'est accessible que par menu ou barre contextuelle sans sélection. | `main_window.cpp:6347` | Taille de cadre cliquable en barre d'état. | HP-UX-033 |

### 3.2 Ergonomie broderie et canevas (`BRO-`)

| ID | Cl. | Prio | Constat et conséquence | Preuve | Correction proposée (alternative) | Tâche |
|---|---|---|---|---|---|---|
| BRO-01 | A | P2 | Objets superposés : Alt+clic ouvre un **menu** (pas de cyclage) ; entrées sans pastille ni type. | `interaction_map.cpp:90`, `main_window.cpp:8980-9023` | Re-clic au même point = objet suivant + « 2/4 » ; menu enrichi. | HP-UX-020 |
| BRO-02 | A | P1 | **Trois règles de sélectionnabilité** pour le même geste : le clic simple tient compte du filtre, Maj/Ctrl/Alt non, le rectangle oui. Un Alt+clic peut ajouter un objet invisible. | `main_window.cpp:8723-8727, 8879, 8918-8926, 8984`, `selection_hit_test.cpp:94-100` | Un prédicat unique `selectableAt(...)`. | HP-UX-019 |
| BRO-03 | A | P1 | **Aucune tolérance de clic** : un tracé ouvert (aire nulle) ou un détail de 1 mm est presque impossible à cliquer (`contains` exact). Seul le rectangle croisant utilise 0,4 mm. | `selection_hit_test.cpp:16, 71-73, 97`, `main_window.cpp:5029, 8725` | Distance ≤ max(0,4 mm, 4 px) ; l'objet contenant le point reste prioritaire. | HP-UX-019 |
| BRO-04 | A | P1 | **Échap ne désélectionne que les régions**, jamais les objets ; il ferme tous les modes d'un coup. | `main_window.cpp:6479-6509` | Échap remonte d'un niveau (mode, outil, objets, régions). | HP-UX-018 |
| BRO-05 | A | P2 | Ctrl+A sélectionne des régions ; les objets sont sur Ctrl+Maj+A. Muet sans segmentation. | `main_window.cpp:895-898`, `main_window_objects.cpp:88-90` | Ctrl+A suit la couche active. | HP-UX-023 |
| BRO-06 | A | P2 | Liste Document : **une seule ligne sélectionnable** pour les objets, vecteurs non cousus absents (UX-13). | `document_panel.cpp:237, 276, 370-402` | Sélection étendue, menu de ligne, lignes vectorielles. | HP-UX-037 |
| BRO-07 | A | — | **Navigation conforme** (molette, Espace, clic milieu, pinch, rectangle en Sélection). À préserver : ne pas régresser. | `interaction_map.cpp:65-80` | — | — |
| BRO-08 | B | P2 | Pas de zoom sur la sélection ni de retour au zoom précédent. | `main_window.cpp:1078-1091` | Z, Maj+Z, Ctrl+1. | HP-UX-023 |
| BRO-09 | A | P2 | **4 modes d'édition + 15 outils** à activer par menu ; pas d'entrée par double-clic (S10 « planifié »). | `main_window.cpp:996-1054, 6355-6434, 6479-6507` | Un mode « Remodeler » contextuel au type, double-clic pour entrer, indication permanente du mode. | HP-UX-001 |
| BRO-10 | A | P3 | Nom du mode actif non affiché dans la barre d'état ; barre d'indices coupée. | `main_window.cpp:6075` | Nom du mode en gras. | HP-UX-001 |
| BRO-11 | A | P1 | **Pas de bouton « défaut »** par champ ni de défauts mémorisés ; valeurs de création en dur. | `main_window.cpp:4232-4244` | ↺ par champ + « Utiliser comme défaut ». | HP-UX-003, HP-UX-035 |
| BRO-12 | A | P1 | Inspecteur tatami plat, 13 champs dont la sous-couche toujours déroulée. | `properties_panel.cpp:961-976` | Sections repliables, mémorisées (`ui_memory`). | HP-UX-003 |
| BRO-13 | A | P3 | Info-bulles partielles, unités parfois dans le libellé. | `properties_panel.cpp:901-958` | Table unique (nom, plage, défaut, unité). | HP-UX-042 |
| BRO-14 | A | P3 | Le type de point se **lit** mais ne se **voit** pas (préfixe texte). | `document_panel.cpp:209-217`, `properties_panel.cpp:801` | Icône de type, filtre par type. | HP-UX-042 |
| BRO-15 | A | P2 | Le « verrou » (`locked`) **fige l'ordre de couture**, il ne protège pas l'objet contre un déplacement. | `embroidery_object.hpp:393`, `selection_hit_test.cpp:97` | Mode « Isoler » de session (décision : **pas de champ `.osp`**). | HP-UX-034 |
| BRO-16 | A | P3 | « Masqué » signifie « non cousu » (info-bulle), ambigu pour qui veut seulement masquer à l'écran. | `document_panel.cpp:305-307` | Distinguer masquer / exclure de la couture. | HP-VIEW-003 |
| BRO-17 | A | P1 | **Valeurs mixtes masquées** en multi-sélection ; deux modèles (« Appliquer à N » et champs en direct) dans le même panneau. | `properties_panel.cpp:597-643` | « — » par champ, un seul modèle, commande composite. | HP-UX-038 |
| BRO-18 | A | P1 | Chaque pas d'un paramètre régénère **sur le fil UI** (la fusion d'annulation ne résout pas le gel). | `main_window.cpp:6931-6948` | Différé 150 ms + `BusyIndicator`. | HP-PERF-009 |
| BRO-19 | A | P1 | Dialogues de création **modaux, sans mémoire** : 3 champs en dur, `exec()`, puis création. | `main_window.cpp:4161, 4221-4260` | Création immédiate au dernier réglage ; « Créer avec réglages… » en option. | HP-UX-002 |
| BRO-20 | A | P1 | **Une modification de paramètre invalide les retouches manuelles sans avertissement préalable** (signalé après coup par ⚠). Les retouches ne sont pas perdues (ADR-014) mais ne sont plus appliquées. | `main_window.cpp:6931-6948`, `document_panel.cpp:43-56`, `properties_panel.cpp:233-240` | Bandeau « désactive N retouches (Ctrl+Z) ». | HP-UX-029 |
| BRO-21 | A | P2 | Réglages mémorisés limités à l'import/segmentation/numérisation ; pas de copier/coller le style. | `ui_memory.hpp` | Copier/coller le style, défauts de points. | HP-UX-035 |
| BRO-22 | A | P2 | Pas de raccourci pour les couches ni pour Document/Fils ; Alt+flèches partage Alt avec Alt+clic. | `main_window.cpp:838` | 1 à 4 pour les couches. | HP-UX-023 |
| BRO-23 | A | P2 | Quatre couches dans Affichage ▸ Calques, une seule en barre ; couche masquée = clic sans effet ni explication. | `main_window.cpp:1060-1075, 6114, 8721, 8878, 8981` | Bascule de vue à 4 états + message d'état. | HP-UX-036 |
| BRO-24 | A | P2 | Cinq barres/bandeaux permanents + 5 docks, y compris pour un usage rare. | `main_window.cpp:6081, 6126, 6355, 7695` | Barres à la demande. | HP-UX-026 |
| BRO-25 | A | P3 | Libellés techniques visibles (« expérimental », « Remodelage satin (avancé) ») ; le menu Déboguer est déjà masqué. | `main_window.cpp:967, 1027, 5363-5366` | Menu « Avancé ». | HP-UX-016 |

### 3.3 Structure Qt et responsive (`QT-`)

Toutes les tailles sont des **estimations de classe B** (sauf mention). Hypothèse : ligne de
formulaire ≈ 34 px logiques en densité confortable, ≈ 28 en compacte.

| ID | Cl. | Prio | Constat et conséquence | Preuve | Correction proposée (alternative) | Tâche |
|---|---|---|---|---|---|---|
| QT-01 | A/B | P1 | **Quatre rangées de barres + bande des fils** ≈ 255 px fixes. **Mesuré** : à 1024 × 640 le canevas ne fait que 360 × 421 ; estimé ≈ 225 px de haut à 150 % maximisé. | `main_window.cpp:470-476`, `main_window_threads.cpp:541-548`, mesure §1 | Fusionner simulation + fils en une rangée ; barre contextuelle dans la principale. | HP-UX-026 |
| QT-02 | A | P1 | **Palette d'outils trop haute** (≈ 17 actions) : déborde dans `»` (visible sur la capture à 640 px), dont « Terminer/Annuler la forme ». | `main_window.cpp:6355-6477` | Deux colonnes sous un seuil ; Terminer/Annuler dans la barre contextuelle pendant un tracé. | HP-UX-026 |
| QT-03 | B | P1 | **Dock Fils sans zone défilante** (≈ 450 px minimum), contrairement à Propriétés/Filtres ; sa visibilité est mémorisée. | `thread_panel.cpp:253-324`, `main_window_threads.cpp:94-104` | `QScrollArea` ; idem `SegmentationColorsPanel`. | HP-UX-024 |
| QT-04 | B | P1 | **Docks Analyse, Historique, Fils, Couleurs empilés** à droite au lieu d'être en onglets. | `main_window_analysis.cpp:176`, `main_window_editing.cpp:637`, `main_window_threads.cpp:102`, `main_window_colors.cpp:25` | `tabifyDockWidget`. | HP-UX-024 |
| QT-05 | B | P2 | Deux colonnes ouvertes à 911 px : canevas ≈ 250 px ; aucun `QSplitter` dans l'application. | `main_window.cpp:499` | Mode « portable » : une colonne. | HP-UX-027 |
| QT-06 | B | P2 | Largeur minimale de l'inspecteur dictée par ses libellés (pas de `WrapLongRows`). | `properties_panel.cpp:163-170` | `WrapLongRows`. | HP-UX-027 |
| QT-07 | A | P1 | **Dialogues sans zone défilante ni borne d'écran** ; tailles initiales jusqu'à 1000 × 720. | `production_dialog.cpp:56`, `ai_segmentation_dialog.cpp:135`, `main_window.cpp:5870`, `text_dialog.cpp:165`, `main_window_analysis.cpp:363` | `fitDialogToScreen` au `showEvent`. | HP-UX-025 |
| QT-08 | B | P1 | **Texte/Lettrage ≈ 750-800 px de haut** minimum (17 lignes + aperçu). | `text_dialog.cpp:285-357` | Colonne gauche défilante, ou onglets Texte/Avancé. | HP-UX-025 |
| QT-09 | B | P1 | **Export machine ≈ 690 px de haut** (un seul formulaire de ~17 lignes). | `machine_export_dialog.cpp:43-181` | « Optimisations » repliable, boutons fixes. | HP-UX-025 |
| QT-10 | A/B | P1 | **Mélange de couleurs : `setMinimumWidth(970)`**, supérieur à 911 px. | `color_blend_dialog.cpp:195` | Retirer, empiler sous un seuil. | HP-UX-025 |
| QT-11 | A/B | P1 | **Segmentation IA ≈ 900 px de haut** minimum (deux aperçus empilés), le plus grand dialogue. | `ai_segmentation_dialog.cpp:135, 215, 221` | `QSplitter` ou onglets. | HP-UX-025 |
| QT-12 | A | P2 | Accueil : hauteur ≈ 440 px, sans zone défilante (= UX-43). | `empty_state_widget.cpp:27-86` | `QScrollArea`, moins de récents si peu de place. | HP-UX-025 |
| QT-13 | A | P3 | Pastilles `QPixmap` non adaptées au DPI ; règles et polices fixes (= UX-28). | `import_dialog.cpp:38`, `text_dialog.cpp:356`, `ruler.cpp:37, 90`, `app_theme.cpp:352` | Fabrique `makeSwatch`, métriques dérivées de la police. | HP-UX-043 |
| QT-14 | A | P1 | **Densité non liée à l'écran** : « Confortable » par défaut même sur 768 px. | `app_theme.cpp:434-444` | Compacte au premier lancement si la hauteur < 800. | HP-UX-026 |
| QT-15 | A/B | P2 | `colorsDock_` créé paresseusement : place perdue au redémarrage ; un dock Fils ouvert mémorisé peut démarrer hors écran. | `main_window_colors.cpp:19-25`, `main_window_ux.cpp:201-231` | Créer caché au constructeur ; borner à l'écran. | HP-UX-024 |
| QT-16 | A | P3 | Barre contextuelle : espaces de fin dans les libellés, étiquette de stats de ~400 px. | `main_window.cpp:6234-6344` | Retirer ; stats en barre d'état. | HP-UX-026 |
| QT-17 | A | P3 | Barre d'état : ≈ 450 px de largeur fixe (curseur `setMinimumWidth(180)`). | `main_window.cpp:507-556` | Texte élidé, minimum 0. | HP-UX-026 |

### 3.4 Constats hérités encore ouverts, rattachés à une tâche

| UX | Sujet | Tâche |
|---|---|---|
| UX-04 | calculs synchrones (génération, autosave, rendu réaliste, production) | HP-PERF-001, HP-PERF-009 |
| UX-07 | import non proportionnel (`placement()` isotrope) | HP-UX-044 |
| UX-10 | Entrée poursuit l'export malgré des risques | HP-UX-032 |
| UX-13, 45 | vecteurs absents de Document ; repli perdu au rafraîchissement | HP-UX-037 |
| UX-14, 26, 32 | workflow = lanceur ; erreurs éphémères ; heure de sauvegarde | HP-UX-039 |
| UX-15, 16 | repli couleur IA, annulation tardive | HP-UX-040 (dépend de HP-PERF-001) |
| UX-33, 34 | valeurs mixtes, portées de groupe | HP-UX-038 |
| UX-46 | barre Fils sans clavier | HP-UX-041 |

## 4. Dialogues : tailles estimées contre le budget 1366 × 768 à 150 %

Contenu disponible ≈ 911 × 449. Classe B (estimations) ; `HP-QA-008` les mesure.

| Dialogue | Minimum estimé | Tient ? |
|---|---|---|
| Import image | ≈ 340 × 430 | oui, juste |
| Segmentation, Export image, Préférences | ≤ 480 × 380 | oui |
| Options de génération, Rendu réaliste, Préférences IA | ≈ 460-480 × 480-520 | limite |
| Export machine | ≈ 520 × 690 | non |
| Texte / Lettrage | ≈ 700 × 780 | non |
| Mélange de couleurs | 970 × ≈ 630 | non (largeur : mesurable) |
| Segmentation IA | ≈ 700 × 900 | non |
| Fiche de production, Aide Gestes, Guide, Analyse | minimum correct, **taille initiale** 720-1000 × 520-720 | initial non borné |
| Dock Fils | ≈ 300 × 475 | non |

## 5. Couverture de tests : trous

Constat A (absence vérifiée par recherche) dans `tests/unit/desktop/` (37 fichiers, ~1000 fonctions) :

1. aucun plafond de `minimumSizeHint` sur les dialogues ni sur la fenêtre tous docks ouverts ;
2. aucune assertion directe sur `isClean`/`markClean` ni sur l'heure d'enregistrement (UX-32) ;
3. aucun test du bouton par défaut des dialogues (UX-10) ;
4. aucun test de `ThreadStrip` (clavier, débordement `+N`) ;
5. ordre de tabulation testé uniquement canevas ↔ un bouton ;
6. `DocumentPanel` : ni conservation du repli/défilement, ni inventaire des vecteurs seuls ;
7. DPI et police agrandie : documentés, non automatisés ;
8. workflow : pas de test « état lié à la révision du document » ;
9. groupes : pas de test d'état mixte par champ ni de portée par contrôle ;
10. aucun test de chaîne `tr` non traduite (prérequis `HP-I18N-002`).

## 6. Dépendances et risques de régression

- **`HP-QA-001` (découper `main_window.cpp`, 9595 lignes + 22 fichiers `main_window_*.cpp`)** :
  les lots qui touchent le même fichier (HP-UX-026, 028, 029, 036, 039) gagnent à venir après, ou
  à se limiter à `main_window_workflow.cpp` / `main_window_ux.cpp`, déjà séparés.
- **`HP-PERF-001`** : HP-PERF-009, HP-UX-040 et la fin d'UX-04. Ne pas coder de threads ad hoc.
- **`app_theme.cpp`, `design_tokens.*`, `main_window.cpp`, `CMakeLists.txt`** : fichiers de
  convergence, un seul propriétaire par lot.
- Parallélisables (fichiers disjoints) : thread_strip, empty_state_widget, text_dialog,
  machine_export_dialog, color_blend_dialog, ai_segmentation_dialog, import_dialog.
- **Risques** : undo/redo (toute édition de groupe = **une** `ICommand`), retouches manuelles
  (`apply_manual_overrides`, passer par `effective_sequence`), QSS global (test de contrat
  clair/sombre), anciens tests qui figent un défaut (ex. Entrée = export), compatibilité `.osp`
  (**aucune tâche n'ajoute de champ persistant**).

## 7. Ce que cet audit ne remet pas en cause

Navigation (molette, Espace, pinch, rectangle de sélection), segmentation non modale avec aperçu
hors fil, glisser-déposer routé par extension, récupération après arrêt anormal, actions
partagées et raisons de grisage, état « modifié » via `undoStack_`. Voir aussi
[`parcours-utilisateur.md`](parcours-utilisateur.md), [`strategie-interface.md`](strategie-interface.md)
et [`synthese.md`](synthese.md).
