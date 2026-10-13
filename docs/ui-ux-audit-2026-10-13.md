# Audit UX/UI d'OpenStitch Studio — 2026-10-13

*État : revue heuristique et statique après refonte, mesures de couleurs réalisées ; compilation desktop en échec, validation visuelle et tests d'usage non réalisés. Aucun correctif d'interface appliqué.*

## 1. Synthèse

**La refonte apporte de vrais progrès ; la priorité restante est la cohérence du document, des résultats et des interactions, avant une nouvelle refonte esthétique.** Sauvegarde et récupération sont mieux protégées, l'inspecteur est resynchronisé et défilable, les actions sont mieux expliquées, l'analyse est invalidée et les icônes suivent le thème.

Le registre actuel contient **32 constats : 2 P0, 17 P1 et 13 P2**, ainsi qu'un **blocage de compilation préalable à toute validation desktop**. Il ne reprend pas les défauts corrigés comme s'ils existaient encore.

Priorités proposées :

1. Rétablir le build de référence pour pouvoir prouver les parcours.
2. Découpler la génération des points de la présence d'une image ; reconnaître les fichiers machine seuls dans toutes les gardes de contenu.
3. Sortir les calculs lourds du fil d'interface et sécuriser l'annulation IA.
4. Rendre honnêtes les dimensions d'import, les réglages de groupe et les avertissements avant export.
5. Corriger les états visuels combinés, le clavier et les cibles du canevas, puis valider petits écrans, DPI et lecteur d'écran.

**Ce n'est ni une certification d'accessibilité, ni une étude utilisateurs, ni une inspection du rendu réellement affiché.** Aucun score esthétique arbitraire, aucune durée de calcul et aucun résultat QTest ne sont inventés.

## 2. Référence, méthode et limites

### Version examinée

- Référence gelée : **`fb0045547de789877c693334139a81f267136683`**, fusion de la PR #31, `claude/ui-refonte-composants`.
- Worktree de lecture et de build : `C:\Users\zache\.local\share\opencode\worktree\85b307\audit-ux-ui-20261013`.
- Application native C++ / Qt 6 Widgets sous Windows, pas une application web.
- Le [premier passage avant fusion](ui-ux-audit-2026-10-13-pre-refonte.md), sur `722ec3ade85f3fcaf8e41c30962b34268ed2ca36`, est **archivé et non applicable globalement**. Ses couleurs et numéros de lignes ne sont pas réutilisés.
- Les conclusions portent sur le snapshot, pas sur d'éventuels commits ou modifications concurrents postérieurs.

### Couverture

Revue des parcours ouverture/import/Nouveau/sauvegarde/récupération, prétraitement/segmentation/vectorisation/numérisation, dessin et transformation, inspecteur mono/multi-objets, document, workflow, analyse/export multiformat, simulation, fils/nuanciers, lettrage, production/PDF, rendu réaliste, accueil/récents, aide, thèmes/icônes/règles et raccourcis. Lecture ciblée des suites desktop et des contrats du modèle, des commandes et du placement d'image.

Deux revues indépendantes en lecture seule ont couvert les composants de panneaux/IA et les surfaces visuelles/accessibilité. La session principale a réconcilié les résultats avec le câblage `main_window*.cpp` : par exemple, les poignées sont effectivement reconstruites au changement de thème ; ce risque conditionnel n'est donc **pas** retenu comme défaut actuel.

Documentation confrontée : `docs/ui-redesign-specification.md`, `docs/ui-audit-2026-10.md`, `docs/audit-ui-ergonomie.md` et sections UX/I18N/PERF de `docs/roadmap-parite-hatch.md`. **`mesh-mcp/search_docs` indisponible : repli sur recherches et lectures ciblées.** La présence d'un correctif annoncé dans une page ne remplace pas la lecture du chemin exécuté.

### Niveaux de preuve et priorité

- **C — Code** : chemin vérifiable dans les sources ; scénario non exécuté dans l'application pendant cet audit.
- **M — Mesure** : calcul sRGB sur les couleurs déclarées ; pas un prélèvement d'écran.
- **V — À valider** : effet ergonomique/visuel dépendant du rendu, du périphérique ou du lecteur d'écran.
- **P0** : parcours essentiel bloqué ou risque majeur de non-réactivité ; priorité de correction, sans prétendre avoir mesuré son incidence.
- **P1** : résultat/état trompeur, risque d'erreur de production ou obstacle d'interaction important.
- **P2** : friction, découvrabilité, cohérence ou finition.

Toutes les références de code sont relatives au worktree gelé. **`MW` = `apps/desktop/main_window.cpp`**. Les identifiants hérités sont conservés lorsque le sujet subsiste ; les trous correspondent aux constats fermés ou regroupés, pas à des défauts oubliés.

### Build réellement tenté

Configuration CMake du worktree réussie. Compilation Release de l'application et des **21 suites desktop** de `tests/unit/desktop/CMakeLists.txt` :

```powershell
cmake --build --preset msvc-release --parallel 2 --target openstitch test_canvas_view test_node_handle test_document_panel test_properties_panel test_main_window test_ui_invariants test_ui_adversarial test_ui_characterization test_ux_global test_canvas_input test_interaction_map test_help_dialogs test_selection_hit_test test_wsl_path_converter test_generation_options test_segmentation_workflow test_realistic_view test_production_dialog test_thread_panel test_lettering_ui test_sam_worker_client
```

**Résultat : code de sortie 1**, erreur MSVC :

```text
apps/desktop/main_window.cpp(5203,65): error C3493:
impossible de capturer implicitement 'kFlattenTolerance'
car aucun mode de capture par défaut n'a été spécifié
```

