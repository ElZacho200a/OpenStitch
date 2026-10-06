# Audit de l'interface Qt — 2026-10-06

Public : mainteneur, contributeur, agents. Complète `docs/ui-redesign-{audit,plan,specification}.md`
(refonte antérieure : tokens, inspecteur, barres d'outils, état vide) et la section 20 de
`docs/roadmap-parite-hatch.md` (HP-UX-*). Ce document porte sur **le câblage et
l'ergonomie**, pas sur la complétude fonctionnelle.

## 1. Méthode et état de base

- Audit en lecture seule de `apps/desktop/` (≈13,5 k lignes ; `main_window.cpp` = 7031 lignes,
  331 Ko) par deux agents indépendants : **câblage** (actions, signaux/slots, raccourcis, docks, CMake)
  et **ergonomie/modernisation** (design system, forme, flux, retours, canevas, accessibilité).
- **Premier build du desktop dans l'environnement de développement** (Linux, Qt 6.4.2 d'apt,
  `build/linux-qt`, headless `QT_QPA_PLATFORM=offscreen`). Une seule erreur de compilation, propre à
  GCC/Linux (`uint64_t` → `QVariant` ambigu, `document_panel.cpp:178`, sans effet sur MSVC), corrigée.
- **Base de non-régression** : 8 suites QTest existantes au vert (dont `test_main_window` : 85 tests,
  y compris le dialogue Contours jamais compilé jusque-là) + nouvelle suite `test_ui_invariants`
  (6 tests, 3 défauts connus en échec attendu).
- Limite : Qt 6.4 ≠ Qt 6.8.3 de la CI Windows ; rendu, DPI et thème Windows restent à vérifier sur la
  CI et à la main. Les chiffres ci-dessous sont ceux des agents ; les trois défauts les plus graves
  (G en double, docks non réouvrables, Suppr limité aux régions) ont été revérifiés à la main.

## 2. Défauts de câblage (priorité de correction)

| Gravité | Constat | Preuve |
|---|---|---|
| **Haute** | Touche **G** liée deux fois (mode remodelage satin et outil polygone régulier) : raccourci ambigu, aucun ne se déclenche | `main_window.cpp:688`, `:5325` ; test `windowShortcutsAreUnique` (XFAIL) |
| **Haute** | Aucun dock n'a de `toggleViewAction()` dans un menu : un panneau fermé (Propriétés, Workflow, Ordre, Filtre, Analyse) ne se rouvre pas | test `everyDockHasAnObjectNameAndIsReopenableFromAMenu` (XFAIL) |
| **Haute** | `refreshDocumentPanel/OrderPanel/FilterPanel` et `runAnalysis` forcent `setVisible/show` : annulent « Masquer les panneaux » et une fermeture voulue | `main_window.cpp:5666, 5967, 6106, 5851` (agent) |
| Moyenne | Barre d'outils contextuelle : `clear()` ne détruit pas les actions/widgets → fuite cumulée à chaque reconstruction | `:5192` (agent) |
| Moyenne | « Éditer les points » ne désactive pas le mode rails (l'inverse le fait) : deux jeux de poignées | `:1042-1051` (agent) |
| Moyenne | **Suppr** ne supprime que les régions ; objets vectoriels/broderie seulement par menu contextuel | `:611`, `canvas_view.cpp:275` (agent, grep revérifié) |
| Moyenne | Raccourcis globaux Return/Entrée/Retour arrière/Échap : risque d'avaler la saisie dans l'inspecteur (non reproduit) | `:5386-5439` (agent) |
| Moyenne | Aide « Raccourcis » périmée (Ctrl+Y pour Rétablir ; une dizaine de touches absentes) | `:5070-5081` (agent) |
| Basse | Mnémoniques en double (File, Broderie) ; actions « document requis » toujours actives ; récents sans « Vider » ; entrée « Déboguer » visible ; ~90 actions sans `objectName` ; essai satin « expérimental » non signalé hors dialogue | agent ; test `menuMnemonicsAreUniquePerMenu` (XFAIL) |

**Propre** : aucune ancienne syntaxe `SIGNAL()/SLOT()` ; tous les signaux des panneaux sont émis et consommés ;
`Q_OBJECT`/AUTOMOC/CMake complets ; aperçus de scène correctement libérés ; tous les `objectName` cherchés par les tests existent.

## 3. Ergonomie et modernisation

Le design system existe (`design_tokens`, 2 thèmes × 2 densités, `app_theme` QSS+palette) et est
largement respecté (7 `setStyleSheet`, 2 couleurs hexadécimales en dur). **Le problème n'est pas
l'absence de tokens, c'est ce qui est bâti dessus.**

**Direction proposée** — « moins carré, plus guidé » :

1. **Forme** : rayons 6/10/14 px au lieu de 3/5 ; boutons secondaires *tonals* (fond, sans bordure) ;
   `QGroupBox` sans cadre (titre + séparateur) ; docks séparés par le fond, pas par des traits ; barres
   de défilement fines (8 px) ; élévation légère pour menus/infobulles ; **style Fusion forcé** + QSS
   complété (case à cocher, curseur, onglets, progression, séparateurs) pour un rendu identique partout.
2. **Icônes** : jeu **SVG** monochrome recoloré par thème (aujourd'hui 28 icônes peintes en 32×32 fixe,
   une seule teinte grise ≈ 2,9:1 en sombre, sans HiDPI) ; une icône pour *chaque* action de menu/barre.
3. **Retours** : **bandeau de notification non modal** (icône + action) à la place de la majorité des
   46 `QMessageBox` (garder le modal pour les confirmations destructives) ; barre d'état segmentée
   (zoom, sélection, points, unités) ; libellé d'annulation (« Annuler : Générer le remplissage »).
4. **Flux** : le panneau Workflow devient un **stepper actionnable** (le clic lance l'étape, un bouton
   principal « Suivant : Segmenter ») ; écran d'accueil à cartes (« Image → broderie », « SVG », « Projet »,
   récents) ; menus regroupés (Objet / Génération / Satin) ; **palette de commandes** (Ctrl+K) ;
   Préférences générales (thème, densité) hors du menu Affichage.
