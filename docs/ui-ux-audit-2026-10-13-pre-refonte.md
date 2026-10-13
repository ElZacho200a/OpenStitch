# Archive : audit UX/UI avant fusion de la refonte — 2026-10-13

*État : historique, référence `722ec3ade85f3fcaf8e41c30962b34268ed2ca36`. Ce rapport a été remplacé après la fusion de la refonte ; plusieurs constats et toutes les mesures de couleurs ci-dessous ne décrivent plus la nouvelle interface. Ne pas l'utiliser comme liste de travaux actuelle.*

Voir l'[audit réconcilié après refonte](ui-ux-audit-2026-10-13.md), référencé à `fb0045547de789877c693334139a81f267136683`. Le résultat de compilation ci-dessous concerne uniquement l'ancienne référence. Aucun correctif d'interface n'a été appliqué par cet audit.

## 1. Synthèse

**Le principal enjeu n'est pas une nouvelle décoration : c'est la confiance dans le document et dans ce que l'interface annonce.** Les fondations existent : inspecteur, thèmes, accueil, commandes annulables, outils de dessin, navigation souris/pavé tactile et aide non modale. Mais plusieurs parcours peuvent perdre un travail, ne pas générer les points attendus, ou présenter des informations périmées.

L'audit recense **32 constats : 4 P0, 17 P1 et 11 P2**, plus un blocage de compilation indépendant. Les priorités proposées sont :

1. Protéger tous les remplacements de document et les récupérations autosave.
2. Rendre fiables les parcours SVG/dessin sans image, l'inspecteur et l'analyse.
3. Garder l'application réactive pendant les calculs.
4. Réduire la charge cognitive : parcours actionnable, inspecteur organisé, messages persistants.
5. Corriger contrastes, accès clavier, petits écrans et incohérences visuelles.

**Ce rapport n'est pas une certification d'accessibilité ni une étude utilisateurs.** Aucun score global arbitraire n'est attribué. Le rendu réel Windows, les dimensions effectives des widgets, les performances et les lecteurs d'écran restent à valider suivant la matrice de la section 8.

## 2. Périmètre, méthode et limites

### Référence examinée

- Commit : `722ec3ade85f3fcaf8e41c30962b34268ed2ca36`.
- Arbre de travail à l'entrée : modifications préexistantes dans `apps/cli/CMakeLists.txt`, `apps/cli/main.cpp` et `aqtinstall.log`, laissées intactes.
- Application : C++ / Qt 6 Widgets, desktop Windows ; pas une application web.
- Sources examinées : construction et actions de `MainWindow`, canevas et table des gestes, inspecteur, document, workflow, accueil, import, options de génération, simulation, analyse/export, fichiers récents/autosave, thèmes/icônes/règles, dialogues d'aide et d'IA, suites QTest associées. Vérifications ciblées dans `document`, `commands` et `image` pour suivre certains contrats.
- Documentation confrontée : `docs/ui-redesign-audit.md`, `docs/ui-redesign-specification.md`, `docs/ui-audit-2026-10.md`, sections UX/I18N/PERF de `docs/roadmap-parite-hatch.md`.
- `mesh-mcp/search_docs` indisponible dans cette session : repli sur recherche de sections et lecture ciblée, sans reprendre les anciens constats comme vérité actuelle.

### Lecture des constats

- **C — Code** : chemin ou absence de mécanisme vérifiable dans les sources. Le scénario décrit n'a pas été exécuté dans l'application durant cet audit.
- **M — Mesure** : calcul reproductible sur les couleurs déclarées, pas mesure d'une capture d'écran.
- **V — À valider** : risque ergonomique ou visuel déduit de la composition ; intensité à confirmer sur le bureau réel.
- **P0** : risque de perte de travail, parcours essentiel bloqué ou réactivité critique.
- **P1** : information trompeuse, erreur de réglage/production ou obstacle d'accès important.
- **P2** : friction, cohérence, découvrabilité ou finition.

Les références de lignes sont celles de cet état du dépôt. `MW` désigne ci-dessous **`apps/desktop/main_window.cpp`**.

### Vérification dynamique tentée

Compilation Release ciblée sur les **15 suites desktop** déclarées dans `tests/unit/desktop/CMakeLists.txt` :

```powershell
cmake --build --preset msvc-release --target test_canvas_view test_node_handle test_document_panel test_properties_panel test_main_window test_ui_invariants test_ui_adversarial test_ui_characterization test_canvas_input test_interaction_map test_help_dialogs test_selection_hit_test test_wsl_path_converter test_generation_options test_sam_worker_client
```

Résultat : **échec de compilation avant exécution des suites**, avec MSVC :

```text
apps/desktop/main_window.cpp(5056,65): error C3493:
impossible de capturer implicitement 'kFlattenTolerance'
car aucun mode de capture par défaut n'a été spécifié
```

La lambda de `offsetVectorObjectCore` est déclarée avec `[]` et utilise la constante locale `kFlattenTolerance` (`MW:5054-5056`). Ce blocage a été observé pendant l'audit, pas corrigé. **Aucun résultat QTest, aucune capture et aucune mesure de fluidité ne sont revendiqués.** Le rétablissement de ce build est un préalable à la validation, pas une refonte UX.

## 3. Acquis à conserver et anciens constats à ne pas recycler