Dans `offsetVectorObjectCore`, la lambda `[]` utilise la constante locale `kFlattenTolerance` (`MW:5201-5203`). **Ce blocage existe aussi dans la référence refondue**, pas seulement dans l'ancienne version. Il n'a pas été corrigé ici. Les suites liées à `openstitch_desktop_widgets` ne peuvent pas être livrées par ce build : **aucune suite QTest exécutée**, aucune capture de cette référence, pas de validation Release/Debug complète revendiquée. Les avertissements de compilation ne sont pas présentés comme des défauts UX.

## 3. Acquis et réconciliation du premier passage

« Corrigé » ci-dessous signifie **mécanisme présent dans le code relu**, pas preuve dynamique obtenue pendant cet audit.

| Sujet / anciens IDs | État après refonte et preuve |
|---|---|
| Protection des remplacements — UX-01 | Garde commune avant image, SVG, projet et import machine ; échec de lecture avant remplacement dans les chemins relus (`MW:1375-1408`, `7572-7607`, `8023` et suivants). Récents/glisser-déposer/argument de lancement utilisent ces entrées (`main_window_ux.cpp:70-129`). |
| Récupération — UX-03 | Une récupération par session ; « Plus tard » conserve tout ; échec conserve l'instantané ; source récupérée conservée jusqu'à une sauvegarde réussie (`MW:7738-7821`). |
| Inspecteur périmé — UX-05 | Comparaison des paramètres, adoption de l'état courant et resynchronisation ; éditions champ par champ, tirage calculé depuis l'état affiché actualisé (`MW:6579-6617`, `properties_panel.cpp:763-779`, `1129-1134`). Les états mixtes de groupe restent un autre problème, UX-33. |
| Bornes et saisie — UX-11 / UX-22 | Minima métier par paramètre, `setKeyboardTracking(false)` et garde molette ; fusion de commandes scalaire dans le chemin mono-objet (`properties_panel.cpp:311-324`, `799-818`, `1138-1157`, `MW:6598-6608`). La régénération reste synchrone : suivie dans UX-04, pas un doublon de constat. |
| Scroll et édition numérique — UX-12 / partie de UX-17 | Inspecteur dans `QScrollArea` (`MW:6559-6566`), boîte vectorielle X/Y/L/H éditable (`properties_panel.cpp:379-442`). Ne signifie pas que tous les dialogues tiennent sur un petit écran ou que les nœuds/rails sont éditables sans souris. |
| Analyse périmée — UX-09 | Invalidation et recalcul différé sur le chemin de rafraîchissement normal (`MW:3042`, `main_window_analysis.cpp:286-296`), suppression des résultats quand le recalcul ne trouve plus de séquence (`164-179`). Le retour prématuré sans raster contourne encore ce mécanisme : traité par UX-02. |
| Pré-export — partie de UX-10 | Résumé avant choix du fichier, analyse intégrée, limites multiformat et lien vers les problèmes (`MW:7860-7952`, `machine_export_dialog.cpp:126-139`). Sécurisation et égalité des règles restent à compléter. |
| Workflow — partie de UX-14 | Les étapes disponibles déclenchent maintenant une action (`main_window_workflow.cpp:498-529`), pas seulement une indication. États et adaptation à la source encore incomplets. |
| Sortie IA et démarrage — parties de UX-15 / UX-16 | Choix régions éditables/broderie explicitement annoncé ; annulation au démarrage empêche l'enchaînement automatique (`ai_segmentation_dialog.cpp:220-244`, `549-573`). Repli couleur et événements tardifs restent ouverts. |
| Icônes, texte d'alerte, thème à chaud — UX-18 / UX-19 / partie de UX-27 | Icônes peintes selon thème/état/DPR (`ui_icons.cpp:51-79`), nouvelle palette contrastée, accueil rafraîchi. `MW:621` reconstruit les items de scène au changement de thème : ne pas réclamer à nouveau une recoloration des poignées inexistante. |
| Français Qt — partie de UX-20 | `main.cpp:25-38` charge `qtbase` français. Le déploiement effectif reste à vérifier ; absence de choix anglais = évolution produit, pas ancien défaut « aucun traducteur ». |
| Actions partagées et raisons de grisage — UX-23 | Barre principale partage les actions ; raisons d'indisponibilité et réglages mémorisés sont présents (`MW:5762` et suivants, `8836-8915`, `ui_memory.*`). |
| Métadonnées et édition — partie de UX-24 | Couleur/type, renommage/visibilité/ordre figé dans Document, boîte numérique et historique existent. Pas un inspecteur absent. |
| Simulation — partie de UX-29 | Vitesse choisissable et information d'objet/fil ajoutées (`MW:7249-7327`). Le compteur et la navigation restent perfectibles. |
| Repère de sauvegarde — partie de UX-32 | `undoStack_.markClean()` à la sauvegarde et `isClean()` au rafraîchissement (`MW:2963-2965`, `7563`). Ne plus affirmer que tout undo reste artificiellement « modifié ». L'heure affichée est un défaut distinct. |
| Calculs classiques — partie de UX-04 | Segmentation et auto-numérisation classiques passent déjà par `BusyIndicator::run`, `std::async` et traitement des événements de peinture (`MW:1214-1220`, `4168`, `4586-4594`). Cela reste modal sans annulation, mais ce n'est pas un calcul moteur sur le fil UI. Les autres chemins synchrones sont recensés séparément ci-dessous. |

Navigation souris/pavé tactile, survol, sélection multiple, commandes annulables, docks réouvrables, aide recherchable non modale et menus de panneaux restent des fondations à conserver. Le lettrage, les fils, la fiche de production et le rendu réaliste sont réellement câblés ; ils ne sont pas déduits de la seule roadmap.

## 4. Audit des parcours

