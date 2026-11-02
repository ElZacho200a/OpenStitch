# OpenStitch — audit différentiel UI/UX sur main (2026-11-02)

## Référence, périmètre et niveau de preuve

- Révision examinée : main à **2685f584a39ec74741fa25ae2cccdad5a62f5b3b**, datée du 1er novembre 2026.
- Références historiques : docs/ui-redesign-{audit,plan,specification}.md, docs/ui-ux-audit-2026-10-13.md, docs/ux/{audit-ux-ui-2026-10-18,synthese,strategie-interface,parcours-utilisateur,mesures-dialogues}.md et docs/roadmap-parite-hatch.md.
- Preuves : **C** = implémentation actuelle lue ; **T** = couverture de test présente (non relancée ici) ; **CI** = exécution GitHub Actions consultée ; **R** = recette interactive à effectuer ; **H** = mesure historique d'octobre, non reproduite sur ce commit.
- Limite : cette analyse a lu le dépôt et les journaux CI via GitHub, sans compilation locale, sans lancement d'une session Qt/Windows, sans véritable CLI Claude authentifiée, sans broderie machine et sans test utilisateur. Une observation de code n'est pas une reproduction visuelle.

## Synthèse décisionnelle

**Ne pas refaire l'identité visuelle.** Les thèmes Soie/Nuit indigo, les tokens, l'inspecteur, les docks responsive, la liste Document, la création directe et le compagnon MCP constituent un socle substantiel. Les prochains lots doivent homogénéiser les gestes, sécuriser les états, terminer l'annulation des tâches, et démontrer les parcours dans des conditions représentatives.

**Priorité de séquencement :** rétablir une CI fiable ; terminer les PR #162 et #164 plutôt que dupliquer leur contenu ; fermer les lacunes du parcours import/numérisation/édition ; réaliser une recette visuelle et agentique ; seulement ensuite envisager la fusion Document/Ordre ou des travaux décoratifs.

## Chantiers constatés et différentiels