| Acquis actuel | Preuve / nuance |
|---|---|
| Deux thèmes et deux densités centralisés | `apps/desktop/design_tokens.cpp`, `apps/desktop/app_theme.cpp` ; couverture de style encore partielle. |
| Accueil avec import image/SVG, projet et DST, plus récents | `apps/desktop/empty_state_widget.cpp:25-75`. Ce n'est plus un canevas sans porte d'entrée. |
| Paramètres éditables après création | `apps/desktop/properties_panel.cpp:140-464`. L'ancien constat « aucun inspecteur » est caduc. |
| Sélection multiple, Suppr universel, déplacement, duplication | `MW:7556-7835`, suites `test_main_window` et `test_ui_adversarial` ; cela ne prouve pas tous les parcours de génération. |
| Panneaux réouvrables et mode canevas préservé | `MW:411-414`, `897-925`, `6198-6223`. L'ancien défaut des docks inaccessibles a un correctif dans le code. |
| Conflit historique sur G supprimé | Polygone régulier = G ; satin = Maj+E (`MW:787`, `5857-5859`). Les tests d'invariants existent, non exécutés ici. |
| Panoramique, sélection fenêtre/croisement, pavé tactile, survol | `apps/desktop/canvas_view.cpp`, `apps/desktop/interaction_map.cpp`, suites dédiées ; le constat « uniquement bouton gauche » est caduc. |
| Halo de sélection et zone de poignée élargie | `MW:2518-2526`, `apps/desktop/node_handle.hpp:31-55` ; la zone reste de 14 px logiques, pas une cible de 24 px. |
| Aide recherchable, générée, non modale | `apps/desktop/help_dialogs.cpp:130-297`, guide de prise en main à partir de la ligne 299. |
| Enregistrement courant, récents et autosave | `MW:6884-7072` ; couverture des situations encore incomplète. |
| Garde de fermeture/Nouveau et résumé DST | `MW:994-1027`, `1452-1470`, `7110-7172` ; ne protège pas toutes les ouvertures. |
| Diagnostic développeur masqué par défaut | `MW:6193-6195`, `4902-4907`. |
| Noms d'annulation contextualisés | `MW:8012-8019`. Ne pas proposer à nouveau cette fonctionnalité comme absente. |

Les audits antérieurs restent utiles comme historique. Les statuts de roadmap sur la navigation, le survol et certains libellés méritent une réconciliation séparée ; ils ne sont pas modifiés par ce rapport.

## 4. Audit des parcours

| Parcours | Ce qui aide | Principal obstacle actuel |
|---|---|---|
| Premier lancement → premier motif | Accueil, guide, outils visibles | Le panneau Workflow renvoie vers les menus au lieu de lancer l'étape ; le dessin sans import n'est pas une entrée de l'accueil. |
| Image → taille → segmentation → broderie | Aperçu d'import, unités, avertissement de cadre | Hauteur libre non appliquée ; opérations synchrones ; plusieurs dialogues successifs. |
| SVG → broderie | Import direct et auto-numérisation des vecteurs | Sortie prématurée du rafraîchissement sans image ; vecteurs absents du panneau Document. |
| Dessin manuel → contour/remplissage | Formes, plume, déplacement et redimensionnement | Même dépendance au raster pour le rafraîchissement ; paramètres de création demandés avant de voir le résultat. |
| Réglage → aperçu → annuler | Inspecteur et commandes annulables | Formulaire non resynchronisé quand l'objet conserve son identifiant ; génération sur chaque changement de valeur. |
| Analyse → correction → export | Liste de problèmes, localisation, résumé | Analyse non invalidée après édition ; export sans réanalyse intégrée ni action de correction. |
| DST importé → sauvegarde/récupération | Import et séquence source dans le document | Plusieurs gardes de contenu ignorent `imported_design`. |
| Crash → récupération | Instantanés et proposition au démarrage | Plusieurs récupérations successives peuvent remplacer la précédente ; suppression inconditionnelle des créneaux traités. |
| IA → revue → validation | Profils, prévisualisations, garder/protéger, progression | Annulation inopérante au démarrage ; « Segmenter » numérise aussi ; un avertissement peut disparaître à la fermeture. |
| Expert clavier / pavé tactile | Gestes centralisés, raccourcis, aide | Les opérations fines de la scène n'ont pas d'alternative clavier/numerique homogène. |

## 5. Registre des constats

### 5.1 Sécurité du travail et réactivité

#### UX-01 — Remplacements de document sans garde de sauvegarde — P0 · C

- **Preuve :** `openImage/openSvg` remplacent `project_` sans `confirmDiscardChanges` (`MW:1030-1116`). `loadProject/openProjectFile` font de même (`6934-6965`). L'import DST propose seulement « Continuer ? », sans choix d'enregistrement (`7175-7202`). Les récents, Nouveau et Quitter ont, eux, la garde commune.
- **Impact :** un utilisateur peut perdre ses modifications en ouvrant normalement un autre document ; `resetDocumentState` efface aussi la pile d'annulation et abandonne le créneau autosave précédent.
- **Scénario à reproduire :** modifier A, ouvrir B depuis le menu/la barre, puis chercher à récupérer A par Ctrl+Z.
- **Recommandation :** un contrat unique pour tout remplacement : Enregistrer / Ne pas enregistrer / Annuler. Une sauvegarde annulée ou en échec interdit le remplacement ; une ouverture de B en échec conserve A.
- **Acceptation :** tests paramétrés image/SVG/OSP/DST/récents, couvrant les trois décisions et l'échec d'enregistrement.

#### UX-02 — Le parcours vectoriel sans image ne régénère pas les points — P0 · C

- **Preuve :** `refreshImage` retourne avant `sequence_.reset`, `refresh_context`, la simulation et plusieurs panneaux dès que le projet n'a ni image ni design importé (`MW:2401-2405` ; génération à `2439-2477`). Un SVG ou un motif dessiné possède des vecteurs/broderies, mais pas ces deux sources.
- **Impact :** création/édition de broderie sans raster ne met pas à jour les points et peut laisser l'export indisponible ; une ancienne séquence peut aussi survivre à cette sortie précoce.
- **Recommandation :** dissocier le traitement facultatif de l'image et le rafraîchissement obligatoire du document/de la séquence.
- **Acceptation :** SVG puis contour/tatami, rectangle dessiné puis remplissage, OSP purement vectoriel, suppression/undo/redo : aperçu, compteurs et export suivent toujours le modèle sans ajouter une image factice.