| Parcours | Acquis | Risque prioritaire restant |
|---|---|---|
| Premier lancement → premier motif | Accueil, récents, guide actionnable | Accueil dans un canevas contraint ; entrée dessin moins évidente que l'import (UX-43). |
| Image → taille → régions → broderie | Import ajusté au cadre, actions expliquées, prévisualisations | Taille Y non consommée ; calculs synchrones (UX-07, UX-04). |
| SVG / dessin / texte → broderie | Import SVG, formes, texte, paramètres et boîte numérique | Génération court-circuitée sans image ; vecteurs non cousus absents de Document (UX-02, UX-13). |
| Multisélection → réglage → undo | Édition de groupe champ par champ, commandes composites | Valeurs mixtes masquées et champs de portée individuelle dans le formulaire de groupe (UX-33, UX-34). |
| IA → revue → validation | Masques, choix de sortie, scores expliqués, annulation initiale | Identités éditables, protection perdue à la fusion, annulation tardive et repli non conservé (UX-35, UX-36, UX-16, UX-15). |
| Analyse → correction → fichier machine | Problèmes localisés, aide à la correction, résumé multiformat | Pré-export moins complet que l'analyse de lettrage ; décision par défaut malgré risques (UX-10). |
| Machine importé seul → OSP | Design importé reconnu par le modèle et le slot de sauvegarde | Actions de sauvegarde, autosave et affichage ne partagent pas la même définition du contenu (UX-08). |
| Arrêt anormal → récupération | Garde et conservation du candidat | Preuve dynamique encore à exécuter ; pas de P0 historique recyclé. |
| Fils → ordre → production/PDF | Catalogue/import, film couleur, résumé, vitesse de production explicitement hypothétique | Accès clavier de la barre des fils ; calcul initial de rendu/fiche synchrone (UX-46, UX-04). |
| Clavier / accessibilité | Focus canevas, noms et aides, saisie numérique | Espace peut partir vers le canevas ; rails/nœuds et annonces incomplets (UX-40, UX-17, UX-42). |

## 5. Registre actuel

### 5.1 Parcours essentiels et réactivité

#### UX-02 — Points non régénérés dans les documents sans image — P0 · C

- **Preuve :** `refreshImage()` retourne si le projet n'a ni image ni `imported_design` (`MW:2971-2974`), avant remise à zéro et génération de `sequence_` (`3010-3037`) ainsi que plusieurs rafraîchissements (`3039-3050`). SVG, dessin et texte peuvent pourtant produire des vecteurs et broderies. `applyText` appelle bien ce chemin (`main_window_lettering.cpp:204-213`).
- **Impact :** résultat visible/compteurs/export non conformes au document ou indisponibles sur ces parcours essentiels ; une ancienne analyse peut aussi rester non signalée comme périmée. `applyLoadedProject` rafraîchit certains panneaux sans image (`MW:7843-7853`), **mais ne restaure ni la génération manquante ni l'invalidation d'analyse**.
- **Recommandation :** rendre le pipeline raster facultatif et le rafraîchissement du document/de la séquence obligatoire.
- **Validation :** SVG, rectangle dessiné, texte neuf et OSP purement vectoriel → couture → aperçu, analyse, simulation et export ; modification/suppression/undo/redo cohérents, sans image factice.

#### UX-04 — Calculs lourds encore synchrones et non annulables — P0 · C/V

- **Preuve :** `WorkIndicator` décrit explicitement un calcul synchrone, sans Annuler, et appelle `processEvents` seulement avant le travail (`main_window_workflow.cpp:50-62`) ; il enveloppe la vectorisation et la numérisation issue de l'IA (`213` et suivants, `397-400`). Régénération (`MW:3027`), écriture autosave (`7709`), construction/nettoyage/raffinement IA (`ai_segmentation_dialog.cpp:874-887`, `934-976`), création de texte (`main_window_lettering.cpp:187`), rendu réaliste (`main_window_realistic.cpp:195-208`) et aperçu de production (`production_dialog.cpp:144-150`, `181-183`) sont exécutés dans leurs callbacks UI.
- **Impact :** sur ces chemins, l'indicateur donne un retour initial, pas une garantie de réactivité pendant le calcul. La temporisation des aperçus et les caches évitent certains recalculs, **sans déplacer ce travail hors du fil UI**. Segmentation et auto-numérisation classiques utilisent en revanche déjà `BusyIndicator::run` hors fil UI : ne pas les confondre avec `WorkIndicator`. Durées et incidence réelle non mesurées.
- **Recommandation :** calcul sur instantané immuable hors fil UI, progression adaptée, annulation et application atomique par commande ; dernière prévisualisation valide conservée. Distinguer annulation de l'inférence et annulation de la validation IA.
- **Validation :** événement/timer UI traité pendant une opération représentative ; annulation sans mutation ; résultat périmé rejeté ; sortie identique au calcul synchrone. Mesurer les temps, sans reprendre d'anciens benchmarks comme preuve de cet audit.

### 5.2 Exactitude du document et des réglages

#### UX-06 — Recliquage du type actif réinitialise encore les paramètres — P1 · C

- **Preuve :** bouton courant toujours relié à `setStitchType` (`MW:5916-5925`) ; pas de no-op sur égalité de type (`4914-4923`) ; contour/tatami repartent de paramètres neufs (`4856-4866`) puis conversion de groupe (`4937-4946`).
- **Impact :** un clic de confirmation peut effacer un réglage de densité/longueur ou effectuer une conversion inutile.
- **Recommandation / validation :** type déjà actif = aucune mutation ni étape undo ; réinitialisation explicite séparée. Tester les quatre types et les groupes à plusieurs sections.

#### UX-07 — Hauteur d'import non proportionnelle non appliquée — P1 · C