| ID | Niveau | État sur main | Constat / preuves | Action recommandée |
| --- | --- | --- | --- | --- |
| D-01 | P1 · C | **À corriger, PR existante** | apps/desktop/main_window_ux.cpp:476-488 : l'horodatage de sauvegarde se replie sur QTime::currentTime() quand savedAt_ est invalide. PR #162 ouverte, mergeable_state « behind » lors de la revue. | Rebaser/mettre à jour #162, exécuter tests et fusionner. Pas de deuxième PR. |
| D-02 | P1 · C | **PR existante en conflit** | PR #164 : réglages avancés repliables de l'inspecteur, non intégrés à main ; mergeable_state « dirty ». Le reste du panneau demeure complexe. | Résoudre conflit #164 avec le code actuel ; vérifier clavier, deux thèmes et taille portable, puis fusionner. |
| D-03 | P1 · C | **Partiel** | apps/desktop/main_window_workflow.cpp:55-77 et 423-432 : la numérisation directe depuis le résultat IA passe encore par un WorkIndicator synchrone sans Annuler. | Migrer ce chemin vers une tâche snapshot + progression/annulation et commit unique, en conservant l'Undo. |
| D-04 | P1 · C | **Partiel** | apps/desktop/main_window.cpp:5739-5763 : F8 utilise BusyIndicator en arrière-plan, mais sans cancellable=true et sans TaskContext. À l'inverse, vectorisation F7 dans main_window_workflow.cpp:220-252 passe BusyIndicator(true), TaskContext et annulation atomique. | Uniformiser l'annulation réellement coopérative de F8 et du chemin IA direct ; conserver F7. |
| D-05 | P1 · C | **Manquant par rapport à la stratégie** | apps/desktop/main_window.cpp:2761-2838 : en mode Sélection, le double-clic tente d'insérer un nœud ; la transition contextuelle générale en mode Remodeler n'est pas implémentée. Il existe en revanche des modes satin/rails/guides/points dédiés. | Contractualiser double-clic, Échap et indicateur du mode. Garder l'insertion de nœud explicite en mode remodelage ; assurer la non-régression du texte et des points. |
| D-06 | P1 · C/H/R | **Mode portable disponible ; validation restante** | docs/ux/mesures-dialogues.md : à 911×480 logiques, canevas ~205×305 avec Document et Propriétés séparés, ~471×305 en mode portable (mesures du 18/10). | Mesures/captures sur Windows 1366×768 avec 125, 150 et 200 %, y compris docks et dialogues IA. Ne pas réécrire le layout sans preuve visuelle. |
| D-07 | P1 · C/T/R | **Compagnon intégré, recette réelle manquante** | PR #188, #190, #192 ; apps/desktop/companion_dock.cpp : avant/après, approbation et Undo existent. Des QTests sont présents ; ils ne prouvent pas un flux réel avec Claude Code authentifié et deux instances. | Scénarios d'action/approbation/rejet/undo, absence de faux succès, session périmée, doc non sauvegardé, PID distincts. |
| D-08 | P2 · C | **Fonctionnalité limitée** | apps/desktop/main_window_ux.cpp:106-145 : le glisser-déposer conserve le premier chemin ouvrable et ne réutilise pas event->position(). Cela recoupe HP-UX-052/060. | Intégrer un choix explicite pour plusieurs fichiers et une position d'insertion, avec garde du travail en cours. |
| D-09 | P2 · C | **Roadmap désynchronisée** | apps/desktop/main_window_editing.cpp:642-713 : le panneau Historique a un clic sur une ligne et une méthode jumpToHistory. Or HP-UX-054 demande encore d'étudier si le clic est absent. | Vérifier la couverture et corriger le statut de la roadmap ; ne pas recréer le panneau. |
| D-10 | P2 · C | **Architecture partiellement extraite** | apps/desktop/main_window.cpp reste ~11 520 lignes et main_window.hpp porte encore plusieurs états de sélection et de modes ; selection_model.* a été introduit en #219. | Extraire progressivement les contrats d'états, sans réécriture massive ; garde comportementale avant chaque déplacement. |
| D-11 | P2 · C | **Productivité en place, unification partielle** | Liste Document enrichie (#74), sélection avancée (#222), panneau Ordre séparé. | Étudier une vue métier unifiée Document/Ordre uniquement après tests de synchronisation et de drag-and-drop. |
| D-12 | P2 · C/R | **Accessibilité à valider** | Thèmes, noms accessibles et tests QTest existent ; les audits historiques reconnaissent l'absence de validation NVDA/UIA et d'essais interactifs sous DPI élevé. | Matrice clavier, focus, contraste non uniquement chromatique, taille des poignées, traduction, lecture d'écran. |
| D-13 | P2 · C | **Internationalisation partielle** | Roadmap HP-UX-008 : pouces dans règles/curseur/grille, mais une partie des champs/boîtes reste en millimètres ; HP-I18N-002 traduit la plupart des chaînes anglaises sans validation humaine complète. | Audit d'unités champs/affichages sans changer le stockage µm ; matrice FR/EN. |
| D-14 | P2 · C | **Amélioré, observabilité à homogénéiser** | Résumé F8 + action Voir, analyse avant export, statut de sélection et avertissements existent, mais certains messages restent éphémères en barre d'état ; HP-UX-013/039 restent partiels. | Historique court de notifications et états persistants pour erreurs non résolues, sans recréer des modales. |

## Acquis à ne pas refaire

- Refonte Soie/Nuit indigo et composants (PR #31).
- Correction documents/masques et interactions de base (PR #36).
- Responsive, mode portable, F8 préréglé, qualité, export rapide, sélection Document (PR #74).
- F7 asynchrone avec annulation atomique (PR #158).
- Workflow compatible image/SVG/broderie importée (PR #160).
- Segmentation source vs couleur de fil clairement différenciées (PR #165).
- Prévisualisation avant/après pour segmentation (PR #167).
- Claude MCP live, sécurité, approbation, Undo et mémoire sémantique (PR #188, #192).
- Redimensionnement multi-objets (#197), modèle de sélection (#219), recherche et lasso (#222).
- Centres radial/spiral déplacés sur canevas (#221).

## Disponibilité CI au commit audité

Actions CI du 2026-11-01, exécution 41352256009 : **format = succès, windows-msvc = succès, linux-core = échec** pendant la configuration ; l'échec résulte d'un téléchargement FreeType par vcpkg impossible (curl code 35, SSL) puis d'erreurs CMake en cascade. Exécution 41352256031, publication MCP Windows : **échec** car le générateur CMake « Visual Studio 17 2022 » ne trouve aucune installation de Visual Studio sur le runner. Cela nécessite une PR de fiabilisation CI/release distincte des chantiers UX ; ne pas interpréter l'ensemble comme un échec de compilation du code UI Windows.

Liens : https://github.com/ElZacho200a/OpenStitch/actions/runs/41352256009 et https://github.com/ElZacho200a/OpenStitch/actions/runs/41352256031.

## Scénarios de recette bout en bout — non exécutés dans cet audit

| Scénario | Pas de recette | Critères de succès et preuve à garder |
| --- | --- | --- |
| S1 · Image raster → DST | Ouvrir tests/fixtures/tentabrode.png ; régler une hauteur physique de 300 mm ; segmentation avec palette limitée à dix couleurs ; fusion/recoloration des régions ; F7 ; F8 ; inspecteur des types ; Analyse ; aperçu puis export DST. | Aucune perte du projet, formes et couleurs compréhensibles, absence de UI freeze, annulation cohérente, analyse à jour, export et rendu issus de la même séquence. Captures + OSP + DST + log. |
| S2 · SVG sans raster | Ouvrir un SVG contenant au moins trois formes ; sélectionner l'ensemble ; attribuer Contour/Tatami/Directionnel ; redimensionner en groupe ; défaire/refaire ; exporter. | Pas d'étape « segmentation manquante » trompeuse ; une intention de groupe = un pas Undo ; dimensions conservées ; objets visibles dans Document. |
| S3 · Import DST | Ouvrir un DST seul, examiner la simulation/Analyse ; sauvegarder sous OSP ; rouvrir ; vérifier l'indicateur de sauvegarde. | Les fonctions qui ne demandent pas d'image restent accessibles ; aucune heure de sauvegarde fictive. |
| S4 · Retouches et satin | Créer un satin, entrer en remodelage, modifier rails et guides, éditer des points ; modifier la densité puis revenir en arrière. | Le mode actif est explicite, Échap le quitte sans perdre la sélection, retouches invalidées signalées, sortie toujours annulable. |
| S5 · Claude/MCP live | Ouvrir OSP non sauvegardé ; demander « mets les objets orange compatibles en satin avec tirage prédictif » ; inspecter objets ciblés, plan, rendu avant/après, approuver, défaire ; rejeter un autre plan ; modifier le projet pendant un appel ; ouvrir deux instances. | Pas de mutation avant accord, aucun faux succès, erreurs visibles, refus de la révision périmée, isolation PID, effet immédiatement visible, un Ctrl+Z fiable. Logs expurgés + captures. |
| S6 · Portable + accessibilité | Windows 1366×768 à 100/125/150/200 %, thème clair/sombre, modes normal/portable, clavier sans souris et test lecteur d'écran. | Canevas utilisable, aucun OK/Annuler hors écran, focus/états lisibles, cible de poignée utilisable, pas de troncature de texte critique. |

## Proposition de découpage en PR indépendantes

**PR-00 — CI de référence et garde de recette (P0, indépendante).**
- Dépôt : .github/workflows, scripts/presets concernés, tests/unit/desktop.
- Réparer la publication Windows MCP (runner/générateur cohérents), rendre robuste l'approvisionnement FreeType Linux, diagnostiquer les échecs externes sans masquer un vrai défaut.
- Acceptation : format, Windows desktop et linux-core verts sur main ; publication MCP Windows crée un paquet ; conserver le journal d'exécution.

**PR-01 — Statut et garde des états du projet (P1, réutiliser #162).**
- Pas de nouvelle implémentation tant que #162 est ouverte : mise à jour sur main, tests QTest et intégration.
- Tests : ouvrir un OSP sans sauvegarder, projet neuf, enregistrement réel, undo vers l'état propre, second document.

**PR-02 — Inspecteur : divulgation progressive (P1, réutiliser #164).**
- Résoudre les conflits de #164 au lieu de créer un deuxième inspecteur.
- Tests : tatami, satin manuel et auto-satin ; les commandes restent câblées après dépliage ; petite fenêtre + clair/sombre.

**PR-03 — Pipeline asynchrone et annulation complète (P1, après PR-00).**
- Code : main_window_workflow.cpp, main_window.cpp, libs/autodigitize et project_ops si nécessaire, BusyIndicator.
- Transformer l'étape directe IA et F8 en tâche sur snapshot, propagation de TaskContext, stop coopératif et commit atomique sur le fil UI ; tout résultat annulé reste hors de l'Undo.
- Tests : annulation avant/pendant/fin, résultat périmé, égalité des résultats avec le chemin existant, ouverture des boutons UI pendant travail.

**PR-04 — Contrat de sélection/remodelage (P1, indépendante de PR-03).**
- Code : main_window.cpp, interaction_map.*, canvas_view.*, tests de gestes.
- Double-clic contextuel pour entrer en édition, Échap par niveaux, état actif visible ; insertion de nœud uniquement dans le bon mode ; préserver la réouverture du texte et le double-clic d'édition des points.
- Tests : clic sur satin/forme/texte, retour par Échap, sans changement silencieux de géométrie.

**PR-05 — Recette visuelle et accessibilité (P1, après PR-01/02/04).**
- Code : tests/unit/desktop/test_ui_responsive.cpp, scripts de captures, aide.
- Générer une matrice reproductible au DPI et thèmes représentatifs ; assertions de lisibilité fonctionnelle, focus, tabulation et taille ; NVDA/UIA reste une étape manuelle documentée.
- Ne pas imposer d'égalité pixel à pixel.

**PR-06 — Fiabilité observable du Compagnon (P1, après PR-00).**
- Code : tests/test_companion_dock, test_live_bridge_ui, backend/relay si défauts reproduits ; docs/source.
- Scénarios S5, instrumentation de traces d'actions/approbation sans fuite de données ; résultat en termes d'objets modifiés, erreurs et aperçu sans faux succès.
- Corriger seulement les dysfonctionnements mis en évidence par cette recette, pas de réécriture du dock.

**PR-07 — Petites frictions de productivité (P2, après PR-04).**
- Code : main_window_ux.cpp, document_panel.cpp, tests.
- Multi-drop, position de dépôt, confirmation remplacer/insérer, vérification Historique et statut roadmap, harmonisation de quelques messages persistants ; scinder si le diff dépasse un périmètre cohérent.
- Tests : trois fichiers déposés, position de curseur, aucun remplacement silencieux, History Undo/Redo.

**PR-08 — Document / Ordre et unité d'usage (P2, après PR-05).**
- D'abord étude UX instrumentée ; seulement ensuite unification éventuelle des listes, tri visuel, valeurs de groupe, affichage cohérent des pouces.
- Refuser la fusion si les parcours expert (optimisation, verrous, couleur) deviennent moins rapides.

## Ordre de lancement et définition de « terminé »

Ordre de lancement : PR-00 ; reprise simultanée des PR existantes #162/#164 ; PR-03 et PR-04 indépendantes ; PR-05/06 ; PR-07/08 ensuite. Une PR n'est « terminée » qu'avec code intégré, tests utiles, CI sur la bonne branche, captures ou logs de recette si l'interaction est graphique, roadmap à jour et aucune régression dans S1–S6.

Le premier benchmark utilisateur doit conserver des artefacts : projet .osp, export .dst, aperçu réel des points OpenStitch, segmentation technique, résultats qualité et rapport de changements. La comparaison doit porter autant sur les gestes et la compréhension que sur le temps de calcul.

## État d'intégration sur la PR #226 (2026-11-02)

Le présent rapport a été établi avant les modifications de la branche unique `docs/ux-audit-differentiel-20261102`. L'état ci-dessous concerne **cette branche**, pas encore `main` :

- **Porté dans la PR** : correctif de l'indicateur de sauvegarde et ses tests (travail de #162), inspecteur avancé tatami/satins et ses tests (travail de #164, rebasé manuellement sur le formulaire enrichi), diagnostic Windows QTest.
- **Porté dans la PR** : jobs Windows rattachés au runner `windows-2022` pour fournir le générateur Visual Studio 17 2022 ; nouvelle tentative de téléchargement vcpkg Linux en cas d'échec externe, sans neutraliser les échecs finaux.
- **Porté dans la PR** : l'auto-numérisation directe depuis la segmentation IA est calculée hors du fil UI ; F8 et ce chemin utilisent un compteur d'identifiants de travail. L'annulation rejette **tout le résultat avant commit** ; le moteur ne reçoit **pas encore** de `TaskContext` et ne stoppe pas immédiatement sa consommation CPU.
- **Porté dans la PR** : double-clic sur un satin sélectionné pour entrer dans le remodelage unifié rails/guides, sans commande ni mutation géométrique ; QTest associé.
- **Porté dans la PR** : plusieurs fichiers déposés ne sont plus silencieusement réduits au premier ; choix explicite ou insertion séquentielle de plusieurs projets/broderies machine par Maj+glisser.
- **Pas livré à ce stade** : annulation CPU coopérative complète des moteurs de numérisation, réorganisation profonde Document/Ordre, tests Windows interactifs/NVDA/DPI et recette en production de Claude Code/MCP. Ne pas annoncer ces critères comme validés.
- **Validation** : compiler et faire passer la CI sur le **dernier SHA de cette PR** ; les journaux des anciens commits peuvent contenir des erreurs de format corrigées depuis. La recette utilisateur S1–S6 doit encore être exécutée avant une fusion.


## Revue complémentaire du périmètre de PR unique — 2026-11-02

**Avertissement :** la liste initiale PR-00…PR-08 était un *plan d'intégration*, pas une liste de fonctionnalités déjà livrées. La PR #226 n'avait modifié que 11 fichiers et **ne prétend pas** couvrir tous les chantiers historiques. Les anciens audits ont des états parfois périmés : le statut ci-dessous est fondé sur les chemins de code/PR observés, pas sur le seul pictogramme de roadmap.

### Travaux encore absents de #226, à réexaminer dans la branche unique

| Domaine | Statut observé dans main | Complément requis et référence |
| --- | --- | --- |
| Palette d'outils par tâche et découvertabilité | **Non intégré à #226 ; roadmap ☐** | Grouper outils de création, dessin, retouche, satin et navigation, cohérence raccourcis/menu/étiquette active. HP-UX-001/004/014/016, sans renier la palette existante. |
| Navigation clavier complète | **Non intégré à #226** | Tab/Maj+Tab entre objets, sélection écran et panneau Document, annonce lecteur d'écran, gestion des raccourcis de modes. HP-UX-050, 011. |
| Undo des saisies numériques | **Non intégré à #226 ; coalescence partielle déjà disponible** | Vérifier X/Y/L/H/angle et propriétés de groupe : dix changements d'un même champ = un pas Undo sans fusionner deux gestes distincts. HP-UX-053. |
| Document et Ordre | **Non intégré à #226 ; vues et tri déjà présents** | Conception d'un parcours unifié ou explicitement synchronisé (sélection, couches, visibilité, verrou, rang), avec tests, sans retirer les optimisations avancées. HP-UX-037/045. |
| Assistant facultatif du premier motif | **Non intégré à #226 ; bandeau Workflow et aide déjà présents** | Aide contextuelle image → dimensions/cadre → palette → fond → numérisation → qualité → DST, sans wizard obligatoire. HP-UX-012, HP-UX-030/031. |
| Édition/aperçu des images | **Non intégré à #226 ; quantification et prévisualisations partielles déjà présentes** | Avant/après pleine résolution, comparaison vectorisation/simplification et annulation explicite, cohérence des masques. HP-UX-048, HP-VIEW-012. |
| Cohérence couleur région / objet / fil | **Non intégré à #226 ; première différenciation déjà livrée** | Parcours contextuel de traçabilité des couleurs entre segmentation, objets de couture et nuancier, légende persistante. HP-UX-047. |
| Préférences et unités | **Non intégré à #226 ; dialog et conversion mm/pouces partiels** | Cadrer magnétisme, défauts de couture, cadre/machine, unités réellement affichées dans inspecteurs et dialogues, sans altérer le stockage µm. HP-UX-007/008. |
| Retour d'état / erreurs persistantes | **Enregistrement repris de #162 ; reste absent de #226** | Révision des étapes Workflow, notification d'erreur persistante et actionnable, historique des opérations importantes, sans faux succès. HP-UX-013/039/061. |
| Qualité, vues, simulation | **Fonctionnalités de base présentes ; pas de nouvelle intégration dans #226** | Cohérence Analyse ↔ simulation ↔ export ; raccourcis lecture/pause et boucle ; indicateur « aperçu réduit ». HP-UX-031/036/055/059. |
| Accessibilité et petits écrans | **Tests headless préexistants ; aucun test réel exécuté pour #226** | Matrice DPI 100/125/150/200 %, FR/EN, contrastes, focus, cibles de poignée, NVDA/UIA, palette daltonisme. HP-UX-015/024–027/041/043/056 et HP-I18N-004. |
| Productivité quotidienne | **Partielle, non intégrée à #226** | Historique cliquable **déjà existant** : seulement tests/statut ; presse-papiers bitmap/fils, favoris couleurs, navigation au clavier et meilleur retour d'annulation. HP-UX-054/057/058/062/063. |
| Sélection avancée | **#219/#222 déjà fusionnées ; non à réimplémenter** | Sélections nommées limitées à la session, filtre par défauts Analyse, lasso de nœuds, rappel et menus Document. HP-UX-065. |
| Compagnon Claude/MCP | **Dock et approbation déjà livrés ; #226 ne modifie pas son code** | Recette de l'agent réel, journaux d'actions fiables, erreurs, annulation, révision périmée, multi-instances ; garantir la parité des commandes avec l'application. docs/agent-mcp-audit-2026-10-28.md. |
| Cartes dans les rapports | **PR #225 ouverte distinctement** | Intégrer ou réconcilier son affichage de cartes dans la PR unique après tests de redimensionnement, DPI et grands PNG, éviter deux PR concurrentes. |
| Compagnon IA « automatique » | **PR #227 ouverte distinctement** | Choix produit à traiter explicitement : mode sans approbation (désactivé par défaut) qui déroge au contrat de validation. Examiner la protection du document, du contexte et de l'Undo avant éventuelle consolidation. |
| Annulation CPU coopérative | **Non livrée : #226 rejette les résultats, mais les moteurs terminent le calcul** | Étendre TaskContext/stop_token au cœur autodigitize et project_ops, progrès réels, annulation atomique, retour clavier. HP-PERF-001/HP-UX-006. |
| Réarchitecture MainWindow et tests d'usage | **Partiellement découpé, sans garantie pratique** | Réduire la responsabilité de main_window.cpp/hpp, tests transversaux des parcours et instrumentation de benchmark réel. HP-QA-001/008/010. |

### À ne **pas** redévelopper sous prétexte d'une roadmap incomplète

- F7 non modal, calcul hors UI et annulation atomique déjà disponibles ; seules les autres opérations restent à auditer.
- Création directe contour/tatami/satin ; inspecteur multi-sélection ; restauration du focus et mode portable ; résumé qualité et export rapide ; panneaux Workflow et Analyse.
- Centre draggable spiral/radial, redimensionnement collectif, sélection inversée/par type/par fil/lasso, et menu de sélection avancé.
- Panneau Historique déjà cliquable via `jumpToHistory` (`main_window_editing.cpp`), malgré HP-UX-054 restée ouverte.
- Compagnon IA avec propositions/approbation, aperçu et Undo (#188 et #192), **différent** de la nouvelle option sans validation de #227.

### Critère d'exhaustivité

Le simple regroupement de petits correctifs dans une PR ne constitue pas « tout faire ». Pour rester une PR unique, conserver les lots dans des commits cohérents et garder la PR non fusionnée tant que les fonctions retenues ne sont pas intégrées, testées et vérifiées. Chaque ligne de la matrice doit sortir avec une décision explicite **livré / déjà présent / écarté / reste**, et toute exception de recette matérielle doit être signalée.