#### UX-03 — La récupération peut remplacer puis supprimer un travail récupéré — P0 · C

- **Preuve :** `checkAutosaveRecovery` parcourt tous les candidats ; chaque « Récupérer » appelle `applyLoadedProject` sans protéger la récupération précédente (`MW:7039-7067`). `discardAutosave(candidate.slot)` s'exécute ensuite même après un chargement en échec (`7069-7071`).
- **Impact :** récupérer A puis B remplace A dans la même fenêtre alors que son instantané a été supprimé. Une récupération échouée supprime également sa source de secours.
- **Recommandation :** sélection explicite d'un candidat, ouverture distincte ou garde entre récupérations ; conserver les candidats non récupérés et ceux en échec. Distinguer « Plus tard » de « Supprimer cette sauvegarde ».
- **Acceptation :** deux candidats A/B restent accessibles ; échec de chargement = instantané conservé ; aucune récupération n'est perdue sans décision explicite.

#### UX-04 — Calculs classiques longs sur le thread de l'interface — P0 · C

- **Preuve :** segmentation, vectorisation et auto-numérisation sont appelées directement entre curseurs d'attente (`MW:3399-3405`, `3464-3468`, `3796-3806`, `3883-3887`). `refresh_context` et autosave sont aussi synchrones (`2456`, `7025`).
- **Impact :** impossibilité de naviguer, comprendre l'avancement ou annuler pendant les calculs lourds ; confusion avec un crash. Aucune durée n'a été mesurée ici.
- **Nuance :** l'IA dispose déjà d'un worker, de messages de progression et d'un bouton d'annulation. Le constat n'est donc pas « aucune infrastructure asynchrone nulle part ».
- **Recommandation :** tâches sur instantanés immuables, progression, annulation et application atomique par commande. Rendre visible la dernière prévisualisation valide pendant le calcul.
- **Acceptation :** événement UI traité pendant l'opération ; Annuler ne modifie pas le document ; résultat obsolète rejeté si le document change ; sortie déterministe identique au calcul synchrone.

### 5.2 Exactitude des réglages et des retours

#### UX-05 — Inspecteur périmé après undo, conversion ou édition externe — P1 · C

- **Preuve :** `updateInspector` ne vérifie que `kind/id` pour reconstruire (`MW:6324-6327`), pas le type ni les paramètres. Les callbacks capturent un état initial (`properties_panel.cpp:195`, `255`, `374`). `ConvertFillGroupCommand` conserve l'identifiant de l'ancre (`libs/commands/include/openstitch/commands/project_commands.hpp:793-797`).
- **Impact :** après 3 → 5 mm puis Ctrl+Z, le formulaire peut encore afficher 5 mm ; après changement de type, il peut proposer les champs du type précédent. Un nouvel édit réémet ces valeurs périmées. « Autre tirage » calcule également sa graine depuis `dbase` : des clics successifs sans reconstruction repartent du même état (`properties_panel.cpp:325-370`).
- **Recommandation :** resynchroniser les valeurs sous blocage de signaux ; reconstruire seulement si le schéma change, en préservant focus et saisie. Les paramètres non édités doivent venir de l'état courant.
- **Acceptation :** undo/redo, angle changé au canevas, conversion et deux tirages successifs donnent des contrôles conformes au document, sans commande parasite.

#### UX-06 — Cliquer le type déjà actif réinitialise des réglages — P1 · C

- **Preuve :** les boutons du type courant sont cliquables (`MW:5729-5735`) ; `setStitchType` ne vérifie pas l'égalité de type et construit des paramètres par défaut (`4717-4735`), puis convertit le groupe (`4823-4825`).
- **Impact :** un clic de confirmation sur « Tatami » peut réinitialiser densité, angle et sous-couches. Sur un groupe satin, une conversion inutile peut également retirer des sections sœurs.
- **Recommandation :** type actuel = no-op ; réinitialiser devient une action séparée et explicite. Pour les conversions réelles, conserver les réglages compatibles et expliquer les pertes.
- **Acceptation :** recliquer le type ne modifie ni paramètres, ni groupe, ni historique undo.

#### UX-07 — Hauteur d'import libre affichée, mais non appliquée — P1 · C

- **Preuve :** l'import autorise largeur/hauteur indépendantes (`import_dialog.cpp:49-62`, `135-141`). `openImage` ne conserve que `mm_per_pixel` calculé depuis la largeur (`MW:1068-1070` ; `libs/document/src/image_placement.cpp:67-71`). Le rendu utilise cette même échelle pour X et Y (`MW:2501-2505`).
- **Scénario :** image carrée, proportions décochées, largeur 80 mm et hauteur 40 mm : le chemin actuel affiche 80 × 80 mm.
- **Impact :** dimensions physiques annoncées et résultat divergents, y compris avertissement de dépassement du cadre.
- **Recommandation :** soit appliquer réellement une transformation non uniforme dans tout le pipeline, soit interdire honnêtement la hauteur indépendante tant que le modèle ne la supporte pas.
- **Acceptation :** dimensions saisies, règle/canevas, segmentation et sortie machine concordent ; proportions cochées restent exactes aux bornes de saisie.

#### UX-08 — Un DST seul n'est pas reconnu partout comme document — P1 · C