- **Preuve :** `ImportDialog` retourne largeur **et** hauteur (`import_dialog.cpp:235-237`), mais `openImageFile` n'en conserve qu'une échelle calculée par la largeur (`MW:1369-1384`, `libs/document/src/image_placement.cpp:67-71`). Le modèle utilisé par le chemin raster reste isotrope. L'information « Résolution » ne décrit elle aussi que X (`import_dialog.cpp:194-203`).
- **Impact :** image carrée saisie en 80 × 40 mm : le contrat actuel de la fenêtre conserve l'échelle de 80 × 80 mm ; taille annoncée et éventuel contrôle de cadre divergent. Ce n'est **pas** un défaut du retour de `placement()` lui-même.
- **Recommandation / validation :** appliquer les deux axes partout ou désactiver honnêtement l'import déformé ; saisie, canevas, segmentation et sortie machine doivent coïncider. Si X/Y diffèrent, afficher deux résolutions ou préciser l'axe.

#### UX-08 — Un fichier machine seul n'est pas reconnu partout comme contenu — P1 · C

- **Preuve :** `saveProjectAs` admet maintenant `imported_design` (`MW:7461-7468`), mais `hasDocument` pour Enregistrer/Enregistrer sous l'ignore (`8907-8912`), comme la vacuité autosave (`7700-7703`) et l'activation Afficher les points (`8871-8872`).
- **Impact :** les actions et la protection automatique ne reflètent pas ce document pourtant visible ; le slot et son action ont des contrats différents. Le risque autosave concerne un état importé devenu modifié, pas une promesse d'autosauvegarder tout fichier propre.
- **Recommandation / validation :** prédicat de contenu commun ; activer l'affichage selon la séquence pertinente. DST/PES/JEF/EXP seuls → sauvegarde OSP, réouverture, garde de fermeture et état modifié protégés sans objet artificiel.

#### UX-33 — « Réglages communs » masque les valeurs multiples — P1 · C/V

- **Preuve :** formulaire construit depuis les paramètres d'un représentant (`properties_panel.cpp:557-561`, `697-700`) ; pas d'état mixte par champ dans le contrat de groupe. L'édition scalaire, elle, conserve correctement les autres champs de chaque objet (`main_window_editing.cpp:114-134`).
- **Impact :** deux tatamis à 0,4 et 0,8 mm semblent avoir une même densité ; afficher un représentant n'est pas afficher une valeur commune.
- **Recommandation / validation :** afficher « valeurs multiples » par champ ; changer l'angle uniformise seulement l'angle, pas la densité. État lisible au clavier/lecteur d'écran, aucun changement implicite à l'ouverture du formulaire.

#### UX-34 — Portées mélangées dans le formulaire de groupe — P1 · C

- **Preuve :** les paramètres scalaires émettent `groupParamsEdited`, mais entrée/sortie, dégradé et bordure émettent l'ID du représentant (`properties_panel.cpp:752-755`, `774-778`, `1095-1098`, `1193-1196`). Intégration confirmée : `setJoinMode`, `editBorderSatin` et `setDensityGradient` ne modifient que cet ID (`main_window_engine.cpp:69-101`, `main_window_directional.cpp:112-137`).
- **Impact :** un bloc présenté comme réglage de plusieurs coutures applique certains champs au groupe et d'autres à une seule couture, sans séparation de portée.
- **Recommandation / validation :** contrats de groupe explicites ou section « Objet actif seulement » distincte ; multisélection de deux objets → chaque contrôle affecte exactement la portée annoncée, avec un undo cohérent.

### 5.3 Parcours, analyse et IA

#### UX-10 — Pré-export moins complet que l'analyse et décision peu prudente — P1 · C

- **Preuve :** export appelle `stitch_analysis::analyze` sur les points (`MW:7896`), tandis que le panneau ajoute `analyze_text_objects(project_)` (`main_window_analysis.cpp:188-192`). Il peut donc annoncer « aucun problème » avec des avertissements de lettrage. Les erreurs d'analyse/dépassements sont affichés, mais seul `check_export_limits` pose `blocking` (`MW:7909-7920`) ; « Choisir le fichier… » demeure bouton par défaut (`machine_export_dialog.cpp:71-72`, `139`).
- **Impact :** vérifications non équivalentes selon l'entrée ; Entrée peut poursuivre malgré certains risques. « Voir les problèmes » existe maintenant : ne pas l'annoncer absent.
- **Recommandation / validation :** rapport partagé avec les règles de texte ; décision explicite en présence de risques, sauvegarde OSP accessible et pertes de couleur annoncées pour le DST. Motif avec texte trop petit/hors cadre → même diagnostic avant export et dans Analyse ; politique de blocage clairement distinguée d'un avertissement autorisant l'export.

#### UX-13 — Vecteurs non cousus absents de Document — P1 · C

- **Preuve :** lignes Objets construites depuis les broderies ; vecteurs utilisés pour les noms de groupes seulement (`document_panel.cpp:370-422`). `Kind::Vector` non traité par la synchronisation (`523-563`) ; auto-visibilité du dock ne tient pas compte des vecteurs seuls (`MW:6532`).
- **Impact :** un SVG non numérisé ne peut pas être inventorié/recherché/sélectionné par ce panneau ; obstacle important au clavier.
- **Recommandation / validation :** ligne vectorielle indépendante des coutures attachées ; SVG de plusieurs formes entièrement navigable avant création des points, sélection synchronisée dans les deux sens.

#### UX-14 — Workflow actionnable, mais progression encore raster-centrique — P1 · C

- **Preuve :** Image/Régions dépendent encore du raster ; Vérification/Export restent seulement `Available` dès qu'une séquence existe (`MW:6385-6404`), même après analyse/export réussis.
- **Impact :** SVG, dessin, texte et import machine ne lisent pas une progression fidèle ; aucune trace fiable de ce qui est vérifié ou exporté.
- **Recommandation / validation :** étapes adaptées à la source, disponibilité distincte de validation, état lié à la révision du document. SVG ne réclame pas de segmentation ; export réussi visible, modification invalidant la validation précédente.