5. **Canevas** : glisser dans le vide = rectangle de sélection ; panoramique Espace+glisser / clic
   milieu ; molette ancrée au curseur avec gestion trackpad ; surbrillance au survol, curseurs par
   poignée, infobulle live de dimension/angle ; couleurs de rails/accroche via tokens.
6. **Accessibilité** : `accessibleName` sur docks/barres/canevas, ordre de tabulation, anneau de
   focus 2 px, états par icône et pas seulement par couleur, cibles ≥ 24 px, suivi du thème système.
7. **Opérations longues** : segmentation, vectorisation, auto-numérisation et génération sont
   synchrones (curseur d'attente, zéro barre de progression) → tâches asynchrones avec progression
   et annulation (HP-PERF-001/UX-006, **P0** de la feuille de route).

Structurel (à répartir, pas à faire d'un bloc) : découper `main_window.cpp` (registre d'actions,
dialogues dédiés, scène) ; moteur de tâches ; système de notifications ; bibliothèque d'icônes ;
parcours guidé.

## 4. Stratégie de non-régression

Conventions du projet (`CLAUDE.md`) : QTest headless, aucune comparaison de pixels, aucun `sleep`.

| Couche | Contenu | État |
|---|---|---|
| **Existant** | 8 suites QTest (canevas, nœuds, panneaux, inspecteur, fenêtre principale, WSL, options, worker SAM) | vert (Linux/Qt 6.4) |
| **Invariants** (`test_ui_invariants`) | raccourcis uniques ; docks nommés et réouvrables ; mnémoniques uniques ; chaque défaut connu en `QEXPECT_FAIL(Continue)` → échoue en « XPASS » dès qu'il est corrigé | créé (6 tests) |
| **À ajouter avant de toucher au code** (lot 0) | *tests de caractérisation* du comportement actuel : matrice d'activation des actions selon l'état (vide / image / segmentée / objet sélectionné) ; Suppr par type d'objet ; exclusivité des modes d'édition ; visibilité des docks après rafraîchissement ; absence de fuite de widgets de la barre contextuelle (compte d'enfants stable) | à faire |
| **À ajouter avec chaque lot** | un test par comportement modifié, écrit *avant* le correctif | convention |
| **Hors périmètre** | rendu/DPI/thème Windows, vrai bureau : CI `windows-msvc` + essai manuel | CI existante |

## 5. Plan multi-agents (architecte → relecteur → codeur → relecteur → testeur)

**Rôles** (chacun un agent distinct ; l'auteur ne se relit jamais) :

| Rôle | Livrable | Interdit |
|---|---|---|
| **Architecte** | spécification courte par lot : composants, interfaces, fichiers touchés, critères d'acceptation vérifiables, tests à écrire | écrire du code de production |
| **Relecteur de conception** | verdict sur la spec : conformité aux invariants (Qt confiné à `apps/desktop`, mutation par `ICommand`, aucune logique métier dans l'UI), cohérence avec les tokens, risques, découpage | modifier la spec sans la renvoyer à l'architecte |
| **Codeur** | implémentation minimale conforme à la spec, un lot = un commit | refactor hors spec, toucher un fichier hors propriété |
| **Relecteur de code** | revue du diff : bugs, fuites, durée de vie des connexions, duplication, respect des conventions (clang-format, noms ASCII des tests) | modifier le code (il demande des corrections) |
| **Testeur** | écrit/étend les tests *adversariaux* du lot, exécute build + suite complète, retire les `QEXPECT_FAIL` devenus faux, rapporte les chiffres | modifier le code de production |

**Porte de sortie d'un lot** : spec approuvée → code relu → tests verts sur `build/linux-qt` (suite complète,
pas seulement le lot) → clang-format → rapport chiffré → commit. Le **Lead** (moi) tranche les désaccords,
tient l'état synthétique et fusionne ; la CI Windows reste l'arbitre final avant de déclarer un lot fini.

**Propriété des fichiers** (c'est le vrai risque : `main_window.cpp` est le carrefour) : un seul codeur écrit
dans `main_window.cpp` à la fois ; les nouveaux composants vont dans des fichiers neufs
(`notification_banner.*`, `command_palette.*`, `task_runner.*`, `icons/*.svg`) ; un lot qui a besoin de
`main_window.cpp` est sérialisé derrière le précédent.

**Lots, par vagues** (effort S/M/L ; ordre = dépendances) :

| Vague | Lot | Contenu | Effort |
|---|---|---|---|
| 0 | **L0 — Filet de sécurité** | tests de caractérisation de la §4 ; base verte documentée | M |
| 1 | **L1 — Corrections de câblage** | G en double ; `toggleViewAction` + sous-menu Panneaux ; rafraîchissements qui réaffichent ; fuite de la barre contextuelle ; exclusivité des modes ; Suppr contextuel ; aide des raccourcis générée depuis les actions ; mnémoniques ; « Vider les récents » ; « Déboguer » derrière un indicateur | M |
| 2 | **L2 — Design system v2** | tokens (rayons, typographie, échelle d'espacement, élévation) ; QSS plat + Fusion ; recoloration des icônes par thème ; suppression des 2 hex et 13 `QColor` du canevas | M |
| 2 | **L3 — Notifications et retours** | `NotificationBanner` ; migration progressive des `QMessageBox` non destructifs ; libellés d'annulation ; barre d'état segmentée | M |
| 3 | **L4 — Icônes SVG** | jeu thémable, HiDPI, icône pour toute action | M |
| 3 | **L5 — Canevas ergonomique** | sélection rectangle, pan Espace/clic milieu, trackpad, survol, curseurs, infobulle live | M |
| 4 | **L6 — Parcours guidé** | stepper actionnable, écran d'accueil, regroupement des menus, palette de commandes, Préférences | L |
| 4 | **L7 — Accessibilité** | noms accessibles, ordre de tabulation, focus, états sans couleur seule | S |
| 5 | **L8 — Tâches asynchrones** | `TaskRunner` + progression + annulation sur segmentation/vectorisation/digitize/génération (HP-PERF-001, UX-006) | L |
| 5 | **L9 — Découpage de `main_window.cpp`** | extraction du registre d'actions et des dialogues ; **fait au fil des lots précédents**, pas en bloc | L |

**Risques** : régressions silencieuses de raccourcis (→ test d'unicité) ; conflits de fusion sur
`main_window.cpp` (→ propriété exclusive, petits commits) ; écarts Qt 6.4 / 6.8 (→ CI Windows) ; perte
de la sensation « native » en forçant Fusion (→ décision à valider avec l'utilisateur) ; asynchrone et
`Project` non thread-safe (→ fonctions pures sur instantanés, cf. `CLAUDE.md`).

**Décisions à valider avant la vague 2** : style Fusion forcé ; bibliothèque d'icônes SVG (licence
compatible Apache-2.0 requise, ex. Lucide en ISC / Tabler en MIT) ; densité par défaut ; ordre de L8
(le plus gros gain d'ergonomie, mais le plus risqué).