- **Preuve :** `updateEmptyState` inclut `imported_design` (`MW:1446-1447`), contrairement à `hasDocument` pour les actions de sauvegarde (`7890-7896`), à `saveProjectAs` (`6894-6899`) et au test de vacuité d'autosave (`7017-7023`). L'action d'affichage des points est activée uniquement par les objets de broderie (`7874`).
- **Impact :** document visible, mais sauvegarde `.osp` indisponible/refusée, pas d'autosave d'un design seul et contrôle d'affichage grisé. La garde de fermeture proposant Enregistrer ne peut pas réussir pour ce contenu seul.
- **Recommandation :** définition de contenu partagée qui couvre image, vecteurs, broderies et design importé ; activation de l'affichage par présence de séquence pertinente.
- **Acceptation :** importer un DST seul, enregistrer/réouvrir l'OSP, autosauvegarder et fermer avec Enregistrer fonctionnent sans créer d'objet artificiel.

#### UX-09 — L'analyse reste visible après modification ou changement de document — P1 · C

- **Preuve :** la liste est vidée uniquement dans `runAnalysis` (`MW:6411`). Ni `refreshImage` ni `resetDocumentState` ne l'invalident ; elle peut conserver « Aucun problème détecté » d'un autre état.
- **Impact :** faux sentiment de validation, résultats ou localisations appartenant à l'ancien motif.
- **Recommandation :** état explicite « Analyse à relancer » après toute modification pertinente, lié à une révision de document ; conserver éventuellement les anciens résultats en les signalant comme périmés.
- **Acceptation :** analyser A puis modifier le cadre ou charger B ne laisse jamais un résultat présenté comme actuel.

#### UX-10 — Export : avertissement présent, décision insuffisamment sécurisée — P1 · C

- **Preuve :** `exportDst` montre dimensions et dépassement, mais ne lance pas `stitch_analysis::analyze` (`MW:7110-7165`). Même hors cadre, le bouton par défaut est Ok (`7142-7155`). Le rappel `.osp` n'a pas d'action d'enregistrement.
- **Impact :** la touche Entrée peut confirmer un dépassement ; l'utilisateur ne peut pas corriger/sauvegarder directement depuis le contrôle pré-export. Le texte ne précise pas que le DST ne conserve pas les couleurs réelles, malgré le commentaire de code (`7160`).
- **Recommandation :** revue pré-export sur la révision actuelle, bouton « Exporter », choix prudent en présence de risques, liens vers cadre/analyse et « Enregistrer le projet ». Distinguer clairement OSP éditable et fichier machine, avec limites des couleurs.
- **Acceptation :** export hors cadre ou avec problèmes = confirmation explicite distincte d'un export normal ; aucune déclaration « vérifié » sur une analyse périmée.

#### UX-11 — Bornes de l'inspecteur incohérentes avec la création — P1 · C

- **Preuve :** `mmSpin` accepte zéro partout (`properties_panel.cpp:84-91`), y compris espacement/longueur/densité. La création tatami impose au contraire 0,1 mm minimum d'espacement et 1 mm de longueur (`MW:3561-3568`).
- **Impact :** réglages physiquement incohérents ou extrêmement denses possibles dans l'inspecteur ; comportements de repli du moteur non expliqués. Les champs réellement autorisés à zéro, comme certaines compensations, ne doivent pas être interdits globalement.
- **Recommandation :** contraintes par paramètre alignées avec le contrat métier, validation des relations entre champs et explication des valeurs limites ; aucun clamp invisible présenté comme la valeur réellement utilisée.
- **Acceptation :** zéro autorisé seulement quand il a un sens ; mêmes limites à la création et à l'édition ; un message indique comment corriger une valeur refusée.

### 5.3 Organisation du travail et parcours guidé

#### UX-12 — Pas de défilement interne pour les formulaires longs — P1 · C/V

- **Preuve :** `PropertiesPanel` est placé directement dans son dock (`MW:6253-6255`) ; le satin et le directionnel ont de nombreux champs dans un formulaire non repliable, sans `QScrollArea` (`properties_panel.cpp:253-419`). La liste de couleurs des filtres est également un layout sans scroll (`MW:6637-6644`).
- **Impact à valider :** fenêtre qui impose une grande taille minimale ou champs hors zone utile sur portable/écran agrandi ; densité visuelle élevée. `resize(1100, 800)` n'est pas une garantie de compatibilité 1100 × 800.
- **Recommandation :** scroll interne, sections repliables, dimensions minimales contrôlées et layout adapté aux petits écrans. Vérifier aussi le dialogue IA avec ses deux aperçus et ses réglages.
- **Acceptation :** tous les champs accessibles à 1280 × 720, à 150/200 % et en texte agrandi ; canevas encore utilisable ; aucun champ perdu après tabulation.

#### UX-13 — Les vecteurs sans broderie sont absents du panneau Document — P1 · C

- **Preuve :** `DocumentPanel::refresh` construit « Objets » depuis `project.embroidery_objects` seulement (`document_panel.cpp:134-166`). Son dock s'affiche automatiquement sur image ou broderies, pas sur vecteurs seuls (`MW:6231`).
- **Impact :** SVG et formes non numérisées n'ont pas d'inventaire clavier pour les retrouver ; ambiguïté entre « forme » et « objet de broderie ». Un groupe satin est réouvert systématiquement à chaque reconstruction (`document_panel.cpp:106-155`).
- **Recommandation :** montrer les vecteurs et leurs broderies attachées dans une structure explicite ; conserver expansion, sélection et position de défilement par identité.
- **Acceptation :** un SVG de plusieurs formes est entièrement visible et sélectionnable dans Document avant génération ; rafraîchir ne déplie pas les groupes fermés par l'utilisateur.

#### UX-14 — Workflow cliquable mais non actionnable, états incomplets — P1 · C

