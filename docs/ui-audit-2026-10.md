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

1. **Forme** : **décision (2026-10-06) : OpenStitch Studio a sa propre identité visuelle, pas de rendu
   natif Windows.** On force le style Qt « Fusion » (sans rapport avec Autodesk Fusion 360 : c'est
   seulement le moteur de style neutre de Qt, qui sert de base) et on habille *tout* via les tokens et
   la QSS : rayons 6/10/14 px au lieu de 3/5 ; boutons secondaires *tonals* (fond, sans bordure) ;
   `QGroupBox` sans cadre (titre + séparateur) ; docks séparés par le fond, pas par des traits ; barres
   de défilement fines (8 px) ; élévation légère pour menus/infobulles ; QSS complétée (case à cocher,
   curseur, onglets, progression, séparateurs). Rendu identique sur toutes les machines.
2. **Icônes** : jeu **SVG** monochrome recoloré par thème (aujourd'hui 28 icônes peintes en 32×32 fixe,
   une seule teinte grise ≈ 2,9:1 en sombre, sans HiDPI) ; une icône pour *chaque* action de menu/barre.
3. **Retours** : **bandeau de notification non modal** (icône + action) à la place de la majorité des
   46 `QMessageBox` (garder le modal pour les confirmations destructives) ; barre d'état segmentée
   (zoom, sélection, points, unités) ; libellé d'annulation (« Annuler : Générer le remplissage »).
4. **Flux** : le panneau Workflow devient un **stepper actionnable** (le clic lance l'étape, un bouton
   principal « Suivant : Segmenter ») ; écran d'accueil à cartes (« Image → broderie », « SVG », « Projet »,
   récents) ; menus regroupés (Objet / Génération / Satin) ; **palette de commandes** (Ctrl+K) ;
   Préférences générales (thème, densité) hors du menu Affichage.
5. **Canevas et souris** : voir la §3 bis (modèle d'interaction complet, point central de l'ergonomie) ;
   surbrillance au survol, curseurs par poignée, infobulle live de dimension/angle ; couleurs de
   rails/accroche via tokens.
6. **Accessibilité** : `accessibleName` sur docks/barres/canevas, ordre de tabulation, anneau de
   focus 2 px, états par icône et pas seulement par couleur, cibles ≥ 24 px, suivi du thème système.
7. **Opérations longues** : segmentation, vectorisation, auto-numérisation et génération sont
   synchrones (curseur d'attente, zéro barre de progression) → tâches asynchrones avec progression
   et annulation (HP-PERF-001/UX-006, **P0** de la feuille de route).

Structurel (à répartir, pas à faire d'un bloc) : découper `main_window.cpp` (registre d'actions,
dialogues dédiés, scène) ; moteur de tâches ; système de notifications ; bibliothèque d'icônes ;
parcours guidé.

## 3 bis. Modèle d'interaction souris et clavier (inspiré de Fusion 360)

**Constat sur l'existant** (`canvas_view.cpp`) : seul le **bouton gauche** est géré ; le glisser
panoramique par défaut (`ScrollHandDrag`), donc **glisser dans le vide ne sélectionne pas** ; **aucune gestion
du clic molette** ni d'Espace ; la molette ne fait que zoomer (`angleDelta` seul : un pavé tactile précis
envoie `pixelDelta`, jamais lu) ; **les modificateurs sont quasi absents** (Maj contraint l'ellipse,
Maj+flèche change le pas) ; **aucun test** de molette, panoramique ou modificateur.

**Principe** : une table unique `InteractionMap` (contexte, bouton, modificateurs → intention) est la
source de vérité du comportement, de l'aide à l'écran et des tests. Ce que Fusion 360 fait bien et qu'on
reprend : *molette = zoom au curseur, clic molette = panoramique, sélection par fenêtre/croisement, Ctrl pour
ajouter/retirer, survol pré-sélectionnant, indications contextuelles des modificateurs, Échap = annuler,
Entrée = valider*. Confrontée le 2026-10-06 à la documentation publique d'Autodesk (recherche web ; le téléchargement direct
des pages était bloqué par le proxy du conteneur, donc seuls les extraits de recherche ont été lus) : voir
« Sources et degré de confirmation » plus bas.