#### UX-15 — Repli du découpage couleur IA non conservé — P1 · C

- **Preuve :** échec de raffinement : message puis succès (`ai_segmentation_dialog.cpp:981-986`) ; appelant ferme immédiatement via `accept()` (`888-890`). Pas de champ dédié au repli dans le résultat public (`ai_segmentation_dialog.hpp:52-57`).
- **Impact :** résultat non subdivisé malgré l'option choisie, sans occasion fiable de lire l'avertissement. Le nom/choix régions versus broderie est désormais amélioré, contrairement au premier passage.
- **Recommandation / validation :** avertissement acquitté avant fermeture ou résultat structuré avec notification persistante. Forcer l'échec couleur : l'écart au résultat demandé reste consultable après validation.

#### UX-16 — Annulation IA tardive sans verrou local, réglages non figés — P1 · C/V

- **Preuve :** hors démarrage, annulation ne change pas la phase (`ai_segmentation_dialog.cpp:563-566`) ; chargement terminé peut encore lancer la segmentation (`599-614`). Plusieurs handlers ignorent `requestId` (`510-517`, `587-588`, `616-624`, `654-662`). Profil/seuils lus après chargement (`608-611`), sans capture complète au lancement.
- **Impact :** la robustesse dépend des garanties du client/worker ; aucune course réellement reproduite ici. La configuration peut être prise à plusieurs moments.
- **Recommandation / validation :** état `Cancelling`, filtrage par requête et paramètres capturés au lancement. Test annulation chargement → événements tardifs : aucune nouvelle segmentation/réactivation ; changer les réglages pendant l'attente ne modifie pas la requête en cours.

#### UX-35 — Identifiant et statistiques des masques IA éditables — P1 · C

- **Preuve :** éléments numériques gardent les flags éditables (`ai_segmentation_dialog.cpp:50-53`, `80-82`, `692-698`) ; ID ensuite relu depuis le texte (`717`, `802`, `902-905`).
- **Impact :** modifier l'ID désolidarise une ligne de son masque et peut l'exclure silencieusement ; modifier aire/IoU/stabilité ne change que l'affichage.
- **Recommandation / validation :** statistiques en lecture seule, ID dans un rôle dédié ; double-clic/F2 sans mutation des valeurs ; tri/aperçu/validation conservent exactement les identités.

#### UX-36 — Fusion IA retire la protection des masques — P1 · C

- **Preuve :** sources retirées, masque fusionné toujours créé avec « Protéger » décoché (`ai_segmentation_dialog.cpp:800-847`, `862-868`) ; protection utilisée ensuite au nettoyage (`914`).
- **Impact :** une protection explicite disparaît sans décision, ce qui change le traitement futur.
- **Recommandation / validation :** hériter de la protection si une source est protégée, ou demander la décision ; fusionner deux masques dont un protégé conserve le statut annoncé.

### 5.4 Visuel, clavier et accessibilité

#### UX-17 — Édition fine du canevas sans alternative complète — P1 · C/V

- **Preuve :** X/Y/L/H vectoriels maintenant éditables ; nœuds/rails/poignées restent des items custom déplacés par événements souris (`node_handle.hpp:37-84`, `269` et suivants). Focus/Échap ne constituent pas une édition numérique ou clavier des coordonnées de chaque nœud.
- **Impact :** les transformations simples progressent, mais un parcours fin n'est pas garanti sans souris ; nommer le canevas ne rend pas sa scène sémantiquement accessible.
- **Recommandation / validation :** édition numérique des éléments fins, focus de scène et annonce sélection/valeur/unité ; réaliser un motif avec nœud/guide/rail sans souris, puis vérifier son export. Contrôler NVDA/UIA ; ne pas déduire un échec de l'absence d'un `setAccessibleName` si Qt fournit déjà un label natif.

#### UX-37 — Sélection + survol : texte presque illisible — P1 · C/M/V

- **Preuve :** texte de sélection appliqué, puis fond de survol remplacé sans changer ce texte (`app_theme.cpp:168-172`). Clair blanc sur `#E4E1FA` = **1,277:1** ; sombre `#1B0E0A` sur `#3A2A4A` = **1,445:1**.
- **Impact :** état combiné incohérent malgré de bons contrastes dans les états simples ; rendu QSS effectif à confirmer.
- **Recommandation / validation :** `:hover:!selected` ou état combiné explicite ; sélectionner puis survoler une ligne de liste dans les deux thèmes, vérifier les couleurs effectives.

#### UX-38 — Grille sombre blanche sur l'intérieur blanc du cadre — P1 · C/M

- **Preuve :** fond intérieur toujours blanc (`canvas_view.cpp:1107-1110`), grille sombre blanche translucide (`design_tokens.cpp:86-88`), puis peinture de grille (`canvas_view.cpp:1130-1134`) : contraste intérieur **1,000:1** sur une zone non recouverte de contenu.
- **Impact :** repère de placement invisible à cet endroit ; l'extérieur sombre n'a pas le même problème.
- **Recommandation / validation :** séparer fond de travail/tissu et couleurs des repères ; grille discernable dedans/dehors en clair/sombre, y compris avec le rendu réaliste.

#### UX-39 — Cibles de poignées hors de leur boîte englobante — P1 · C/V

- **Preuve :** items visuels 8 × 8, `shape()` 22 × 22, sans `boundingRect()` englobant cette cible (`node_handle.hpp:42`, `54-59`, `269`, `282-286`). Géométrie incohérente pour les recherches préliminaires de scène Qt.
- **Impact :** l'élargissement promis du hit-test n'est pas fiabilisé ; zone réellement atteignable à reproduire, pas affirmée depuis une capture.
- **Recommandation / validation :** boîte englobante couvrant la cible, dessin petit préservé ; clic/glisser à ±10 DIP, différents zooms/DPR et cibles chevauchantes. 22 DIP ne prouve pas une ergonomie tactile suffisante.