- **Preuve :** un clic affiche seulement une indication pendant six secondes (`MW:6123-6132`). Les étapes Image/Régions restent conditionnées à la présence d'une image ; Vérification/Export ne passent jamais à Done dans `refreshWorkflow` (`6136-6161`).
- **Impact :** un bouton promet une action mais renvoie au menu ; les parcours SVG, dessin et DST ne correspondent pas à la progression affichée ; impossible de lire ce qui est réellement vérifié ou exporté.
- **Recommandation :** adapter les étapes à la source et déclencher les actions disponibles ; distinguer disponibilité et validation, avec une action principale « Suite : … » et une raison pour les étapes inaccessibles.
- **Acceptation :** cliquer une étape disponible réalise l'action annoncée ; SVG ne réclame pas de segmentation ; l'édition invalide la vérification précédente.

#### UX-15 — « Segmenter avec l'IA » numérise aussi et masque un repli — P1 · C

- **Preuve :** après validation des masques, `segmentWithAi` appelle directement `auto_digitize` et ajoute des broderies (`MW:3874-3905`). Si la division par couleur échoue, le dialogue pose un message puis appelle immédiatement `accept()` (`ai_segmentation_dialog.cpp:610-626`).
- **Impact :** action plus large que son nom ; l'utilisateur croit avoir simplement segmenté. Le message de repli vers les formes IA peut disparaître avant lecture alors que le résultat diffère du choix demandé.
- **Recommandation :** nommer le résultat « Détecter les formes et créer la broderie », ou séparer les deux décisions ; transmettre les avertissements et le rapport de nettoyage au résultat persistant de la fenêtre principale.
- **Acceptation :** destination explicitement annoncée avant Valider ; repli couleur consultable après fermeture ; aucune double numérisation involontaire induite par le workflow.

#### UX-16 — Annulation IA disponible avant qu'elle puisse agir — P1 · C

- **Preuve :** `cancelButton` est activé au début d'`onAnalyzeClicked` (`ai_segmentation_dialog.cpp:205-208`). Pendant `StartingWorker`, aucun identifiant de requête n'est encore créé (`278-280`) ; `onCancelClicked` ne fait rien s'il est vide (`284-289`). Les réglages restent éditables pendant l'opération et certains sont lus à une phase ultérieure (`325-331`).
- **Impact :** premier lancement apparemment impossible à annuler ; résultat pouvant combiner des réglages pris à des moments différents.
- **Recommandation :** annuler chaque phase, y compris préparation/démarrage, et capturer les réglages au lancement ; rendre visible l'état « Annulation… » jusqu'à son aboutissement.
- **Acceptation :** annulation testée au démarrage, chargement et inférence ; aucune phase ultérieure ne redémarre une analyse annulée ; configuration du résultat identique à celle du lancement.

### 5.4 Accessibilité et localisation

#### UX-17 — Édition fine du canevas sans alternative clavier homogène — P1 · C/V

- **Preuve :** canevas nommé et focusable (`MW:424-430`, `canvas_view.cpp:165-168`), mais poignées custom `QGraphicsEllipseItem/QGraphicsRectItem` déplacées par événements souris (`node_handle.hpp:35-99`, `189-213`). L'inspecteur vectoriel affiche des informations, pas des coordonnées/dimensions éditables (`MW:6339-6354`).
- **Impact :** déplacer un objet au clavier est possible, mais pas régler de manière homogène un nœud, rail, guide ou redimensionnement ; nommer le canevas ne rend pas ces opérations accessibles.
- **Nuance :** `QFormLayout::addRow(texte, widget)` peut fournir une association label/champ native Qt. L'absence de `setAccessibleName` explicite sur chaque champ ne prouve pas à elle seule un échec lecteur d'écran.
- **Recommandation :** édition numérique accessible, focus de scène explicite et annonces sélection/valeur/unité/erreur ; audits NVDA et parcours clavier de bout en bout.
- **Acceptation :** créer, sélectionner, dimensionner, régler et exporter un motif simple sans souris ; NVDA annonce les contrôles importants sans libellés ambigus.

#### UX-18 — Contraste insuffisant des icônes en thème sombre — P1 · M/C

- **Preuve :** encre fixe `#5C626A`, aucune recoloration par thème (`ui_icons.cpp:16-35`) ; contraste **2,21:1** sur `#2B2E34`, **1,94:1** sur `#33373E`.
- **Impact :** outils actifs difficiles à identifier dans le thème sombre ; mêmes icônes raster de 32 px sur tous les DPI, à tester à 150/200 %.
- **Recommandation :** encre sémantique claire/sombre et icon engine vectoriel ou rendu au DPR ; états normal/actif/désactivé explicites.
- **Acceptation :** icônes nécessaires à la compréhension ≥ 3:1 sur leurs fonds usuels, taille logique stable et rendu net à 200 %.

#### UX-19 — Avertissement d'import trop peu contrasté en clair — P1 · M/C

- **Preuve :** `warningLabel` utilise `tokens.warning` (`import_dialog.cpp:69-73`), soit `#B4791F` sur fond de dialogue `#F2F3F4` : **3,32:1**, inférieur à la référence 4,5:1 pour texte courant.
- **Impact :** une alerte importante, « L'image dépasse le cadre », est moins lisible que le reste du formulaire.
- **Recommandation :** distinguer couleur de texte d'avertissement et couleur de pictogramme/fond ; garder le texte explicite en plus de la couleur.
- **Acceptation :** contraste du texte d'alerte ≥ 4,5:1 dans les deux thèmes et après changement de thème.

#### UX-20 — Localisation française incomplète et pas de langue sélectionnable — P1 · C/V