| Contexte | Geste | Intention |
|---|---|---|
| Partout | Molette | Zoom ancré sous le curseur — *Fusion* |
| Partout | Ctrl + Maj + clic molette + glisser | Zoom continu — *Fusion (variante)* |
| Partout | **Clic molette + glisser** | Panoramique — *Fusion* |
| Partout | Double-clic molette | Cadrer le design (zoom ajusté) |
| Partout | Espace + glisser gauche | Panoramique (secours sans molette cliquable) |
| Partout | Maj + molette / Alt + molette | Défilement horizontal / vertical |
| Pavé tactile | Deux doigts / pincement | Panoramique / zoom (`pixelDelta`, `QNativeGestureEvent`) |
| Sélection | Clic | Sélectionner (remplace) |
| Sélection | **Maj + clic** | Ajouter seulement (recliquer ne retire pas) — *Fusion* |
| Sélection | **Ctrl + clic** | Basculer : ajouter / retirer — *Fusion* |
| Sélection | **Appui long sur un objet** (ou **Alt + clic**) | « Sélectionner dessous » : liste des objets superposés sous le curseur — *Fusion pour l'appui long* |
| Sélection | Glisser dans le vide | Rectangle : gauche→droite = *englobe* (entièrement dedans), droite→gauche = *croise* — *Fusion* |
| Sélection | Maj / Ctrl + glisser | Ajouter seulement / basculer, comme pour le clic — *Fusion* |
| Sélection | Touches **1 / 2 / 3** | Mode de sélection : fenêtre / lasso libre / pinceau (glisser sur les objets) — *Fusion (esquisse)* |
| Sélection | Double-clic objet | Entrer en édition de l'objet (nœuds / points) |
| Sélection | Survol | Surbrillance de pré-sélection + curseur adapté |
| Sélection | Clic dans le vide | Désélectionner |
| Déplacement | Glisser un objet | Déplacer ; **Maj** verrouille l'axe ; **Ctrl** suspend l'accroche ; **Alt** duplique |
| Dessin | Clic / double-clic ou Entrée | Ajouter un point / terminer |
| Dessin | Maj | Contraindre (angle 15° ; carré / cercle) |
| Dessin | Alt | Dessiner depuis le centre |
| Dessin | Ctrl (maintenu) | Suspendre l'accroche |
| Dessin | Retour arrière / Échap | Retirer le dernier point / annuler l'outil |
| Édition de nœuds | Glisser un nœud | Déplacer ; Maj = axe ; Ctrl = sans accroche |
| Édition de nœuds | Double-clic sur un segment | Insérer un nœud |
| Édition de nœuds | Suppr | Retirer les nœuds sélectionnés |
| Tous | Clic droit | Menu contextuel selon l'objet et le contexte (jamais d'entrée de débogage) |
| Tous | Suppr | Supprimer la sélection **quel que soit son type** (région, vectoriel, broderie) |

**Sources et degré de confirmation** (Autodesk, extraits de recherche web du 2026-10-06) :

- *Confirmé* : panoramique = clic molette maintenu ; zoom = molette (ou Ctrl+Maj+clic molette) ;
  rotation 3D = Maj+clic molette (**sans objet ici**, le canevas est 2D) ; **Ctrl (Cmd sur macOS) ajoute ou
  retire** de la sélection ; **Maj ajoute seulement** (un second clic ou un second passage du rectangle ne
  retire rien) ; rectangle haut-gauche→bas-droite = **fenêtre** (entièrement dedans), haut-droite→bas-gauche =
  **croisement** ; « Select Other » s'ouvre par **appui long du bouton gauche** sur la géométrie ; en esquisse,
  touches **1 / 2 / 3** = sélection par fenêtre / libre / pinceau ; Fusion propose des **préréglages de
  navigation** (Fusion, Inventor, SolidWorks, Tinkercad) dans les préférences.
- *Pas dans Fusion, proposition OpenStitch* : double-clic molette = cadrer ; Espace+glisser ; Maj/Alt+molette
  en défilement ; Alt = dupliquer au déplacement ; Alt = dessiner depuis le centre ; Ctrl maintenu = suspendre
  l'accroche ; double-clic sur un segment = insérer un nœud. À valider à l'usage.