#### UX-40 — Espace peut être capturé hors du canevas focalisé — P1 · C/V

- **Preuve :** filtre applicatif teste pointeur sur viewport et exclusion des champs de saisie, pas tous les boutons/cases/listes (`canvas_view.cpp:699-743`). Le filtre `ShortcutOverride` de MainWindow ne restitue pas explicitement Espace à ces contrôles (`main_window_ux.cpp:136-163`).
- **Impact :** pointeur immobile sur canevas, Tab vers une case/bouton : Espace peut activer le panoramique au lieu du contrôle.
- **Recommandation / validation :** priorité au widget focalisé hors canevas ; scénario ci-dessus avec case, bouton et liste, puis panoramique Espace fonctionnant quand le canevas détient le focus.

### 5.5 Frictions et finitions

| ID / priorité / preuve | Constat, impact et preuve | Recommandation / validation |
|---|---|---|
| **UX-25 · P2 · C** | L'aide liste « Entrer en édition de l'objet » pour le double-clic (`interaction_map.cpp:97`, `268-273`, `help_dialogs.cpp:241-243`), mais l'effet en sélection concerne le texte seulement, pas toute forme (`MW:2111-2135`). | Restreindre le libellé au texte, ou implémenter la transition générale. Chaque geste promis doit avoir un test d'effet, pas seulement une ligne dans la table. |
| **UX-26 · P2 · C** | Erreurs de génération/autosave uniquement en barre d'état (`MW:3035-3036`, `7713-7714`) ; tous les messages soumis à effacement à 10 s (`502-514`). L'indicateur permanent existe, mais ne dit pas l'échec d'autosave. | Notification d'erreur persistante avec reprise et court historique ; aucun avertissement important remplacé par un geste ou une réussite sans rester consultable. |
| **UX-28 · P2 · C/V** | Règles à 7,5 pt et boîtes/épaisseur fixes (`ruler.cpp:14-39`, `79-90`) ; densité agit principalement sur `controlHeight` (`design_tokens.cpp:9-18`). Pas de choix `Fusion` trouvé au lancement/gestion du thème (`main.cpp:18-43`, `app_theme.cpp:378-390`). | Métriques dérivées de la police, politique de style explicitement choisie (Fusion n'est pas une obligation UX en soi). Police agrandie 150/200 % → règles lisibles sans chevauchement ; vérifier style actif et widgets natifs. |
| **UX-29 · P2 · C** | Vitesse maintenant disponible, mais compteur = indice de commande (`MW:7356-7362`, `7405`, `7417-7418`), pas nombre de pénétrations ; navigation dédiée par couleur/commande absente des contrôles construits à `7249-7298`. | Nommer l'unité de progression, pas avant/arrière et par bloc de couleur. Séquence contenant sauts/coupes : compteur non ambigu et inspection locale possible à vitesse choisie. |
| **UX-30 · P2 · C** | Préférences encore réparties entre menus IA, thème/densité/navigation/accroche ; autosave fixé à 120 s (`MW:642`, construction des menus). | Regrouper les préférences sans retirer les accès rapides ; rendre cadre/défauts/dossiers/autosave compréhensibles. Pouces/anglais restent des évolutions produit, pas régressions du stockage µm. |
| **UX-31 · P2 · C/V** | Configuration IA encore technique ; OK accepte une IA activée avec chemins vides, diagnostic seulement dans le test facultatif (`ai_preferences_dialog.cpp:45-104`, `131-150`, `215-220`). Scores/protection sont désormais expliqués dans la revue. | Parcours de détection/test guidé, paramètres techniques dans Avancé ; validation adaptée au runtime ou confirmation d'enregistrement incomplet. IA activée et chemins manquants → cause/action visible avant acceptation. |
| **UX-32 · P2 · C** | « Enregistré à » utilise l'heure courante de rafraîchissement du label, pas un instant mémorisé de sauvegarde (`main_window_ux.cpp:260-278`). Revenir propre par undo peut ainsi afficher l'heure de l'undo comme heure de sauvegarde. | Stocker l'instant réel, distinguer fichier enregistré / modifications non enregistrées / autosave et échec. Enregistrer à T1, éditer puis undo à T2 : afficher T1, pas T2. |
| **UX-41 · P2 · C/V** | Règle focus des boutons précédant la bordure `[primary="true"]` de spécificité équivalente (`app_theme.cpp:141`, `259-266`) ; bouton d'accueil concerné (`empty_state_widget.cpp:49-52`). | État `primary:focus` distinct après les états principaux ; focus reconnaissable sur chaque bouton d'accueil avec Tab, clair/sombre. |
| **UX-42 · P2 · C/V** | Certains noms/états accessibles sont incomplets ou génériques : trois « Parcourir… » IA sans contexte (`ai_preferences_dialog.cpp:25-34`), libellés IA juxtaposés, état Workflow dans label séparé (`workflow_panel.cpp:90-120`), instructions du guide renommées « Description de l'étape N » sans prose accessible explicite (`help_dialogs.cpp:413-415`). | Buddies/noms contextualisés, état d'étape annoncé, instructions complètes exposées. Inspection `QAccessible`/UIA puis NVDA ; ne pas conclure à un échec réel avant cette preuve. |
| **UX-43 · P2 · C/V** | Accueil : marges, textes non repliables, largeur minimale 260 des boutons et cinq récents sans élision/scroll (`empty_state_widget.cpp:28-82`). Dialogue texte : environ quinze lignes, deux colonnes et pas de scroll (`text_dialog.cpp:208-248`). Le scroll inspecteur ne résout pas ces surfaces. | Accueil/dialogues adaptables ; récents élidés avec chemin complet, débordement vertical maîtrisé, entrée dessin découvrable. Fenêtre 1280 × 720 et DPI 150/200 %, cinq noms longs : toutes les actions atteignables et canevas utilisable. |
| **UX-44 · P2 · C/V** | Aide Gestes : texte repliable sans recalcul explicite de hauteur de ligne au redimensionnement/filtrage ; recherche/préréglage sur une ligne (`help_dialogs.cpp:143-188`, `279-280`). | Layout adaptable et hauteurs actualisées ; à 460 × 320, instructions entièrement consultables, Tab sortant toujours du tableau. |
| **UX-45 · P2 · C/V** | `DocumentPanel::refresh` détruit l'arbre et rouvre chaque groupe (`document_panel.cpp:349-350`, `397-401`), sans conservation explicite de l'ancrage de scroll. | Restaurer expansion/sélection/position par identité ; fermer un groupe et rafraîchir le même document ne change pas son repli ni la position de lecture. |
| **UX-46 · P2 · C/V** | Barre Fils peinte et activée à la souris seulement : nom accessible du conteneur, pas de focus/activation clavier des pastilles ; excédent affiché en « +N », non actionnable (`thread_strip.cpp:25-31`, `55-71`, `111-135`). Le panneau Fils fournit un autre accès, mais la barre ne l'indique pas pour le débordement. | Navigation/focus des fils ou boutons natifs ; « +N » ouvre la liste complète. Tous les fils sélectionnables au clavier et au-delà de la largeur disponible, avec nom/code annoncés plutôt que couleur seule. |

## 6. Mesures de contraste après refonte

Calcul indépendant sur les nouveaux tokens `design_tokens.cpp`, avec luminance relative sRGB. Pour chaque composante normalisée `c` : `c/12,92` si `c ≤ 0,04045`, sinon `((c + 0,055)/1,055)^2,4`. `L = 0,2126 R + 0,7152 G + 0,0722 B` ; ratio `(Lmax + 0,05)/(Lmin + 0,05)`. Alpha composé avec le fond avant calcul.

| Usage | Couleurs / domaine | Résultat |
|---|---|---:|
| Texte principal clair | `#2B2733` sur fenêtre/surface/surface relevée | minimum **11,960:1** |
| Texte secondaire clair | `#625A6E` sur ces trois fonds | minimum **5,373:1** |
| Texte principal sombre | `#ECEAFB` sur fenêtre/surface/surface relevée | minimum **12,470:1** |
| Texte secondaire sombre | `#A5A1CC` sur ces trois fonds | minimum **6,028:1** |
| Couleurs d'état utilisées comme texte | succès/avertissement/erreur/info, trois fonds clairs | minimum **4,860:1** |
| Couleurs d'état utilisées comme texte | mêmes usages, trois fonds sombres | minimum **6,470:1** |
| Bordure forte clair / sombre | trois fonds de chaque thème | minima **3,330:1 / 3,056:1** |
| Sélection normale claire | `#FFFFFF / #4B3FD1` | **7,165:1** |
| Sélection normale sombre | `#1B0E0A / #FF7A59` | **7,344:1** |
| Sélection survolée claire | `#FFFFFF / #E4E1FA` | **1,277:1** |
| Sélection survolée sombre | `#1B0E0A / #3A2A4A` | **1,445:1** |
| Grille sombre dans le cadre blanc | blanc alpha 28/255 sur blanc | **1,000:1** |

**Bonne base de palette ; défauts localisés de composition d'états et de fond du canevas.** Les anciennes mesures d'icônes fixes et d'avertissement clair ne sont plus applicables. Les ratios théoriques des tokens `canvasStitch`/`canvasJump` sombres sur blanc sont 1,628:1 / 2,602:1 : contrôler leurs usages effectifs sans confondre ces tokens avec les couleurs métier des fils.

Repères WCAG utilisés comme guide pour le desktop : 4,5:1 pour texte courant, 3:1 pour information graphique nécessaire. Aucun statut de conformité déduit de cette seule table. Une bordure décorative peu contrastée n'est pas automatiquement une violation ; couleurs de focus correctes ne prouvent pas présence/visibilité du focus sur tous les contrôles. Netteté HiDPI des chemins `QIcon::paint`/`pixmap` reste à observer sous Qt.

## 7. Plan de remédiation proposé

| Lot | Priorité | IDs | Résultat et preuve attendus |
|---|---|---|---|
| **0 — Rendre l'audit testable** | Préalable | C3493 | Build desktop, 21 suites exécutables, captures de la même référence ; ne pas maquiller un échec avec un ancien binaire. |
| **1 — Cohérence du document** | P0/P1 | 02, 06, 07, 08, 13 | SVG/dessin/texte et machine seuls utilisables ; dimensions exactes ; conversion actuelle sans effet ; tests de parcours. |
| **2 — Réactivité et contrat IA** | P0/P1 | 04, 15, 16, 35, 36 | UI réactive, annulation atomique, requêtes/réglages figés, identités/protection stables, avertissements conservés. |
| **3 — Réglages et production** | P1/P2 | 10, 14, 25, 26, 29, 32, 33, 34, 45 | Portée honnête, valeurs multiples visibles, même analyse pré-export, états/replis/heure réels. |
| **4 — Accès et design system** | P1/P2 | 17, 28, 37, 38, 39, 40, 41, 42, 43, 44, 46 | États visuels lisibles, clavier fiable, cibles correctes, petits écrans/DPI et annonces contrôlés. |
| **5 — Préférences et prise en main** | P2 | 30, 31 | Configuration accessible aux non-développeurs, validation honnête et paramètres regroupés. |

Ordre : build, parcours sans image et incohérences documentaires, puis calculs et production. Les petits correctifs d'états QSS/clavier peuvent être isolés du chantier asynchrone. **Ne pas refaire les corrections déjà présentes ni lancer une nouvelle décoration globale pour résoudre des erreurs de contrat.**

Garde-fous : mutations par `ICommand`, un geste = historique compréhensible, séquence effective commune aux consommateurs, snapshots et tests de déterminisme pour les tâches. Un nouveau champ persistant exige la checklist `openstitch-stitch-param` ; cet audit n'en ajoute pas. Pas d'estimation d'effort « quelques minutes » pour les changements transversaux non investigués.

## 8. Preuves restantes et protocole de validation

### Automatisation après rétablissement du build

Exécuter les 21 suites en Release via CTest avec `QT_QPA_PLATFORM=offscreen`/PATH Qt fournis par le dépôt, puis Debug selon le périmètre des correctifs. Les tests de composants sont utiles mais ne prouvent pas l'intégration complète ; la présence d'un nom de test n'est pas un succès d'exécution.

| Famille | Tests de non-régression attendus |
|---|---|
| Document / sans raster | Fixtures SVG, dessin, texte et OSP vectoriel → couture → séquence → analyse/export ; suppression et undo/redo ; fixture machine seule → actions/OSP/garde/état modifié. |
| Sécurité déjà corrigée | Toutes les ouvertures : Enregistrer/Ne pas enregistrer/Annuler et écriture en échec ; récupération multiple et candidate corrompue conservée. |
| Dimensions / conversion | Import non proportionnel exact ou option explicitement interdite ; recliquer type actif sans modifier paramètres/groupe/historique. |
| Inspecteur / groupe | Valeurs mixtes, portée de chaque contrôle, seules valeurs touchées uniformisées ; undo/redo et édition externe resynchronisés sans commande parasite. |
| Analyse / export | Même ensemble de règles, lettrage inclus ; politique explicite de dépassement/erreur/avertissement ; aucune analyse périmée présentée comme courante. |
| IA | Identités non éditables ; protection héritée ; annulation de chaque phase et événements tardifs ; paramètres figés ; repli couleur consultable après fermeture. |
| Réactivité | Événement UI traité pendant le travail, Annuler sans mutation, résultat obsolète ignoré, sortie byte-identique. |
| Interaction / accès | Espace selon le focus, hit-test aux bords de cible, noms/états accessibles, clavier et débordement de la barre Fils, gestes d'aide correspondant à un effet. |

QTest headless : pas de `sleep`, pas de tests pixel introduits dans la CI. Captures manuelles = pièces de revue visuelle, pas une preuve automatisée de conformité.

### Matrice visuelle et d'usage — non exécutée pendant cet audit

| Dimension | Cas | Critères |
|---|---|---|
| Écran / DPI | 1920 × 1080 et 1280 × 720 ; 100/150/200 % ; changement d'écran | Place utile du canevas, overflow, dialogues dans la zone de travail, netteté. |
| Apparence | Clair/sombre × Confortable/Compact, changement à chaud | Sélection + survol, focus/checked/disabled, grille, alertes, accueil et surfaces natives. |
| Données | Vide, raster, SVG, dessin, texte, machine seul ; grands groupes/nuanciers | Pas de source oubliée, fausse étape, erreur de portée ou état de sauvegarde trompeur. |
| Périphériques | Souris avec/sans clic molette, pavé tactile réel | Pan/zoom/pinch/accroche, chevauchement des poignées ; événements synthétiques ≠ pilote réel. |
| Clavier | Tab/Maj+Tab, Espace, Entrée/Échap/Suppr, saisie et retour du focus | Aucun raccourci détournant le contrôle focalisé ; parcours complet et focus visible. |
| Accessibilité | `QAccessible`/UIA puis NVDA Windows | Nom, valeur, unité, état, instructions, indisponibilité et annonce de fin/erreur. |
| Texte / locale | Français Qt réellement déployé, virgule/point, police 150/200 %, noms longs | Pas de langue mélangée/troncature critique/chiffre trompeur ; règles et labels lisibles. |
| Erreurs | Fichier absent/corrompu, écriture refusée, export hors cadre, IA absente | Document préservé, cause claire, action de reprise et information persistante. |
| Performance | Images et motifs de référence petits/denses ; génération, validation IA, rendu, autosave | Temps mesurés, événements UI, annulation, dernière prévisualisation stable. |
| Production réelle | PDF/impression, contrôle couleur et ordre, broderie sur échantillon | Lisibilité multi-page, avertissements, distinction aperçu écran/résultat physique. |

### Étude utilisateurs recommandée

Protocole formatif avec environ cinq personnes débutantes/expérimentées en broderie, sans prétention statistique. Tâches : premier motif image, SVG direct, texte, réglage de taille/densité, correction d'avertissement, récupération et fichier machine. Mesurer réussite sans aide, erreurs/récupérations, hésitations, temps et compréhension des termes. Comparer sur les mêmes fixtures ; préférence esthétique et réussite de tâche sont deux mesures différentes.

## 9. Livraison

Revue large des surfaces, causes vérifiables, mesures de palette, priorités et critères de validation livrés. **La partie dynamique/visuelle reste bloquée par C3493 ; les essais utilisateurs et l'accessibilité assistée restent à faire.** Ce rapport est une base de travail actuelle pour le snapshot indiqué, pas une promesse « toute l'UX est validée ».

Aucun C++/CMake, moteur, interface ou statut de roadmap modifié par ce lot ; agents en lecture seule. Annexes dans `docs/`, hors `docs/source/` : pas de changement au PDF éditorial. Rapport antérieur archivé explicitement, lien de l'audit historique conservé. Aucun commit effectué ; travail concurrent laissé intact.