- **Preuve :** `main.cpp` ne charge aucun `QTranslator` ; nombreux `QDialogButtonBox::Ok/Cancel/Close` sans traduction explicite, contrairement à la garde de sauvegarde qui renomme ses boutons (`MW:998-1005`). Exemples : `import_dialog.cpp:75`, `brightness_dialog.cpp:28`, `ai_segmentation_dialog.cpp:188`.
- **Impact à valider :** texte métier français mélangé aux libellés standard Qt non traduits ; pas de parcours en anglais. Les unités sont explicites, mais les formats localisés ne sont pas homogènes.
- **Recommandation :** charger traductions applicatives et Qt ; garder une nomenclature source cohérente ; tester nombres décimaux, accents, longueurs de texte et messages du cœur.
- **Acceptation :** parcours complet en français puis anglais sans langue mélangée, saisies virgule/point conformes à la locale, aucune troncature critique.

### 5.5 Frictions et cohérence de l'interface

| ID / priorité / preuve | Constat et effet | Recommandation / critère vérifiable |
|---|---|---|
| **UX-21 · P2 · C** | Création contour/tatami ouvre encore une modale de paramètres (`MW:3503-3523`, `3558-3584`) alors que l'inspecteur permet déjà de les éditer. Les réglages initiaux repartent de constantes. | Création avec défauts sûrs/dernier réglage explicite, puis sélection et focus de l'inspecteur ; un clic crée un résultat visible, Ctrl+Z l'enlève. Garder les confirmations satin quand elles portent sur une vraie décision de couverture. |
| **UX-22 · P1 · C** | Chaque `valueChanged` pousse une commande et régénère (`properties_panel.cpp:178-180`, `231-240` ; `MW:6257-6272`). Risque de latence et d'annulation découpée pendant la frappe. | Validation de saisie ou temporisation contrôlée, regroupement par geste et aperçu annulable ; une modification intentionnelle = un pas undo, sans bloquer la frappe. Ne pas résoudre cela en cassant l'undo ou en mutant directement le modèle. |
| **UX-23 · P2 · C** | La barre principale crée de nouvelles actions Ouvrir/Enregistrer au lieu de partager celles des menus (`MW:5591-5600`). Leur activation n'est pas mise en phase avec `saveProjectAct`. Les actions auto-numérisation/IA restent aussi déclenchables sans leur contexte. Accueil, menu et barre ne nomment pas uniformément l'ouverture SVG. | Registre d'actions partagé, mêmes états/libellés/infobulles partout ; Enregistrer est inactif pour un document réellement vide ; l'indisponibilité dit quoi sélectionner/importer. Ajouter une entrée d'accueil « Dessiner un motif » qui libère le canevas. |
| **UX-24 · P2 · C** | Inspecteur surtout paramétrique : pas de section commune éditable nom/fil/visibilité/dimensions (`properties_panel.cpp:140-464`, `MW:6339-6354`). « Verrou » dans Ordre ne précise pas qu'il s'agit du verrou d'optimisation d'ordre (`MW:6493`, `6725-6734` ; `embroidery_object.hpp:199`). | Section Général homogène et sémantique explicite « Conserver cette position dans l'ordre ». Ne pas promettre un verrou total d'édition si ce n'est pas son contrat. |
| **UX-25 · P2 · C/V** | Sélection affiche simultanément corps, nœuds et coins de redimensionnement ; plusieurs modes spécialisés restent proposés. La table d'aide annonce « Double-clic : entrer en édition » (`interaction_map.cpp:97`), alors que `onCanvasDoubleClicked` ne réalise pas cette transition en sélection (`MW:1635-1692`). | Sélection/transformation puis remodelage explicite par contexte, et aide reflétant uniquement le comportement réel. Vérifier chaque promesse de geste par un test d'effet, pas seulement l'existence d'une ligne dans la table. |
| **UX-26 · P2 · C** | Résultats, avertissements et échecs se répartissent entre nombreuses modales et barre d'état écrasable ; échec de génération uniquement en `showMessage` (`MW:2463-2465`), succès/échec autosave sans état permanent (`7017-7031`). | Notifications non modales persistantes avec action et historique court ; réserver les modales aux vraies décisions. Une erreur de génération ou de sauvegarde reste visible jusqu'à résolution/fermeture explicite. |
| **UX-27 · P2 · C** | Certaines couleurs locales sont figées à la construction : cadre d'accueil (`empty_state_widget.cpp:15-19`), warning d'import (`import_dialog.cpp:71-72`). Elles ne s'abonnent pas au changement de thème ; icônes également figées. | Réappliquer tous les styles locaux via le même événement, ou supprimer ces styles au profit d'éléments thémés. Basculer clair/sombre sans rouvrir l'accueil/dialogue ne laisse aucune surface de l'ancien thème. |
| **UX-28 · P2 · C/V** | Design system incomplet : métriques de barres fixes 18/20 px (`MW:5589`, `5832`), densité ne change que `controlHeight` (`design_tokens.cpp:9-18`), règles à 7,5 pt (`ruler.cpp:35-38`), QSS sans couverture explicite des toolbuttons/onglets/sliders/checks. Aucun `setStyle(Fusion)` dans `main.cpp` malgré la décision du 06/10. Focus custom limité à une bordure 1 px sur quelques contrôles (`app_theme.cpp:88`, `103-105`). | Unifier métriques, hiérarchie typographique et états focus/checked ; décider et appliquer consciemment la base de style. Valider les deux thèmes, deux densités et focus sur docks, listes, palette et canevas ; ne pas remplacer le problème par une refonte décorative globale. |
| **UX-29 · P2 · C** | Simulation : lecture/curseur seulement, vitesse automatiquement liée à la taille du motif (`MW:6768-6854`). Le compteur suit les indices de commandes, pas explicitement les points cousus. Pas de contrôle vitesse ni de pas par couleur. | Unités de progression explicites, vitesse, pas précédent/suivant et navigation par bloc de couleur ; un motif dense reste inspectable à un rythme choisi. |
| **UX-30 · P2 · C** | Préférences dispersées : IA dans Édition, thème/densité/navigation/accroche dans Affichage (`MW:649`, `861-895`). Pas de préférences générales ; autosave fixé à 120 s. | Préférences regroupées, sans retirer les accès rapides ; unités/cadre/défauts/dossier/autosave compréhensibles. Les pouces sont une évolution produit, pas une régression du stockage en µm. |
| **UX-31 · P2 · C/V** | Configuration IA orientée développeur (WSL, venv, script worker, modèles, logs : `ai_preferences_dialog.cpp:48-100`) et revue avec « IoU », « Stabilité », « Protéger » sans explication intégrée (`ai_segmentation_dialog.cpp:137-163`). | Chemin de configuration guidé avec détection/test, paramètres techniques dans Avancé, explication des scores/protection et coût des modèles ; un utilisateur non développeur peut déterminer si l'IA est disponible et quoi corriger. |
| **UX-32 · P2 · C** | Barre d'état permanente = gestes, outil, coordonnées (`MW:451-475`), pas zoom, taille/points de sélection ni état autosave. Résumé global conditionné à `hasImage` (`5804-5818`). Marqueur « modifié » forcé à chaque `refreshImage`, y compris undo vers l'état enregistré (`2395`, `1135-1138`). | État segmenté, concis et cohérent sur raster/SVG/DST ; vrai repère d'enregistrement dans l'historique. Après enregistrer → éditer → annuler, le marqueur doit refléter l'égalité à l'état enregistré, pas le passage dans une fonction de rafraîchissement. |