- *Non vérifié* : le détail du menu radial (« marking menu ») du clic droit de Fusion, et les valeurs exactes
  (délai de l'appui long). On commence par un menu contextuel classique ; le menu radial est une option
  ultérieure.
- Sources : [raccourcis Fusion](https://www.autodesk.com/shortcuts/fusion-360),
  [préréglages pan/zoom/orbit](https://www.autodesk.com/products/fusion-360/blog/quick-tip-pan-zoom-orbit-preferences/),
  [3 façons de sélectionner une zone](https://www.autodesk.com/products/fusion-360/blog/tip-tuesday-3-ways-to-select-an-area-in-fusion-360/),
  [Select Other](https://www.autodesk.com/products/fusion-360/blog/quick-tip-how-to-use-select-other/),
  [sélection (aide)](https://help.autodesk.com/view/fusion360/ENU/?guid=SLD-SELECTION).

**Préréglages de navigation** : comme Fusion, une préférence « Navigation » avec au moins *OpenStitch*
(défaut ci-dessus) et un préréglage « pavé tactile / sans clic molette » ; les autres (Inventor, SolidWorks)
seulement si la demande existe.

**Retour visuel obligatoire** : une **ligne d'indications** (barre d'état) affiche en permanence ce que
chaque modificateur ferait *dans l'état courant* (« Clic : sélectionner · Maj : ajouter · Ctrl : basculer ·
Alt : objet dessous · Molette : zoom · Clic molette : panoramique ») ; le curseur change avec le
modificateur tenu (ajout, retrait, déplacement, duplication) ; l'aide « Raccourcis » est **générée** depuis
`InteractionMap` et les actions.

**Mise en œuvre Qt** : `CanvasView` passe en `NoDrag` et gère lui-même clic molette, Espace (focus
explicite pour que la barre d'espace ne « clique » pas un bouton), rectangle englobe/croise, et
`pixelDelta` / `QNativeGestureEvent` ; modificateurs lus **depuis l'évènement** (jamais
`QGuiApplication::keyboardModifiers()` différé, déjà une règle du code) ; l'état « Espace enfoncé » se
perd à la perte de focus (à tester). Le panoramique doit rester disponible *pendant* un outil de dessin.

**Tests** (QTest headless, `QTest::mouseClick/Press/Move` acceptent bouton et modificateurs ; molette et
gestes par évènements injectés ; aucun `sleep`) : une ligne de test par ligne de la table ci-dessus ; test
de **cohérence de la table** (aucun couple contexte+geste ambigu) ; test que l'aide affichée correspond à la
table ; non-régression du comportement actuel (clic gauche, flèches, Maj ellipse) avant de le modifier.

## 4. Stratégie de non-régression

Conventions du projet (`CLAUDE.md`) : QTest headless, aucune comparaison de pixels, aucun `sleep`.

| Couche | Contenu | État |
|---|---|---|
| **Existant** | 8 suites QTest (canevas, nœuds, panneaux, inspecteur, fenêtre principale, WSL, options, worker SAM) | vert (Linux/Qt 6.4) |
| **Invariants** (`test_ui_invariants`) | raccourcis uniques ; docks nommés et réouvrables ; mnémoniques uniques ; chaque défaut connu en `QEXPECT_FAIL(Continue)` → échoue en « XPASS » dès qu'il est corrigé | créé (6 tests) |
| **À ajouter avant de toucher au code** (lot 0) | *tests de caractérisation* du comportement actuel : matrice d'activation des actions selon l'état (vide / image / segmentée / objet sélectionné) ; Suppr par type d'objet ; exclusivité des modes d'édition ; visibilité des docks après rafraîchissement ; absence de fuite de widgets de la barre contextuelle (compte d'enfants stable) ; **souris** : clic gauche, flèches, Maj ellipse, molette actuelle | à faire |
| **Souris / clavier** (lot L5) | matrice `InteractionMap` : un test par geste (clic molette, Ctrl/Maj/Alt+clic, englobe/croise, Espace, molette, double-clic) | à faire avec L5 |
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
| 2 | **L2 — Design system v2 (identité propre)** | tokens (rayons, typographie, échelle d'espacement, élévation) ; style Qt de base forcé + QSS complet, aucun rendu natif ; recoloration des icônes par thème ; suppression des 2 hex et 13 `QColor` du canevas | M |
| 2 | **L5 — Modèle d'interaction souris/clavier** *(remonté en vague 2 : base de la prise en main)* | `InteractionMap` ; clic molette, Espace, molette ancrée + trackpad, Maj/Ctrl/Alt+clic, englobe/croise, survol, curseurs par modificateur, ligne d'indications, aide générée, Suppr universel ; tests de la §3 bis | L |
| 2 | **L3 — Notifications et retours** | `NotificationBanner` ; migration progressive des `QMessageBox` non destructifs ; libellés d'annulation ; barre d'état segmentée | M |
| 3 | **L4 — Icônes SVG** | jeu thémable, HiDPI, icône pour toute action | M |
| 3 | **L5b — Retours de canevas** | infobulle live de dimension/angle, poignées à cible ≥ 12 px, accroche animée avec libellé | S |
| 4 | **L6 — Parcours guidé** | stepper actionnable, écran d'accueil, regroupement des menus, palette de commandes, Préférences | L |
| 4 | **L7 — Accessibilité** | noms accessibles, ordre de tabulation, focus, états sans couleur seule | S |
| 5 | **L8 — Tâches asynchrones** | `TaskRunner` + progression + annulation sur segmentation/vectorisation/digitize/génération (HP-PERF-001, UX-006) | L |
| 5 | **L9 — Découpage de `main_window.cpp`** | extraction du registre d'actions et des dialogues ; **fait au fil des lots précédents**, pas en bloc | L |

**Risques** : régressions silencieuses de raccourcis (→ test d'unicité) ; conflits de fusion sur
`main_window.cpp` (→ propriété exclusive, petits commits) ; écarts Qt 6.4 / 6.8 (→ CI Windows) ; gestes
en conflit selon le contexte (ex. Maj = ajout au rectangle *ou* verrou d'axe → `InteractionMap` testée) ;
pavés tactiles et souris sans clic molette (→ Espace + glisser, pincement) ; asynchrone et
`Project` non thread-safe (→ fonctions pures sur instantanés, cf. `CLAUDE.md`).

**Décisions prises le 2026-10-06** : identité visuelle propre (pas de rendu natif Windows) ; la souris et
les modificateurs (clic molette, Ctrl, Maj, Alt) sont une priorité de premier rang, remontée en vague 2.

**Décisions encore à valider avant la vague 2** : les propositions de la §3 bis qui ne viennent pas de
Fusion (liste dans « Sources et degré de confirmation ») ; bibliothèque d'icônes SVG (licence compatible Apache-2.0 requise,
ex. Lucide en ISC / Tabler en MIT) ; densité par défaut ; ordre de L8 (le plus gros gain d'ergonomie, mais
le plus risqué).

## 6. État d'avancement des lots (mis à jour le 2026-10-06, après la PR #6)

| Lot | État | Notes |
|---|---|---|
| **L0** Filet de sécurité | ☑ livré (PR #6) | caractérisation, invariants, et suite adversariale `test_ui_adversarial` |
| **L1** Corrections de câblage | ☑ livré (PR #6) | G/Maj+E, Affichage ▸ Panneaux, modes exclusifs, Enregistrer, mnémoniques, récents, débogage |
| **L5** Modèle d'interaction + menu Aide | ☑ livré (PR #6), vérification manuelle Windows restante | `InteractionMap`, `CompositeCommand`, `CanvasView`, sélection multiple, Suppr universel, ligne d'indications, Aide/F1, préréglages de navigation |
| **L2** Design system v2 (identité propre) | ☐ à faire | tokens (rayons, typographie, espacement, élévation), QSS plat, style de base forcé, recoloration des icônes |
| **L3** Notifications et retours | ☐ à faire | bandeau non modal, migration des `QMessageBox` non destructifs, libellés d'annulation, barre d'état segmentée (la ligne d'indications existe déjà) |
| **L4** Icônes SVG | ☐ à faire | jeu thémable, HiDPI, icône pour toute action |
| **L5b** Retours de canevas | ◐ partiel | surbrillance au survol et curseurs par modificateur livrés ; restent infobulle live de dimension/angle, curseurs par poignée, accroche animée |
| **L6** Parcours guidé | ◐ partiel | guide de prise en main non modal livré ; restent stepper actionnable, écran d'accueil à cartes, regroupement des menus, palette de commandes, Préférences |
| **L7** Accessibilité | ◐ partiel | noms accessibles sur les dialogues d'aide et la ligne d'indications ; restent docks, barres d'outils, canevas, ordre de tabulation, focus, états sans couleur seule |
| **L8** Tâches asynchrones | ☐ à faire | le plus gros gain d'ergonomie et le plus risqué (HP-PERF-001, HP-UX-006) |
| **L9** Découpage de `main_window.cpp` | ☐ à faire | s'est encore alourdi avec L1/L5 ; à faire au fil des lots suivants |

Décisions prises en cours de route (2026-10-06) : l'**accrochage des nœuds au glisser** est désactivé par
défaut (réglage `edit/snapNodesOnDrag`, sommets des autres objets, ≤ 1 mm) ; les touches 2/3 (lasso, pinceau)
restent « prévues » ; M3 (Ctrl+glisser suspend l'accroche d'un corps d'objet) est « prévue » faute d'accroche.