## 6. Contrastes : résultats et interprétation

Calcul effectué sur les valeurs sRGB des sources, avec luminance relative et ratio `(Lmax + 0,05) / (Lmin + 0,05)` ; conversion linéaire sRGB standard. Aucun échantillonnage d'écran.

| Usage | Premier plan / fond | Ratio |
|---|---|---:|
| Texte principal clair | `#1E2126` / `#FAFBFC` | **15,58:1** |
| Texte secondaire clair | `#696F78` / `#FAFBFC` | **4,89:1** |
| Texte principal sombre | `#E6E8EC` / `#2B2E34` | **11,09:1** |
| Texte secondaire sombre | `#9AA0A8` / `#2B2E34` | **5,16:1** |
| Icône sombre sur barre | `#5C626A` / `#2B2E34` | **2,21:1** |
| Icône sombre sur surface relevée | `#5C626A` / `#33373E` | **1,94:1** |
| Icône claire sur barre | `#5C626A` / `#FAFBFC` | **5,94:1** |
| Avertissement d'import clair | `#B4791F` / `#F2F3F4` | **3,32:1** |
| Texte sélection clair | `#FFFFFF` / `#B04E3C` | **5,25:1** |
| Texte sélection sombre | `#1A1412` / `#D06450` | **4,88:1** |
| Focus clair | `#2A6BD0` / `#FFFFFF` | **5,11:1** |
| Focus sombre | `#4A8BE0` / `#33373E` | **3,45:1** |
| Bordure claire sur champ | `#D4D7DB` / `#FFFFFF` | **1,44:1** |
| Bordure sombre sur champ | `#3E434B` / `#33373E` | **1,20:1** |

Interprétation :

- Les couleurs de texte principal/secondaire et de sélection sont une bonne base ; ce n'est pas un thème globalement illisible.
- Références WCAG utilisées comme guide pour cette application desktop : 4,5:1 pour texte courant, 3:1 pour information graphique nécessaire. Pas de déclaration de conformité Web/desktop à partir de ces seuls calculs.
- Une bordure décorative à faible contraste n'est pas automatiquement un défaut de conformité. Si elle constitue le seul repère d'un champ, son contraste doit être traité ; le rendu effectif est à examiner.
- La couleur de focus a un contraste correct sur ces fonds, mais cela ne prouve ni sa présence sur tous les contrôles ni la visibilité d'une bordure de 1 px.
- Les poignées custom ont une cible élargie de **14 px logiques**, à comparer à un objectif ergonomique de 24 px lorsque la densité de nœuds le permet ; agrandir les zones sans gérer les recouvrements peut créer de nouvelles ambiguïtés.
- Les emojis/pictogrammes textuels restent utilisés pour ordre, simulation et analyse (`MW:6491-6493`, `6413-6418`, `6773`). Ils ne constituent pas seuls une violation ; leur apparence/annonce varie et doit être vérifiée.

## 7. Plan de remédiation proposé, sans implémentation

| Lot | Priorité | Constats | Résultat attendu |
|---|---|---|---|
| **0 — Débloquer la vérification** | Préalable | C3493 | Build desktop et 15 suites QTest exécutables ; captures des parcours de référence. |
| **1 — Fiabilité du document** | P0/P1 | 01, 02, 03, 05, 06, 07, 08, 09, 11 | Impossible de perdre/remplacer un document sans décision ; rendu, champs, sauvegarde et analyse conformes au modèle. |
| **2 — Réactivité et visibilité des tâches** | P0/P1 | 04, 16, 22, 26 | Travail en arrière-plan, annulation atomique, réglages capturés, états persistants. |
| **3 — Parcours de production** | P1/P2 | 10, 13, 14, 15, 21, 23, 24, 25, 29, 32 | Source → réglage → vérification → export compréhensible, actions honnêtes et états synchronisés. |
| **4 — Accessibilité et adaptation** | P1/P2 | 12, 17, 18, 19, 20, 27, 28 | Clavier/lecteur d'écran, thèmes contrastés, DPI/petits écrans, traductions cohérentes. |
| **5 — Préférences et prise en main IA** | P2 | 30, 31 | Configuration utilisable sans connaître l'architecture du worker. |

**Ordre conseillé :** lot 0, protection contre la perte de travail et parcours sans image du lot 1, puis réactivité. Les petites corrections de contraste peuvent être livrées indépendamment. Ne pas lancer une refonte cosmétique avant les corrections de confiance.

Garde-fous : commandes `ICommand`, séquence effective partagée entre aperçu/export/analyse, instantanés pour les tâches, tests de déterminisme. Les propositions de nouveaux paramètres persistants demandent la checklist `openstitch-stitch-param` ; ce rapport ne les ajoute pas.

## 8. Plan de preuve et validation restant à faire

### Tests automatisés à ajouter lors des correctifs

Les tests existants couvrent déjà beaucoup de sélection, gestes, docks et modes. Les tests de composants isolés ne suffisent pas à prouver la cohérence de tout un parcours.

| Vérification | Preuve attendue |
|---|---|
| Remplacements de document | Matrice de tous les points d'entrée et décisions, y compris sauvegarde annulée/échouée. |
| Sans raster | Fixture SVG/vectorielle → création broderie → aperçu/export → modification → undo/redo, sans image factice. |
| Récupérations multiples | Deux instantanés distincts, sauvegarde conservée en cas d'échec, aucune suppression involontaire. |
| Import non proportionnel | Taille saisie égale à taille de travail, ou option explicitement non disponible. |
| Inspecteur | Égalité widget/modèle après undo, redo, conversion, édition au canevas ; tirages successifs différents et reproductibles. |
| Type déjà courant | No-op sur paramètres, sections et undo. |
| DST source seul | Sauvegarde OSP, autosave, affichage et garde de fermeture cohérents. |
| Analyse | Résultats liés à une révision ; modification et chargement les invalident. |
| Tâches | UI traite un événement pendant calcul ; annulation sans mutation ; résultat tardif obsolète ignoré. |
| IA | Annulation de toutes les phases, configuration figée, avertissement de repli conservé hors dialogue. |
| Contrôles/UI | États des mêmes actions cohérents menu/barre/guide, saisie = un geste undo, pas de commande émise par resynchronisation. |

QTest headless conformément aux conventions du dépôt ; pas de tests de comparaison de pixels ni de temporisations arbitraires. Les captures manuelles sont des pièces d'audit visuel, pas des tests pixel introduits dans la CI.

### Matrice de validation visuelle et d'usage

**Toutes les lignes ci-dessous restent non exécutées pendant cet audit.**

| Dimension | Cas à couvrir | À observer |
|---|---|---|
| Résolution/DPI | 1920 × 1080 à 100 %, 1280 × 720 à 100 %, portable à 150/200 %, passage entre écrans | Place utile du canevas, champs accessibles, toolbar overflow, netteté d'icônes, dialogues dans la zone de travail. |
| Apparence | Clair/sombre × Confortable/Compact ; changement à chaud | Surfaces, icônes, sélection, focus, états désactivés, accueil, warnings, onglets et sliders. |
| Données | Vide, image, SVG sans raster, dessin seul, DST seul, projet avec retouches, beaucoup d'objets/couleurs | Aucune fausse étape, forme perdue, sélection fantôme ou état de sauvegarde incohérent. |
| Périphériques | Souris trois boutons, souris sans clic molette, pavé tactile réel | Zoom au curseur, pan, pinch, sélection/accroche, perte de focus ; événements synthétiques ≠ pilote réel. |
| Clavier | Tab/Maj+Tab, menus, Enter/Escape/Delete, champs de saisie | Focus visible, raccourcis qui n'avalent pas la saisie, retour du focus après dialogue, tâches réalisables sans souris. |
| Lecteur d'écran | NVDA Windows, menus/panneaux/champs/notifications | Nom, valeur, unité, état, cause d'indisponibilité et annonce de fin/échec de tâche. |
| Langue/texte | Français/anglais, locale décimale, police agrandie, noms longs, récents homonymes | Pas de mélange de langue, troncature ambiguë, mauvais chiffre ou fichier indiscernable. |
| Erreurs | Fichier absent/corrompu, écriture refusée, export hors cadre, modèle IA absent, génération impossible | Travail précédent préservé, cause compréhensible, action de reprise et message persistant. |
| Performance | Petite image puis image de référence et motif dense, modification de paramètre, simulation, autosave | Temps mesuré, réactivité, annulation, aperçu stable ; ne pas réutiliser d'anciens benchmarks comme résultats de cet audit. |

### Tests utilisateurs recommandés

Protocole formatif avec environ cinq personnes, mélange débutants en numérisation et utilisateurs de broderie ; ce n'est pas un échantillon statistique. Tâches : premier motif depuis image, réglage de taille/densité, SVG direct, correction d'un avertissement, reprise d'un travail et export machine. Mesurer réussite sans aide, erreurs et récupérations, hésitations, temps et compréhension des termes. Comparer avant/après sur mêmes fixtures ; ne pas confondre préférence esthétique et succès de tâche.

## 9. Conclusion et statut de livraison de l'audit

Le rapport couvre les principales surfaces et parcours UX/UI actuels, avec causes vérifiables et critères d'acceptation. **La revue de code et les mesures de couleurs sont réalisées ; la validation visuelle, les tests desktop, l'accessibilité assistée et les essais utilisateurs restent à effectuer.**

Aucun fichier C++/CMake, aucune interface, aucun moteur de broderie et aucun statut de roadmap n'ont été modifiés dans ce lot. Le rapport est une annexe sous `docs/`, hors `docs/source/` : pas de modification du PDF éditorial. Aucun commit effectué.
