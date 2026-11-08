# OpenStitch Studio — Cahier exhaustif de contrôle qualité et correction

> **Version** : 2026-11-08 · **Cible** : `ElZacho200a/OpenStitch`, desktop `apps/desktop_v2/studio/` et services réellement utilisés · **Nature** : plan de recette / checklist **à exécuter**, pas résultats d'un audit déjà effectué.

## Intention et périmètre

Aucune coquille tolérée par défaut : inspecter chaque contrôle, chaque état, chaque parcours, chaque taille de fenêtre, les résultats métiers, la persistance `.osp`, la sortie machine, les backends GPU/CPU, les conditions d'erreur et la distribution Windows. **Ne pas faire passer une fonctionnalité absente pour un test échoué :** enregistrer `ABSENT` et décider si elle relève d'une correction de parité ou d'un autre chantier. **Ne pas inventer une validation** : `NOT_RUN` reste `NOT_RUN` sans environnement approprié.

Ce document inclut volontairement des tests très fins ou rares. Le traitement peut être priorisé, mais **aucune entrée ne doit disparaître de l'inventaire** parce qu'elle paraît mineure. Il ne promet pas une couverture mathématiquement exhaustive des états possibles ; la découverte de chaque nouveau composant ajoute des vérifications à la matrice.

### Points déjà contextualisés dans le dépôt

- Quatre espaces `ImageSpace`, `SegmentationSpace`, `VectorSpace`, `PointsSpace`, hôte `SpaceHost`, un seul canevas actif, `DocumentSession` et `SelectionModel` communs : `docs/desktop-v2/23-espaces-studio.md`.
- Cible Qt Quick / QML, rendu Scene Graph GPU et repli `--software`, jetons dans `Theme.qml`, composants `Studio*`, tests `test_desktop_v2_studio_*`.
- Inventaire de parité V1→Studio et script `scripts/parity/build_studio_matrix.py` ; **un test de contrôleur n'est pas une recette visuelle**.
- PR #291 (ouverte lors de la consultation) : adaptations de panneaux et scroll, validation visuelle/build restant à confirmer ; PR #289 : corrections et réduction des fils, intégration à surveiller. **Ne jamais supposer ces PR fusionnées.**
- La documentation historique peut précéder la bascule du binaire Studio : **le code courant et les checks actuels prévalent sur un état ancien du journal**.

### Grille de sévérité

| Niveau | Exemple | Traitement |
|---|---|---|
| **S0 — Bloquant** | perte/corruption de fichier, modification silencieuse, export machine incorrect dangereux, crash reproductible, faille grave | stop release, corriger d'abord |
| **S1 — Critique** | commande essentielle non cliquable, canvas non rafraîchi, ouverture/sauvegarde/export bloqué, régression majeure, CI cassée par le code | corriger avant merge qualité |
| **S2 — Important** | mauvais focus, liste tronquée, action accessible seulement par contournement, rendu dégradé, erreur de statut | corriger dans chantier, sinon justification publique |
| **S3 — Mineur** | alignement, infobulle, cohérence de marge, micro-animation, état rare non bloquant | corriger ou journaliser explicitement |

### Contrat de preuve par vérification

Chaque ID reçoit : `statut`, `sévérité`, `SHA`, `plateforme`, `Debug/Release`, `GPU/CPU`, `taille`, `DPR`, `document fixture`, `étapes de reproduction`, `résultat attendu`, `résultat constaté`, `preuve (test + capture/log)`, `correctif`, `test de non-régression`, `PR`. Une vérification `PASS` exige une preuve correspondant à son **niveau** : capture/événement réel pour l'UI, comparaison artefact pour le format, mesure native pour la perf, test réel pour la machine si applicable.

Statuts possibles : `PASS`, `FAIL`, `ABSENT`, `BLOCKED`, `NOT_RUN`, `NOT_APPLICABLE`. Aucune substitution entre statuts.

### Matrice minimale de couverture visuelle

- **Espaces** : Image, Segmentation, Vecteurs, Points ; plus dialogues transversaux, états vides, erreurs, tâches et sélection multiple.
- **Fenêtres** : 1000×640, 1280×720, 1366×768, 1536×960, 1920×1080, 2560×1440, minimum effectif, hauteurs réduites, ultra-large.
- **DPR** : 1,0 ; 1,25 ; 1,5 ; 2,0, et transition multi-écrans si possible.
- **UI** : sombre, clair, densités proposées, mouvement réduit.
- **Backends** : GPU matériel prouvé par logs, logiciel `--software`, éventuellement autres RHI si supportés.
- **États d'interaction** : repos, hover, pressed, disabled, focus clavier, popup ouvert, menu contextuel, scroll, geste en cours, tâche en cours, erreur et notification.
- **Documents** : vide, image simple, segmentation complexe, SVG, `.osp` ancien, `.osp` complet, fichier défectueux, 1k/10k/50k/100k+ points, 10 couleurs, objets superposés et hors cadre.

Ne pas réaliser le produit cartésien complet si irréaliste : utiliser une matrice de **paires** pour le général, puis toutes les combinaisons des flux S0/S1 et des défauts reproduits. Les tests de capture sont requis sur environnement graphique Windows natif pour valider géométrie et cliquabilité ; `offscreen` ne remplace pas la validation GPU ni les polices du poste réel.

---

## Registre des vérifications atomiques

Chaque ligne est une vérification indépendante. Numérotation stable par famille, conçue pour alimenter des issues, une matrice CSV ou un tableau Notion.

### GOV — Cadrage, inventaire, preuves et pilotage (20 vérifications)

- [ ] **GOV-001** — Identifier le SHA exact de la branche testée et la date du contrôle.

- [ ] **GOV-002** — Lister les PR ouvertes qui touchent Studio et leurs branches de base.

- [ ] **GOV-003** — Repérer les modifications concurrentes des agents avant toute écriture.

- [ ] **GOV-004** — Lister les exécutables V1, V2 Widgets et Studio sans les confondre.

- [ ] **GOV-005** — Dresser l'inventaire des QML, composants, contrôleurs, panneaux, dialogues et actions.

- [ ] **GOV-006** — Dresser l'inventaire des commandes du catalogue et leurs raccourcis.

- [ ] **GOV-007** — Dresser l'inventaire des formats importés et exportés réellement supportés.

- [ ] **GOV-008** — Comparer l'inventaire V1 à la matrice de parité Studio générée.

- [ ] **GOV-009** — Ajouter séparément les fonctionnalités V2-only non couvertes par l'inventaire V1.

- [ ] **GOV-010** — Comparer les paramètres exposés en QML au `param_spec` réel et aux paramètres moteur.

- [ ] **GOV-011** — Identifier les fonctions présentes dans le code mais inaccessibles depuis l'UI.

- [ ] **GOV-012** — Identifier les boutons visibles sans implémentation fonctionnelle.

- [ ] **GOV-013** — Identifier les commandes activées dont les préconditions ne sont pas réunies.

- [ ] **GOV-014** — Identifier les fonctionnalités dupliquées, incohérentes ou contradictoires.

- [ ] **GOV-015** — Tenir un registre d'anomalies avec ID, sévérité, reproduction et preuve.

- [ ] **GOV-016** — Distinguer `PASS`, `FAIL`, `ABSENT`, `BLOCKED`, `NOT_RUN`, `NOT_APPLICABLE`.

- [ ] **GOV-017** — Ne jamais assimiler un test unitaire vert à une validation d'usage réelle.

- [ ] **GOV-018** — Ne jamais déclarer « validé » sans capture ou test pertinent et artefact.

- [ ] **GOV-019** — Documenter explicitement les décisions produit nécessaires plutôt que choisir silencieusement.

- [ ] **GOV-020** — Conserver la traçabilité anomalie → correctif → test → preuve → commit.


### ENV — Configuration, compilation et environnement (20 vérifications)

- [ ] **ENV-001** — Configurer CMake via les presets Windows/MSVC pris en charge.

- [ ] **ENV-002** — Compiler Studio en Debug à partir d'un clone propre.

- [ ] **ENV-003** — Compiler Studio en Release à partir d'un clone propre.

- [ ] **ENV-004** — Compiler les cibles Qt-free sans Qt installé.

- [ ] **ENV-005** — Compiler les composants historiques préservés quand leur compatibilité est requise.

- [ ] **ENV-006** — Vérifier qu'une dépendance Qt manquante est signalée clairement, non silencieusement.

- [ ] **ENV-007** — Vérifier que chaque fichier QML est inclus dans les ressources du binaire.

- [ ] **ENV-008** — Vérifier que chaque composant Studio* nécessaire est déclaré dans `qmldir`.

- [ ] **ENV-009** — Vérifier la résolution des imports QML dans le binaire installé, pas seulement au build.

- [ ] **ENV-010** — Rechercher avertissements de compilation et erreurs QML au chargement.

- [ ] **ENV-011** — Vérifier l'absence de nouvelles erreurs clang-format/lint.

- [ ] **ENV-012** — Vérifier ressources fonts, icônes, images, plugins et traductions.

- [ ] **ENV-013** — Lancer tous les tests concernés en Debug avec `--output-on-failure`.

- [ ] **ENV-014** — Lancer tous les tests concernés en Release avec `--output-on-failure`.

- [ ] **ENV-015** — Répéter les tests connus instables plusieurs fois sur la même révision.

- [ ] **ENV-016** — Tester avec et sans le cache de compilation/construction existant.

- [ ] **ENV-017** — Tester lancement depuis un répertoire de travail différent de la racine.

- [ ] **ENV-018** — Tester un utilisateur Windows non administrateur.

- [ ] **ENV-019** — Vérifier chemins de DLL et plugins Qt au lancement d'une installation propre.

- [ ] **ENV-020** — Vérifier absence de dépendance accidentelle aux fichiers du dépôt local.


### BOOT — Démarrage, fenêtre et shell général (20 vérifications)

- [ ] **BOOT-001** — Démarrer avec aucun fichier et aucun historique utilisateur.

- [ ] **BOOT-002** — Démarrer avec un `.osp` valide fourni en argument.

- [ ] **BOOT-003** — Démarrer avec une image en argument si le mode est supporté.

- [ ] **BOOT-004** — Démarrer avec une URL/chemin invalide et obtenir une erreur intelligible.

- [ ] **BOOT-005** — Démarrer sans réseau et vérifier les fonctions locales.

- [ ] **BOOT-006** — Démarrer avec préférences corrompues et prévoir récupération saine.

- [ ] **BOOT-007** — Vérifier icône, titre, version et état modifié de la fenêtre.

- [ ] **BOOT-008** — Vérifier première frame complète sans écran blanc permanent.

- [ ] **BOOT-009** — Vérifier absence de flash de thème clair au lancement sombre.

- [ ] **BOOT-010** — Vérifier maximiser, restaurer, réduire, fermer.

- [ ] **BOOT-011** — Vérifier redimensionnement en continu pendant une tâche lourde.

- [ ] **BOOT-012** — Vérifier réouverture après fermeture normale.

- [ ] **BOOT-013** — Vérifier réveil après veille et retour de focus fenêtre.

- [ ] **BOOT-014** — Vérifier bascule plein écran si disponible.

- [ ] **BOOT-015** — Vérifier restauration de taille, position et moniteur, sans hors-écran.

- [ ] **BOOT-016** — Vérifier que le bouton fermer de la fenêtre n'est pas masqué.

- [ ] **BOOT-017** — Vérifier barre de titre et commandes système avec DPI différents.

- [ ] **BOOT-018** — Vérifier comportement avec un fichier corrompu à l'ouverture initiale.

- [ ] **BOOT-019** — Vérifier aucun dialogue caché ne bloque l'application au démarrage.

- [ ] **BOOT-020** — Vérifier que chaque zone du shell a un nom/fonction identifiable.


### THEME — Design system, couleurs, typographie et icônes (25 vérifications)

- [ ] **THEME-001** — Tester le thème sombre sur chaque écran et panneau.

- [ ] **THEME-002** — Tester le thème clair sur chaque écran et panneau.

- [ ] **THEME-003** — Tester le mode système et changement dynamique si supportés.

- [ ] **THEME-004** — Tester la densité compacte/standard/augmentée si exposée.

- [ ] **THEME-005** — Tester le réglage « réduire les animations » sur chaque transition.

- [ ] **THEME-006** — Contrôler contrastes texte/surface sur les variantes de composants.

- [ ] **THEME-007** — Contrôler contrastes des états disabled, hover, pressed, checked, selected, focus.

- [ ] **THEME-008** — Contrôler contrastes des messages succès, avertissement, erreur, information.

- [ ] **THEME-009** — Contrôler texte secondaire, texte d'aide, placeholder, unités et badges.

- [ ] **THEME-010** — Contrôler couleurs du canevas indépendantes du thème.

- [ ] **THEME-011** — Contrôler conservation exacte des couleurs documentaires et fils.

- [ ] **THEME-012** — Vérifier absence d'hexadécimaux UI hors tokens autorisés.

- [ ] **THEME-013** — Vérifier absence de tailles de police numériques dispersées hors tokens.

- [ ] **THEME-014** — Vérifier absence de contrôles Qt bruts non habillés par les composants Studio.

- [ ] **THEME-015** — Vérifier police, graisse, taille, crénage, interlignage cohérents.

- [ ] **THEME-016** — Vérifier lisibilité des chiffres décimaux, signes, symboles, µm/mm, pourcentages.

- [ ] **THEME-017** — Vérifier icônes présentes, lisibles, cohérentes de style et sens.

- [ ] **THEME-018** — Vérifier icônes fonctionnelles sur clair/sombre et fonds survolés.

- [ ] **THEME-019** — Vérifier alignement vertical des pictogrammes et libellés.

- [ ] **THEME-020** — Vérifier homogénéité rayons, bordures, ombres, espacements et hauteurs de contrôle.

- [ ] **THEME-021** — Vérifier absence de couleurs « état » confondues avec couleurs de fils.

- [ ] **THEME-022** — Vérifier messages de confirmation non uniquement différenciés par couleur.

- [ ] **THEME-023** — Vérifier troncature des labels longs et présence d'infobulles explicatives.

- [ ] **THEME-024** — Vérifier transitions réversibles, sans animation parasite ni scintillement.

- [ ] **THEME-025** — Vérifier nombre de styles visuels concurrents et uniformiser sans perte d'information.


### LAY — Adaptation des dispositions, tailles et écrans (26 vérifications)

- [ ] **LAY-001** — Tester fenêtre 1000×640 en dimensions logiques.

- [ ] **LAY-002** — Tester fenêtre 1280×720 en dimensions logiques.

- [ ] **LAY-003** — Tester fenêtre 1366×768 en dimensions logiques.

- [ ] **LAY-004** — Tester fenêtre 1440×900 en dimensions logiques.

- [ ] **LAY-005** — Tester fenêtre 1536×960 en dimensions logiques.

- [ ] **LAY-006** — Tester fenêtre 1920×1080 en dimensions logiques.

- [ ] **LAY-007** — Tester fenêtre 2560×1440 en dimensions logiques.

- [ ] **LAY-008** — Tester fenêtre ultra-large et fenêtre très étroite.

- [ ] **LAY-009** — Tester largeur et hauteur minimales permises par l'application.

- [ ] **LAY-010** — Tester DPI 100 %, 125 %, 150 %, 175 %, 200 %, si matériel/environnement disponibles.

- [ ] **LAY-011** — Tester sur moniteur physique HiDPI, pas uniquement émulation logicielle.

- [ ] **LAY-012** — Tester glisser la fenêtre d'un moniteur DPI 100 % à DPI 150 %.

- [ ] **LAY-013** — Tester changement de résolution/échelle pendant l'application si supporté.

- [ ] **LAY-014** — Vérifier qu'aucun contenu ne sort du viewport sans moyen d'accès.

- [ ] **LAY-015** — Vérifier que le défilement est offert si un panneau ne tient pas.

- [ ] **LAY-016** — Vérifier scroll horizontal uniquement là où nécessaire, avec indicateur perceptible.

- [ ] **LAY-017** — Vérifier redimensionnement de chaque panneau et ses limites mini/maxi.

- [ ] **LAY-018** — Vérifier que le canevas garde une surface utile lorsque les panneaux s'ouvrent.

- [ ] **LAY-019** — Vérifier absence de superposition non voulue entre barres, panneaux et canvas.

- [ ] **LAY-020** — Vérifier menus, fenêtres modales et popups ne dépassant pas les écrans.

- [ ] **LAY-021** — Vérifier ancrage des toolbars lorsque la fenêtre se rétrécit.

- [ ] **LAY-022** — Vérifier texte intégral visible avec zoom système de texte.

- [ ] **LAY-023** — Vérifier affichage de données longues dans l'inspecteur.

- [ ] **LAY-024** — Vérifier basculement d'onglet sans saut intempestif de largeur.

- [ ] **LAY-025** — Vérifier retour au layout précédent sans déplacement sauvage des panneaux.

- [ ] **LAY-026** — Vérifier persistance/restauration d'une disposition personnalisée si disponible.


### CTL — Tous les contrôles : visibilité, clics, états et saisie (30 vérifications)

- [ ] **CTL-001** — Inventorier chaque bouton visible et cliquer physiquement en son centre.

- [ ] **CTL-002** — Cliquer chaque bouton près des quatre bords de sa zone interactive.

- [ ] **CTL-003** — Tester clic souris gauche, double-clic et clic droit selon leur sémantique.

- [ ] **CTL-004** — Détecter les éléments transparents superposés qui interceptent les clics.

- [ ] **CTL-005** — Vérifier que chaque bouton déclenche une action observable, pas seulement un signal.

- [ ] **CTL-006** — Vérifier qu'un bouton désactivé reste réellement inactif.

- [ ] **CTL-007** — Vérifier que les préconditions de disabled sont justifiées et explicables.

- [ ] **CTL-008** — Vérifier action cliquable après avoir affiché puis fermé un popup.

- [ ] **CTL-009** — Vérifier action cliquable après changement d'espace et retour.

- [ ] **CTL-010** — Vérifier action cliquable après un redimensionnement drastique.

- [ ] **CTL-011** — Ouvrir et fermer toutes les combobox par souris.

- [ ] **CTL-012** — Ouvrir et fermer toutes les combobox par clavier.

- [ ] **CTL-013** — Vérifier popup au-dessus des autres éléments, sans clip ni transparence gênante.

- [ ] **CTL-014** — Vérifier popup repositionné près des bords de fenêtre.

- [ ] **CTL-015** — Vérifier listes déroulantes longues avec scroll et sélection.

- [ ] **CTL-016** — Vérifier que la liste reflète l'élément réellement sélectionné.

- [ ] **CTL-017** — Tester checkboxes, switches et boutons radio dans les deux sens.

- [ ] **CTL-018** — Tester sliders aux bornes, au centre, au pas fin et au pas grossier.

- [ ] **CTL-019** — Tester champs numériques au clavier, incréments, molette et flèches.

- [ ] **CTL-020** — Tester champs texte vides, longs, espaces, Unicode et caractères spéciaux.

- [ ] **CTL-021** — Vérifier validation des unités, décimales, virgules et nombres négatifs.

- [ ] **CTL-022** — Vérifier validation des bornes et correction d'entrées hors plage.

- [ ] **CTL-023** — Vérifier signal visuel d'erreur sans effacer la saisie sans avertissement.

- [ ] **CTL-024** — Vérifier valeur affichée = valeur effective dans le modèle après validation.

- [ ] **CTL-025** — Vérifier état visuel checked/selected cohérent avec l'état métier.

- [ ] **CTL-026** — Vérifier tabulations et focus pour chaque contrôle interactif.

- [ ] **CTL-027** — Vérifier que la molette sur panneau ne modifie pas un paramètre involontairement.

- [ ] **CTL-028** — Vérifier absence d'activation double causée par plusieurs handlers.

- [ ] **CTL-029** — Vérifier drag de slider ne déclenchant pas un pan du canevas sous-jacent.

- [ ] **CTL-030** — Vérifier la cible interactive suffisante pour les petites icônes.


### KEY — Clavier, focus, raccourcis et édition de texte (25 vérifications)

- [ ] **KEY-001** — Parcourir toute la fenêtre au clavier avec Tab et Maj+Tab.

- [ ] **KEY-002** — Vérifier ordre de focus conforme à la lecture visuelle.

- [ ] **KEY-003** — Vérifier anneau de focus visible et contrasté partout.

- [ ] **KEY-004** — Vérifier retour du focus au contrôle initiateur après fermeture de dialogue.

- [ ] **KEY-005** — Vérifier aucune perte de focus lors d'un changement de thème.

- [ ] **KEY-006** — Vérifier focus et frappe après un changement d'espace.

- [ ] **KEY-007** — Vérifier focus après manipulation du canevas et retour à l'inspecteur.

- [ ] **KEY-008** — Vérifier que Entrée valide et Échap annule/ferme selon le contexte.

- [ ] **KEY-009** — Vérifier que Espace active uniquement le contrôle focalisé.

- [ ] **KEY-010** — Vérifier que les flèches du clavier ne déplacent pas des objets pendant la saisie de texte.

- [ ] **KEY-011** — Vérifier que Suppr ne supprime rien tant qu'un champ texte l'absorbe.

- [ ] **KEY-012** — Vérifier Ctrl+A sélectionne le texte ou les objets selon le focus.

- [ ] **KEY-013** — Vérifier Ctrl+C/Ctrl+V/Ctrl+X dans les champs et l'éditeur selon le focus.

- [ ] **KEY-014** — Vérifier Ctrl+Z/Ctrl+Y sans double exécution ni collision avec édition de texte.

- [ ] **KEY-015** — Vérifier Ctrl+1…4 depuis chaque espace et chaque état de focus approprié.

- [ ] **KEY-016** — Vérifier Ctrl+K et exécution des commandes depuis la palette.

- [ ] **KEY-017** — Vérifier les raccourcis globaux annoncés dans les infobulles et menus.

- [ ] **KEY-018** — Vérifier l'absence de raccourcis dupliqués/collision entre outils.

- [ ] **KEY-019** — Vérifier répétition d'une touche maintenue et cadence souhaitable.

- [ ] **KEY-020** — Vérifier clavier AZERTY, éventuellement QWERTY, et clavier sans pavé numérique.

- [ ] **KEY-021** — Vérifier clavier avec composition IME et accents dans les champs.

- [ ] **KEY-022** — Vérifier possibilité de sortir d'un mode outil sans souris.

- [ ] **KEY-023** — Vérifier que les raccourcis restent attachés au bon contexte d'espace.

- [ ] **KEY-024** — Vérifier qu'une action destructive ne se lance pas pendant un dialogue modal.

- [ ] **KEY-025** — Vérifier navigation clavier à l'intérieur d'un popup puis retour sans perte de sélection.


### POP — Menus, modales, infobulles, confirmations et notifications (22 vérifications)

- [ ] **POP-001** — Ouvrir chaque menu principal depuis souris et clavier.

- [ ] **POP-002** — Ouvrir chaque menu contextuel lié au canevas.

- [ ] **POP-003** — Ouvrir chaque menu contextuel lié à l'arbre des objets et aux régions.

- [ ] **POP-004** — Ouvrir menus avec un élément sélectionné et sans sélection.

- [ ] **POP-005** — Vérifier sous-menus, séparateurs, libellés, icônes et raccourcis.

- [ ] **POP-006** — Vérifier popup ancré au bon objet après pan/zoom/resize.

- [ ] **POP-007** — Vérifier qu'un menu se ferme au clic extérieur sans effet secondaire.

- [ ] **POP-008** — Vérifier qu'un second menu n'est pas bloqué par le premier.

- [ ] **POP-009** — Vérifier que les dialogues ont un titre, une action primaire et une annulation clairs.

- [ ] **POP-010** — Vérifier comportement clic extérieur, Entrée, Échap et croix de fermeture.

- [ ] **POP-011** — Vérifier validation impossible si un champ obligatoire est invalide.

- [ ] **POP-012** — Vérifier confirmation avant écrasement, perte de modifications et opérations destructrices.

- [ ] **POP-013** — Vérifier textes de confirmation avec conséquences et données impactées.

- [ ] **POP-014** — Vérifier notifications de réussite, erreur, progression et avertissement.

- [ ] **POP-015** — Vérifier notification lisible et non cachée sous une barre/popup.

- [ ] **POP-016** — Vérifier qu'une notification n'intercepte pas le canevas de façon persistante.

- [ ] **POP-017** — Vérifier détail utile en cas d'échec, avec récupération proposée.

- [ ] **POP-018** — Vérifier infobulles informatives sans bloquer la commande.

- [ ] **POP-019** — Vérifier temporisation et apparition d'infobulles après survol répété.

- [ ] **POP-020** — Vérifier absence de fenêtre modale cachée derrière la fenêtre principale.

- [ ] **POP-021** — Vérifier la fermeture d'un dialogue ne réinitialise pas une sélection non concernée.

- [ ] **POP-022** — Vérifier la cohérence des libellés d'un panneau à l'autre.


### SPACE — Les quatre espaces et leur cohérence commune (24 vérifications)

- [ ] **SPACE-001** — Vérifier Image, Segmentation, Vecteurs, Points accessibles par onglets.

- [ ] **SPACE-002** — Vérifier Image, Segmentation, Vecteurs, Points accessibles par Ctrl+1…4.

- [ ] **SPACE-003** — Vérifier un seul espace chargé/un seul canevas actif à la fois.

- [ ] **SPACE-004** — Vérifier document identique avant et après dix bascules d'espaces.

- [ ] **SPACE-005** — Vérifier sélection unique commune à tous les espaces.

- [ ] **SPACE-006** — Vérifier maintien de caméra par espace et restauration au retour.

- [ ] **SPACE-007** — Vérifier réinitialisation de caméra après remplacement du document.

- [ ] **SPACE-008** — Vérifier état des panneaux sauvegardé hors du composant démonté.

- [ ] **SPACE-009** — Vérifier qu'une tâche continue ou s'annule proprement lors du démontage QML.

- [ ] **SPACE-010** — Vérifier absence de signaux laissés connectés à un objet QML détruit.

- [ ] **SPACE-011** — Vérifier absence de fuite mémoire après cent bascules d'espaces.

- [ ] **SPACE-012** — Vérifier statut d'étape `empty`, `upToDate`, `stale`, `blocked` exact.

- [ ] **SPACE-013** — Vérifier pastille, texte d'aide, infobulle et nom accessible de chaque statut.

- [ ] **SPACE-014** — Vérifier action « Mettre à jour l'aval » disponible au bon moment.

- [ ] **SPACE-015** — Vérifier clic sur action de bandeau déclenchant la vraie régénération attendue.

- [ ] **SPACE-016** — Vérifier régénération aval annulable en une étape.

- [ ] **SPACE-017** — Vérifier refus/confirmation si régénération détruit des retouches.

- [ ] **SPACE-018** — Vérifier que la modification amont ne modifie pas l'aval silencieusement.

- [ ] **SPACE-019** — Vérifier accès aux actions Fichier, aide, préférences et palette dans chaque espace.

- [ ] **SPACE-020** — Vérifier place cohérente des outils, panneaux, inspecteurs et états vides.

- [ ] **SPACE-021** — Vérifier qu'une action hors contexte bascule vers le bon espace si prévue.

- [ ] **SPACE-022** — Vérifier navigation vers un problème depuis un autre espace sans perte de document.

- [ ] **SPACE-023** — Vérifier harmonisation des comportements de scroll/panneaux entre les quatre espaces.

- [ ] **SPACE-024** — Vérifier absence d'anciens intitulés contradictoires avec les quatre nouveaux espaces.


### CANV — Canevas : exactitude du rendu et invalidation (30 vérifications)

- [ ] **CANV-001** — Vérifier rendu d'un document vide, minimal, normal et très dense.

- [ ] **CANV-002** — Vérifier chargement initial sans canevas blanc durable.

- [ ] **CANV-003** — Vérifier le contenu après resize de la fenêtre.

- [ ] **CANV-004** — Vérifier contenu après réduire/restaurer la fenêtre.

- [ ] **CANV-005** — Vérifier contenu après changement d'espace puis retour.

- [ ] **CANV-006** — Vérifier contenu après ouverture/fermeture d'un panneau superposé.

- [ ] **CANV-007** — Vérifier contenu après changement de thème.

- [ ] **CANV-008** — Vérifier contenu après changement de DPI ou déplacement de moniteur.

- [ ] **CANV-009** — Vérifier contenu après mise en veille/reprise et perte de contexte GPU.

- [ ] **CANV-010** — Vérifier rendu après une modification de géométrie.

- [ ] **CANV-011** — Vérifier rendu après une modification de paramètres de points.

- [ ] **CANV-012** — Vérifier rendu après un changement de couleur de fil.

- [ ] **CANV-013** — Vérifier rendu après un changement de visibilité de calque.

- [ ] **CANV-014** — Vérifier rendu après un reorder d'objet ou de fil.

- [ ] **CANV-015** — Vérifier rendu après undo/redo et retour à la révision précédente.

- [ ] **CANV-016** — Vérifier aucune géométrie fantôme ou « image précédente » après effacement.

- [ ] **CANV-017** — Vérifier ordre Z et empilement des couches image/régions/vecteurs/points.

- [ ] **CANV-018** — Vérifier clip dans le viewport et absence de dessin sur l'inspecteur.

- [ ] **CANV-019** — Vérifier transparence alpha, bordures, antialiasing et épaisseur des traits.

- [ ] **CANV-020** — Vérifier rendu sans segment cousu à travers un saut/coupe.

- [ ] **CANV-021** — Vérifier continuité des segments et ordre effectif de couture.

- [ ] **CANV-022** — Vérifier coordonnées mm/µm, origine, axes et orientation Y cohérents.

- [ ] **CANV-023** — Vérifier qu'un zoom ne change pas les coordonnées document persistées.

- [ ] **CANV-024** — Vérifier que le rendu respecte les objets cachés et exclus.

- [ ] **CANV-025** — Vérifier que l'aperçu réaliste représente effectivement les paramètres de fil.

- [ ] **CANV-026** — Vérifier cohérence des couleurs rendu versus documents exportés.

- [ ] **CANV-027** — Vérifier mise à jour des overlays indépendamment de la géométrie lourde.

- [ ] **CANV-028** — Vérifier que les avertissements de rendu n'entraînent pas de fenêtre bloquée.

- [ ] **CANV-029** — Vérifier aucun repaint continu en l'absence d'interaction.

- [ ] **CANV-030** — Vérifier comparaison visuelle GPU/CPU sur scène identique.


### CAM — Canevas : navigation, sélection et outils souris (30 vérifications)

- [ ] **CAM-001** — Vérifier pan par outil main, y compris après zoom.

- [ ] **CAM-002** — Vérifier zoom molette ancré sous curseur.

- [ ] **CAM-003** — Vérifier zoom clavier/boutons et ses bornes.

- [ ] **CAM-004** — Vérifier zoom 100 %, ajuster, zoom sélection, zoom rectangle si disponibles.

- [ ] **CAM-005** — Vérifier retour au zoom précédent si disponible.

- [ ] **CAM-006** — Vérifier stabilité des coordonnées au zoom extrême.

- [ ] **CAM-007** — Vérifier absence de saut d'image quand une poignée est saisie.

- [ ] **CAM-008** — Vérifier hit-test d'objets à tous les niveaux de zoom.

- [ ] **CAM-009** — Vérifier sélection simple au clic sur fil/objet.

- [ ] **CAM-010** — Vérifier désélection au clic sur vide.

- [ ] **CAM-011** — Vérifier Ctrl/Maj pour sélection additive et soustractive.

- [ ] **CAM-012** — Vérifier sélection par rectangle en tous sens.

- [ ] **CAM-013** — Vérifier sélection d'objets complètement masqués via l'arbre.

- [ ] **CAM-014** — Vérifier sélection d'objets de très petite taille.

- [ ] **CAM-015** — Vérifier sélection avec objets coïncidents/superposés.

- [ ] **CAM-016** — Vérifier pan/zoom pendant qu'un outil de sélection est actif.

- [ ] **CAM-017** — Vérifier drag d'objet et relâchement = une commande d'historique.

- [ ] **CAM-018** — Vérifier que cliquer sans déplacer ne crée pas de commande vide.

- [ ] **CAM-019** — Vérifier comportement d'une opération interrompue par Échap.

- [ ] **CAM-020** — Vérifier comportement d'une opération interrompue par perte de focus.

- [ ] **CAM-021** — Vérifier seuil de déplacement avant drag correctement calibré.

- [ ] **CAM-022** — Vérifier transformations souris avec verrouillage de proportion selon le cas.

- [ ] **CAM-023** — Vérifier poignées de transformation non interceptées par le canevas.

- [ ] **CAM-024** — Vérifier curseur souris adapté à l'outil et à l'état.

- [ ] **CAM-025** — Vérifier que les overlays suivent le pan/zoom sans retard visuel.

- [ ] **CAM-026** — Vérifier outil actif affiché dans l'interface et clairement désactivable.

- [ ] **CAM-027** — Vérifier clic droit lors d'un geste en cours.

- [ ] **CAM-028** — Vérifier sélection d'un objet pendant la simulation sans corruption.

- [ ] **CAM-029** — Vérifier cohérence sur stylet/tablette graphique si supportés.

- [ ] **CAM-030** — Vérifier comportement du zoom près des limites numériques des coordonnées.


### IMG — Espace Image : import et opérations de préparation (26 vérifications)

- [ ] **IMG-001** — Importer PNG avec transparence, fond uni et photographie.

- [ ] **IMG-002** — Importer JPEG avec orientation EXIF si supportée.

- [ ] **IMG-003** — Importer image très petite, très grande et de dimensions non carrées.

- [ ] **IMG-004** — Importer image à profil ICC / couleurs inhabituelles si supporté.

- [ ] **IMG-005** — Vérifier gestion des erreurs d'image illisible.

- [ ] **IMG-006** — Vérifier dimensions en mm et proportions conservées à l'import.

- [ ] **IMG-007** — Vérifier différence entre hauteur image et hauteur du motif cousu.

- [ ] **IMG-008** — Vérifier choix du fond à éliminer et résultat sur alpha.

- [ ] **IMG-009** — Vérifier recadrage depuis poignées avec ratio imposé/libre selon options.

- [ ] **IMG-010** — Vérifier annulation d'un recadrage sans modification document.

- [ ] **IMG-011** — Vérifier comparaison avant/après et rideau de comparaison.

- [ ] **IMG-012** — Vérifier pile d'opérations d'image dans le bon ordre.

- [ ] **IMG-013** — Vérifier activation, désactivation, suppression et réordonnancement des opérations.

- [ ] **IMG-014** — Vérifier opérations luminosité/contraste/saturation/gamma si proposées.

- [ ] **IMG-015** — Vérifier quantification de couleurs et niveaux de détail si proposés.

- [ ] **IMG-016** — Vérifier CLAHE/nettoyage/lissage si proposés et leur intensité.

- [ ] **IMG-017** — Vérifier prévisualisation instantanée ou progression visible.

- [ ] **IMG-018** — Vérifier image source originale conservée et non réécrite.

- [ ] **IMG-019** — Vérifier undo/redo pour chaque opération de préparation.

- [ ] **IMG-020** — Vérifier relance d'opération après réglage sans résultat périmé.

- [ ] **IMG-021** — Vérifier barre d'outils Image entièrement accessible en petite fenêtre.

- [ ] **IMG-022** — Vérifier panneaux Image non superposés à la zone utile sans intention.

- [ ] **IMG-023** — Vérifier cohérence du panneau Image avec le thème et la densité.

- [ ] **IMG-024** — Vérifier prétraitement déclenche bien invalidation segmentation aval.

- [ ] **IMG-025** — Vérifier export ou sauvegarde intermédiaire d'image quand proposée.

- [ ] **IMG-026** — Vérifier réouverture OSP rétablissant la pile d'opérations d'image.


### SEG — Espace Segmentation : régions et corrections (34 vérifications)

- [ ] **SEG-001** — Lancer segmentation sur image appropriée.

- [ ] **SEG-002** — Vérifier état vide sans image et action indisponible justifiée.

- [ ] **SEG-003** — Vérifier choix nombre maximal de couleurs.

- [ ] **SEG-004** — Vérifier seuil de plus petite région.

- [ ] **SEG-005** — Vérifier lissage des contours et suppression du bruit.

- [ ] **SEG-006** — Vérifier segmentation sur image transparente et bords de l'image.

- [ ] **SEG-007** — Vérifier segmentation sur zones proches en couleur.

- [ ] **SEG-008** — Vérifier stabilité/idempotence pour mêmes paramètres et image.

- [ ] **SEG-009** — Vérifier aperçu des zones avant application.

- [ ] **SEG-010** — Vérifier application ne modifiant jamais l'image source.

- [ ] **SEG-011** — Vérifier sélection d'une région au clic sur sa couche.

- [ ] **SEG-012** — Vérifier sélection de régions proches ou très petites.

- [ ] **SEG-013** — Vérifier régions masquées, visibles, verrouillées si supportées.

- [ ] **SEG-014** — Vérifier navigation région précédente/suivante.

- [ ] **SEG-015** — Vérifier recoloration d'une région.

- [ ] **SEG-016** — Vérifier restauration de la couleur originale d'une région.

- [ ] **SEG-017** — Vérifier fusion de régions en choisissant explicitement cible et source.

- [ ] **SEG-018** — Vérifier découpe d'une région par couteau (cas « pont » étroit).

- [ ] **SEG-019** — Vérifier pinceau de correction et variation de taille.

- [ ] **SEG-020** — Vérifier pot de peinture, tolérance et propagation si supportés.

- [ ] **SEG-021** — Vérifier ajout/suppression de zones de détail.

- [ ] **SEG-022** — Vérifier séparation de zones qui doivent avoir directions de fil différentes.

- [ ] **SEG-023** — Vérifier absence de fuite de couleurs ou d'îlots parasites.

- [ ] **SEG-024** — Vérifier chaque correction remonte en statut segmentation « corrections en attente ».

- [ ] **SEG-025** — Vérifier vectorisation de la sélection depuis segmentation.

- [ ] **SEG-026** — Vérifier stratégie de mise à jour des vecteurs après modification des régions.

- [ ] **SEG-027** — Vérifier préservation des objets manuels sans région source.

- [ ] **SEG-028** — Vérifier confirmation avant perte de retouches sur régénération.

- [ ] **SEG-029** — Vérifier une seule étape undo pour chaque correction logique.

- [ ] **SEG-030** — Vérifier sauvegarde/réouverture de la segmentation technique dans OSP.

- [ ] **SEG-031** — Vérifier absence de recalcul SAM/IA non nécessaire à réouverture.

- [ ] **SEG-032** — Vérifier affichage couche régions par-dessus/au-dessous image selon réglages.

- [ ] **SEG-033** — Vérifier calcul et affichage du nombre de régions et couleurs.

- [ ] **SEG-034** — Vérifier fonctionnement avec dix couleurs et régions complexes sur benchmark.


### AISEG — Segmentation assistée par IA / SAM (17 vérifications)

- [ ] **AISEG-001** — Vérifier disponibilité réelle du backend déclaré et message en cas d'absence.

- [ ] **AISEG-002** — Vérifier distinction entre segmentation locale et distante.

- [ ] **AISEG-003** — Vérifier chargement de modèle sans blocage de la fenêtre.

- [ ] **AISEG-004** — Vérifier premier téléchargement/mise en cache si supporté.

- [ ] **AISEG-005** — Vérifier action sur zone/point/boîte de requête si supportée.

- [ ] **AISEG-006** — Vérifier réexécution avec prompt modifié.

- [ ] **AISEG-007** — Vérifier raccord des masques IA aux couleurs et objets logiques.

- [ ] **AISEG-008** — Vérifier gestion de masque vide ou multiple inattendu.

- [ ] **AISEG-009** — Vérifier résultat ne fusionnant pas silencieusement zones sémantiques essentielles.

- [ ] **AISEG-010** — Vérifier correction manuelle possible après résultat IA.

- [ ] **AISEG-011** — Vérifier révisions et annulation après acceptation.

- [ ] **AISEG-012** — Vérifier indicateur de progression et possibilité d'annuler.

- [ ] **AISEG-013** — Vérifier timeout, erreur d'authentification, absence de réseau ou modèle absent.

- [ ] **AISEG-014** — Vérifier résultat reçu après remplacement du document rejeté.

- [ ] **AISEG-015** — Vérifier aucun secret IA visible dans logs, captures ou fichiers projet.

- [ ] **AISEG-016** — Vérifier utilisation explicite des services IA et consentement/indication réseau.

- [ ] **AISEG-017** — Vérifier provenance du masque IA et reproductibilité documentées.


### VEC — Espace Vectorisation : formes, édition et topologie (30 vérifications)

- [ ] **VEC-001** — Vérifier vectorisation de toutes les régions pertinentes.

- [ ] **VEC-002** — Vérifier vectorisation d'une seule région sélectionnée.

- [ ] **VEC-003** — Vérifier import SVG et conservation des dimensions.

- [ ] **VEC-004** — Vérifier import DXF et contours s'il est supporté.

- [ ] **VEC-005** — Vérifier rendu des vecteurs sur image, sans décalage.

- [ ] **VEC-006** — Vérifier dessin rectangle, ellipse, cercle, polygone.

- [ ] **VEC-007** — Vérifier dessin libre/plume/Bézier selon outils présents.

- [ ] **VEC-008** — Vérifier fermeture de contour et détection de forme ouverte.

- [ ] **VEC-009** — Vérifier édition de nœud : sélectionner, déplacer, ajouter, supprimer.

- [ ] **VEC-010** — Vérifier poignées et tangentes Bézier si disponibles.

- [ ] **VEC-011** — Vérifier jointures douces/angles/cusps si disponibles.

- [ ] **VEC-012** — Vérifier fusion/découpe de chemins et opérations booléennes si disponibles.

- [ ] **VEC-013** — Vérifier simplification des chemins et tolérance.

- [ ] **VEC-014** — Vérifier lissage sans effacement de détails significatifs.

- [ ] **VEC-015** — Vérifier nettoyage des micro-îlots et seuil de compacité si disponible.

- [ ] **VEC-016** — Vérifier création de trous et conservation des évidements.

- [ ] **VEC-017** — Vérifier orientation des contours et gestion des auto-intersections.

- [ ] **VEC-018** — Vérifier ordre Z des vecteurs et ordre de sélection.

- [ ] **VEC-019** — Vérifier transformations déplacer/rotation/échelle/miroir.

- [ ] **VEC-020** — Vérifier aligner, distribuer, accrochage grille/guides/noeuds si disponibles.

- [ ] **VEC-021** — Vérifier copie/collage/duplication et identité distincte des objets.

- [ ] **VEC-022** — Vérifier objets manuels non détruits après régénération segmentation.

- [ ] **VEC-023** — Vérifier lien `source_region` conservé pour objets générés.

- [ ] **VEC-024** — Vérifier disparition d'une région source correctement marquée obsolète.

- [ ] **VEC-025** — Vérifier inspecteur `Calques`/`Édition` et scroll réellement utilisables.

- [ ] **VEC-026** — Vérifier contrôles de cette barre cliquables par vrais événements souris.

- [ ] **VEC-027** — Vérifier action « créer broderie depuis vecteur » et état aval.

- [ ] **VEC-028** — Vérifier un vecteur hors cadre signalé sans perte de coordonnées.

- [ ] **VEC-029** — Vérifier formes avec coordonnées extrêmes et très petites géométries.

- [ ] **VEC-030** — Vérifier undo/redo et aller-retour OSP de l'édition de forme.


### OBJ — Objets, calques, arbre, sélection et transformations (27 vérifications)

- [ ] **OBJ-001** — Vérifier arbre d'objets conforme au document réel.

- [ ] **OBJ-002** — Vérifier sélection arbre → canevas/inspecteur.

- [ ] **OBJ-003** — Vérifier sélection canevas → arbre/inspecteur.

- [ ] **OBJ-004** — Vérifier multi-sélection par Ctrl/Maj dans l'arbre.

- [ ] **OBJ-005** — Vérifier état partiellement sélectionné sur des groupes si applicable.

- [ ] **OBJ-006** — Vérifier renommage simple et en lot si disponible.

- [ ] **OBJ-007** — Vérifier visibility/mute/lock avec feedback immédiat.

- [ ] **OBJ-008** — Vérifier objet verrouillé non modifiable par geste ou raccourci.

- [ ] **OBJ-009** — Vérifier groupe, dégroupe et hiérarchie si supportés.

- [ ] **OBJ-010** — Vérifier ordre des calques et ordre de couture sans confusion.

- [ ] **OBJ-011** — Vérifier drag & drop d'objets dans l'arbre sans crash.

- [ ] **OBJ-012** — Vérifier menu contextuel d'objet avec/sans sélection multiple.

- [ ] **OBJ-013** — Vérifier supprimer objet sélectionné puis annuler.

- [ ] **OBJ-014** — Vérifier dupliquer un ou plusieurs objets avec nouveaux identifiants.

- [ ] **OBJ-015** — Vérifier couper/copier/coller entre objets et documents selon support.

- [ ] **OBJ-016** — Vérifier noms et couleurs actualisés lors d'un changement rapide de sélection.

- [ ] **OBJ-017** — Vérifier inspecteur affiche état mixte pour valeurs différentes.

- [ ] **OBJ-018** — Vérifier édition d'une valeur mixte modifiant seulement les objets éligibles.

- [ ] **OBJ-019** — Vérifier déplacement par flèches 1 mm et Maj+flèches 5 mm si contrat actif.

- [ ] **OBJ-020** — Vérifier déplacement par souris en coordonnées exactes.

- [ ] **OBJ-021** — Vérifier redimensionnement multi-objets et transformation autour du bon pivot.

- [ ] **OBJ-022** — Vérifier gestion d'un objet totalement masqué au canevas mais dans l'arbre.

- [ ] **OBJ-023** — Vérifier conservation de la sélection après undo/redo et suppression/restauration.

- [ ] **OBJ-024** — Vérifier absence de collisions d'identifiants 64 bits passés en chaînes QML.

- [ ] **OBJ-025** — Vérifier performance de l'arbre avec des milliers d'objets.

- [ ] **OBJ-026** — Vérifier scroll et virtualisation de listes longues.

- [ ] **OBJ-027** — Vérifier état de l'arbre après document remplacé pendant un filtre actif.


### STCH — Espace Points : génération et paramètres généraux (28 vérifications)

- [ ] **STCH-001** — Vérifier génération de points à partir de régions vectorisées.

- [ ] **STCH-002** — Vérifier génération de points depuis formes manuelles.

- [ ] **STCH-003** — Vérifier objet sans points et message explicite.

- [ ] **STCH-004** — Vérifier changement de type de point et prévisualisation.

- [ ] **STCH-005** — Vérifier séquence effective = unique source des points rendus/exportés.

- [ ] **STCH-006** — Vérifier aucune génération lourde à chaque pan/zoom.

- [ ] **STCH-007** — Vérifier changement de densité et nombre de points résultant.

- [ ] **STCH-008** — Vérifier angle de remplissage et conventions de signe.

- [ ] **STCH-009** — Vérifier longueur maximale et minimale de point.

- [ ] **STCH-010** — Vérifier largeur minimale/maximale et contraintes du type.

- [ ] **STCH-011** — Vérifier sous-couche activée, désactivée et paramétrée.

- [ ] **STCH-012** — Vérifier compensation de tirage manuel et prédictif si disponibles.

- [ ] **STCH-013** — Vérifier séquence de points déterministe pour entrée identique.

- [ ] **STCH-014** — Vérifier type/paramètres absents du `param_spec` exposés ou déclarés ABSENT.

- [ ] **STCH-015** — Vérifier champs `handmade`, `seed`, `density_gradient`, `center_mm`, `pull_*`, `caps` si supportés.

- [ ] **STCH-016** — Vérifier bornes et unités de tous les paramètres de point.

- [ ] **STCH-017** — Vérifier une révision provoque l'invalidation du rendu adéquat.

- [ ] **STCH-018** — Vérifier inspector multi-sélection « valeur mixte » de tous les paramètres.

- [ ] **STCH-019** — Vérifier application d'un changement uniquement aux types compatibles.

- [ ] **STCH-020** — Vérifier messages intelligibles pour combinaisons géométriques impossibles.

- [ ] **STCH-021** — Vérifier calcul de points rapide et tâche annulable pour projet lourd.

- [ ] **STCH-022** — Vérifier ordre de génération respectant ordre du document.

- [ ] **STCH-023** — Vérifier affichage des sauts, coupes, points cousus séparément.

- [ ] **STCH-024** — Vérifier contraintes de cadre et sortie hors cadre.

- [ ] **STCH-025** — Vérifier sauvegarde/rechargement conservant paramètres, pas seulement aperçu.

- [ ] **STCH-026** — Vérifier cohérence du nombre de points analyse/simulation/export.

- [ ] **STCH-027** — Vérifier panneau Points et ses barres entièrement accessibles au scroll.

- [ ] **STCH-028** — Vérifier statut des outils actifs et raccourcis de numérisation.


### FILL — Familles de points et modes de remplissage (24 vérifications)

- [ ] **FILL-001** — Vérifier point droit/run avec longueur, finitions et chemins ouverts.

- [ ] **FILL-002** — Vérifier tatami avec densité, angle et gestion des trous.

- [ ] **FILL-003** — Vérifier satin standard sur colonnes étroites.

- [ ] **FILL-004** — Vérifier satin automatique sur formes compatibles/incompatibles.

- [ ] **FILL-005** — Vérifier remplissage spirale sur disque et forme irrégulière.

- [ ] **FILL-006** — Vérifier centre spirale explicitement fixé, notamment (0,0).

- [ ] **FILL-007** — Vérifier spirale sans coupes inutiles sur tracé continu.

- [ ] **FILL-008** — Vérifier remplissage radial sur objets circulaires/irréguliers.

- [ ] **FILL-009** — Vérifier radial sans retrait de bord lorsque réglé à zéro.

- [ ] **FILL-010** — Vérifier remplissage directionnel et variation locale d'angle.

- [ ] **FILL-011** — Vérifier fonction paramétrique d'orientation `g(x)` si proposée.

- [ ] **FILL-012** — Vérifier contour / contour_fill / concentrique si disponibles.

- [ ] **FILL-013** — Vérifier motif fill et motif sur contour si disponibles.

- [ ] **FILL-014** — Vérifier crosshatch/croisé si disponible.

- [ ] **FILL-015** — Vérifier blanket/bordure couverture si disponible.

- [ ] **FILL-016** — Vérifier point de croix si disponible.

- [ ] **FILL-017** — Vérifier appliqué avec séquence de positionnement, fixation et finition.

- [ ] **FILL-018** — Vérifier formes avec îlots, évidements et bordures fines.

- [ ] **FILL-019** — Vérifier transitions inter-types et compatibilité de paramètres.

- [ ] **FILL-020** — Vérifier points extrêmes/dégénérés sans NaN ni crash.

- [ ] **FILL-021** — Vérifier générateur stable lors de changement rapide de type.

- [ ] **FILL-022** — Vérifier présence UI de toutes familles déclarées par le moteur.

- [ ] **FILL-023** — Vérifier rendu réaliste différenciant visuellement les types de point.

- [ ] **FILL-024** — Vérifier qu'aucun type UI n'est une coquille vide.


### SAT — Satin : rails, axes, guides et finitions (22 vérifications)

- [ ] **SAT-001** — Vérifier création de deux rails satin.

- [ ] **SAT-002** — Vérifier reconnaissance de l'ordre et du sens des rails.

- [ ] **SAT-003** — Vérifier affichage clair de la largeur entre rails.

- [ ] **SAT-004** — Vérifier ajout/suppression/déplacement de nœuds.

- [ ] **SAT-005** — Vérifier tangentes Bézier et continuité si supportées.

- [ ] **SAT-006** — Vérifier insertion et déplacement des barreaux/guides.

- [ ] **SAT-007** — Vérifier gestion des rails qui se croisent.

- [ ] **SAT-008** — Vérifier gestion des rails inversés ou qui divergent.

- [ ] **SAT-009** — Vérifier extrémités pointues, plates et arrondies selon options.

- [ ] **SAT-010** — Vérifier largeur maximum et conversion conseillée vers tatami.

- [ ] **SAT-011** — Vérifier paramètres de sous-couche satin.

- [ ] **SAT-012** — Vérifier compensation de tirage par côté.

- [ ] **SAT-013** — Vérifier densité satin et limitation des points trop longs.

- [ ] **SAT-014** — Vérifier fractionnement de points sur satin large.

- [ ] **SAT-015** — Vérifier évolution visuelle du rail pendant le drag.

- [ ] **SAT-016** — Vérifier une seule commande undo par geste complet.

- [ ] **SAT-017** — Vérifier annulation par Échap sans corruption géométrique.

- [ ] **SAT-018** — Vérifier choix de stratégie auto-satin parmi variantes réellement existantes.

- [ ] **SAT-019** — Vérifier conservation des rails lors de sauvegarde OSP.

- [ ] **SAT-020** — Vérifier rendu et export DST cohérents avec les rails visibles.

- [ ] **SAT-021** — Vérifier sélection d'une poignée près d'autres vecteurs.

- [ ] **SAT-022** — Vérifier transformation du satin sur multi-objets.


### FADE — Fondu entre deux fils, mélange et transitions (20 vérifications)

- [ ] **FADE-001** — Vérifier création d'un fondu entre deux fils compatibles.

- [ ] **FADE-002** — Vérifier choix du fil source et du fil cible.

- [ ] **FADE-003** — Vérifier axe début/fin du fondu sur le canevas.

- [ ] **FADE-004** — Vérifier déplacement et rotation de l'axe.

- [ ] **FADE-005** — Vérifier inversion du sens du dégradé.

- [ ] **FADE-006** — Vérifier longueur, densité, courbe ou profil si exposés.

- [ ] **FADE-007** — Vérifier affichage de la zone affectée avant validation.

- [ ] **FADE-008** — Vérifier aperçu du mélange des deux fils à zoom variable.

- [ ] **FADE-009** — Vérifier aucune couleur virtuelle inventée comme fil physique.

- [ ] **FADE-010** — Vérifier ordre effectif des points pour les deux fils.

- [ ] **FADE-011** — Vérifier sauts et coupes à la transition raisonnablement maîtrisés.

- [ ] **FADE-012** — Vérifier paramètres hors plage ou transition dégénérée.

- [ ] **FADE-013** — Vérifier aperçu juste avant/après modification.

- [ ] **FADE-014** — Vérifier résultat sans artefact aux limites des deux régions.

- [ ] **FADE-015** — Vérifier interaction de fondu avec sous-couche et tirage.

- [ ] **FADE-016** — Vérifier annulation/rétablissement d'un fondu entier.

- [ ] **FADE-017** — Vérifier sauvegarde/réouverture du fondu.

- [ ] **FADE-018** — Vérifier diagnostics et export reflètent réellement le fondu.

- [ ] **FADE-019** — Vérifier multi-sélection/édition de plusieurs fondus si possible.

- [ ] **FADE-020** — Vérifier outil disponible uniquement dans le bon espace et sans perte du geste.


### EDIT — Retouche de points, outils de précision et géométrie (20 vérifications)

- [ ] **EDIT-001** — Vérifier activation et sortie du mode édition des points.

- [ ] **EDIT-002** — Vérifier sélection d'un point par clic avec tolérance adaptée au zoom.

- [ ] **EDIT-003** — Vérifier multi-sélection de points et plage continue si disponible.

- [ ] **EDIT-004** — Vérifier sélection de tous les points d'un objet.

- [ ] **EDIT-005** — Vérifier déplacement d'un ou plusieurs points.

- [ ] **EDIT-006** — Vérifier suppression puis restauration de points.

- [ ] **EDIT-007** — Vérifier basculement point cousu ↔ saut si disponible.

- [ ] **EDIT-008** — Vérifier insertion d'une coupe après un point.

- [ ] **EDIT-009** — Vérifier déplacement de points préservant indices et ordre.

- [ ] **EDIT-010** — Vérifier retouche de points maintenue après modification non destructrice.

- [ ] **EDIT-011** — Vérifier avertissement avant régénération détruisant des retouches.

- [ ] **EDIT-012** — Vérifier annulation/rétablissement des retouches par geste.

- [ ] **EDIT-013** — Vérifier affichage du point actif et des voisins.

- [ ] **EDIT-014** — Vérifier que la simulation pointe le bon point après édition.

- [ ] **EDIT-015** — Vérifier que l'export machine contient les overrides de points.

- [ ] **EDIT-016** — Vérifier reprise OSP des overrides sans recalcul inattendu.

- [ ] **EDIT-017** — Vérifier édition de points très denses sans sélection d'un voisin erroné.

- [ ] **EDIT-018** — Vérifier retouche dans un objet caché/verrouillé impossible.

- [ ] **EDIT-019** — Vérifier coupe, saut et point ne sont pas confondus visuellement.

- [ ] **EDIT-020** — Vérifier indice du point et coordonnées affichés fidèlement.


### THR — Fils, palettes, ordre, couleurs et réduction (21 vérifications)

- [ ] **THR-001** — Vérifier création, sélection et suppression de fil.

- [ ] **THR-002** — Vérifier nom, identifiant, couleur HEX, marque et référence si existants.

- [ ] **THR-003** — Vérifier nuanciers intégrés et recherche de couleur.

- [ ] **THR-004** — Vérifier import de nuancier CSV/JSON si disponible.

- [ ] **THR-005** — Vérifier export de liste de fils CSV si disponible.

- [ ] **THR-006** — Vérifier ajout de fil sans modifier les autres objets.

- [ ] **THR-007** — Vérifier réattribution d'objets à un fil.

- [ ] **THR-008** — Vérifier changement de couleur dans l'arbre, le canevas et l'analyse.

- [ ] **THR-009** — Vérifier ordre des bobines et synchronisation avec ordre de couture.

- [ ] **THR-010** — Vérifier réduction de palette à N fils avec aperçu avant/après.

- [ ] **THR-011** — Vérifier réduction à zéro/un/deux fils refusée ou gérée selon contraintes.

- [ ] **THR-012** — Vérifier distance couleur et critère de fusion des voisines.

- [ ] **THR-013** — Vérifier réduction préservant au mieux les détails de petites régions.

- [ ] **THR-014** — Vérifier fusion de fils identiques et noms/références associés.

- [ ] **THR-015** — Vérifier restauration après réduction via undo.

- [ ] **THR-016** — Vérifier le nombre de changements de couleur dans l'export.

- [ ] **THR-017** — Vérifier que les couleurs document ne sont pas modifiées par le thème UI.

- [ ] **THR-018** — Vérifier contraste entre deux fils très proches en vue d'analyse.

- [ ] **THR-019** — Vérifier réouverture OSP conservant les choix de fils.

- [ ] **THR-020** — Vérifier lisibilité des swatches sur clair/sombre et dans l'état désactivé.

- [ ] **THR-021** — Vérifier cohérence entre fil visible, fil actif et objet sélectionné.


### HOOP — Cadres, unités, placements et contraintes machine (20 vérifications)

- [ ] **HOOP-001** — Vérifier dimensions du cadre en mm, largeur et hauteur inversées si rotation.

- [ ] **HOOP-002** — Vérifier placement au centre et translation précise.

- [ ] **HOOP-003** — Vérifier import image à 100×100 mm et comparaison avec motif cousu.

- [ ] **HOOP-004** — Vérifier formats 240×240 mm, 300×240 mm et hauteur 330 mm.

- [ ] **HOOP-005** — Vérifier avertissement hors cadre pour un seul objet.

- [ ] **HOOP-006** — Vérifier avertissement hors cadre pour plusieurs objets.

- [ ] **HOOP-007** — Vérifier objet partiellement au-delà du cadre.

- [ ] **HOOP-008** — Vérifier objet sur la limite du cadre à epsilon près.

- [ ] **HOOP-009** — Vérifier repères/grille/règles cohérents avec le document.

- [ ] **HOOP-010** — Vérifier 100 %/taille réelle et calibration de l'écran si disponible.

- [ ] **HOOP-011** — Vérifier changement de cadre n'efface pas les réglages de points.

- [ ] **HOOP-012** — Vérifier sélection de machine/profil machine si présent.

- [ ] **HOOP-013** — Vérifier capacités machine : dimensions, points, sauts, coupes, fils.

- [ ] **HOOP-014** — Vérifier contraintes du format DST et autres formats exportés.

- [ ] **HOOP-015** — Vérifier prise en compte orientation d'origine et inversion Y.

- [ ] **HOOP-016** — Vérifier possibilité de choisir origine machine et point de départ.

- [ ] **HOOP-017** — Vérifier contrôles de production réinitialisés après modification.

- [ ] **HOOP-018** — Vérifier contrôle physique distinct du diagnostic purement visuel.

- [ ] **HOOP-019** — Vérifier warnings non masqués lors de l'export hors cadre.

- [ ] **HOOP-020** — Vérifier document en pouces à import/export si unité alternative disponible.


### ANL — Analyse de brodabilité, diagnostics et auto-corrections (26 vérifications)

- [ ] **ANL-001** — Vérifier accès au panneau Analyse depuis Points et depuis la palette.

- [ ] **ANL-002** — Vérifier calcul du nombre de points sur la révision actuelle.

- [ ] **ANL-003** — Vérifier estimation sauts, coupes, déplacements et changements de fil.

- [ ] **ANL-004** — Vérifier longueur minimum/maximum des points.

- [ ] **ANL-005** — Vérifier zones trop denses et sous-denses.

- [ ] **ANL-006** — Vérifier satin trop large et géométries incompatibles.

- [ ] **ANL-007** — Vérifier fils/objets hors cadre.

- [ ] **ANL-008** — Vérifier objets nuls, formes ouvertes et points inexistants.

- [ ] **ANL-009** — Vérifier diagnostic de croisements et recouvrements si supporté.

- [ ] **ANL-010** — Vérifier carte des tensions/tirage si disponible.

- [ ] **ANL-011** — Vérifier distinction modèle prédictif non calibré vs mesure réelle.

- [ ] **ANL-012** — Vérifier surimpression des risques correctement localisée au canevas.

- [ ] **ANL-013** — Vérifier clic sur défaut → sélection et centrage du bon objet.

- [ ] **ANL-014** — Vérifier tri/filtre par sévérité et objet si disponible.

- [ ] **ANL-015** — Vérifier absence de diagnostic périmé après toute modification.

- [ ] **ANL-016** — Vérifier qu'un résultat asynchrone de révision ancienne est rejeté.

- [ ] **ANL-017** — Vérifier bouton « Corriger » affiche les propositions réellement applicables.

- [ ] **ANL-018** — Vérifier correction unitaire avec prévisualisation si offerte.

- [ ] **ANL-019** — Vérifier « corriger tout ce qui est sûr » n'applique pas d'opération ambiguë.

- [ ] **ANL-020** — Vérifier correction acceptée en une seule étape undo.

- [ ] **ANL-021** — Vérifier correction refusée si la proposition devient périmée.

- [ ] **ANL-022** — Vérifier correction MoveIntoHoop, ClampStitchLength, SplitLongStitches, EnableFinishing selon support.

- [ ] **ANL-023** — Vérifier réduction du nombre de fils proposée avec justification/impact.

- [ ] **ANL-024** — Vérifier aucune correction n'altère le moteur ou les fichiers silencieusement.

- [ ] **ANL-025** — Vérifier recalcul du diagnostic après correction et baisse effective du défaut.

- [ ] **ANL-026** — Vérifier absence de promesse de compatibilité physique sans test machine.


### SIM — Simulation de couture et contrôle temporel (20 vérifications)

- [ ] **SIM-001** — Vérifier démarrage, pause, reprise et arrêt de la simulation.

- [ ] **SIM-002** — Vérifier bouton simulation visible et cliquable en fenêtre réduite.

- [ ] **SIM-003** — Vérifier barre de progression et position réelle dans la séquence.

- [ ] **SIM-004** — Vérifier navigation point suivant/précédent.

- [ ] **SIM-005** — Vérifier navigation objet suivant/précédent.

- [ ] **SIM-006** — Vérifier navigation fil/couleur suivant/précédent.

- [ ] **SIM-007** — Vérifier saut vers un pourcentage ou un index de point.

- [ ] **SIM-008** — Vérifier première et dernière positions incluses.

- [ ] **SIM-009** — Vérifier changement de vitesse sans dérive des indices.

- [ ] **SIM-010** — Vérifier rendu des sauts et coupes pendant la simulation.

- [ ] **SIM-011** — Vérifier ordre visuel de superposition des fils.

- [ ] **SIM-012** — Vérifier indicateur de couleur/objet courant.

- [ ] **SIM-013** — Vérifier arrêt ou invalidation propre sur modification du document.

- [ ] **SIM-014** — Vérifier reprise sur un autre espace selon contrat voulu.

- [ ] **SIM-015** — Vérifier pause lors de mise en arrière-plan si nécessaire.

- [ ] **SIM-016** — Vérifier simulation sans fichier et avec séquence vide.

- [ ] **SIM-017** — Vérifier simulation sur 100 000+ points sans blocage.

- [ ] **SIM-018** — Vérifier fermeture/rechargement document pendant une simulation.

- [ ] **SIM-019** — Vérifier synchronisation des compteurs avec l'export effectif.

- [ ] **SIM-020** — Vérifier pas de timer restant actif après fermeture de la fenêtre.


### OPT — Optimisation d'ordre et réduction des coupes (18 vérifications)

- [ ] **OPT-001** — Vérifier action optimiser accessible dans l'interface cible.

- [ ] **OPT-002** — Vérifier stratégies d'optimisation offertes par le moteur.

- [ ] **OPT-003** — Vérifier mesures avant/après calculées sur même révision.

- [ ] **OPT-004** — Vérifier comparaison nombre de coupes, sauts, changements et longueur déplacée.

- [ ] **OPT-005** — Vérifier optimisation préservant l'identité des objets.

- [ ] **OPT-006** — Vérifier ordre de couture modifié uniquement après approbation.

- [ ] **OPT-007** — Vérifier « aucune amélioration » annoncé honnêtement.

- [ ] **OPT-008** — Vérifier choix de stratégie incompatible refusé proprement.

- [ ] **OPT-009** — Vérifier préservation des contraintes de dessus/dessous et raccords.

- [ ] **OPT-010** — Vérifier absence d'optimisation traversant volontairement une zone interdite.

- [ ] **OPT-011** — Vérifier traitement de couches de même couleur éloignées.

- [ ] **OPT-012** — Vérifier gestion de contours et remplissages dépendants.

- [ ] **OPT-013** — Vérifier application en une seule étape undo.

- [ ] **OPT-014** — Vérifier rejet d'un plan d'optimisation devenu périmé.

- [ ] **OPT-015** — Vérifier calcul annulable et progressif sur motif lourd.

- [ ] **OPT-016** — Vérifier équivalence de points finaux après annulation/rétablissement.

- [ ] **OPT-017** — Vérifier export DST ne réintroduisant pas des coupes superflues.

- [ ] **OPT-018** — Vérifier résultat déterministe pour paramètres et projet identiques.


### EXP — Export, formats, production et fichiers dérivés (28 vérifications)

- [ ] **EXP-001** — Vérifier export DST depuis un document valide.

- [ ] **EXP-002** — Vérifier préconditions et messages pour document vide.

- [ ] **EXP-003** — Vérifier confirmation des avertissements machine avant export.

- [ ] **EXP-004** — Vérifier contrôle machine sur la révision exportée.

- [ ] **EXP-005** — Vérifier mise à jour de l'état « exporté » après réussite réelle.

- [ ] **EXP-006** — Vérifier que l'export échoué ne marque pas « exporté ».

- [ ] **EXP-007** — Vérifier DST re-parsable et contenu cohérent avec aperçu.

- [ ] **EXP-008** — Vérifier nombre de points, sauts, coupes et fils dans DST.

- [ ] **EXP-009** — Vérifier dimensions brodées et origin offsets dans DST.

- [ ] **EXP-010** — Vérifier export ne contient pas d'objet masqué.

- [ ] **EXP-011** — Vérifier export respecte les retouches de points.

- [ ] **EXP-012** — Vérifier export respecte compensation tirage et finitions.

- [ ] **EXP-013** — Vérifier export avec derniers réglages si disponible.

- [ ] **EXP-014** — Vérifier export PES/EXP/JEF/VP3 et autres uniquement si déclarés supportés.

- [ ] **EXP-015** — Vérifier import machine DST/PES et retour si supporté.

- [ ] **EXP-016** — Vérifier export SVG/DXF si supportés.

- [ ] **EXP-017** — Vérifier rendu réaliste PNG à résolution spécifiée.

- [ ] **EXP-018** — Vérifier image technique de segmentation PNG si disponible.

- [ ] **EXP-019** — Vérifier prévisualisation/impression gabarit 1:1 si supportées.

- [ ] **EXP-020** — Vérifier fiche de production avec cadre, dimensions, fils et notes si supportée.

- [ ] **EXP-021** — Vérifier contour de découpe d'appliqué si disponible.

- [ ] **EXP-022** — Vérifier absence d'écrasement imprévu d'un fichier export existant.

- [ ] **EXP-023** — Vérifier chemin Unicode et nom de fichier long.

- [ ] **EXP-024** — Vérifier erreur disque plein, fichier verrouillé ou répertoire interdit.

- [ ] **EXP-025** — Vérifier comportement de l'export après modification non sauvegardée.

- [ ] **EXP-026** — Vérifier correspondance entre taille « motif » et taille « image » mentionnée.

- [ ] **EXP-027** — Vérifier éventuelles coupures de fil automatiques et paramétrage de machine.

- [ ] **EXP-028** — Vérifier ordre et gestion des changements de couleur aux limites du format.


### IO — Fichiers projet, OSP, import, sauvegarde et récupération (32 vérifications)

- [ ] **IO-001** — Créer un projet puis enregistrer sous nouveau nom.

- [ ] **IO-002** — Ouvrir un `.osp` réel issu d'une version antérieure.

- [ ] **IO-003** — Ouvrir `.osp` récent avec images, segmentation, vecteurs, fils et points.

- [ ] **IO-004** — Vérifier séquence OSP → chargement → OSP sans altération sémantique.

- [ ] **IO-005** — Vérifier DST exporté avant et après sauvegarde/rechargement identique si aucune modification.

- [ ] **IO-006** — Vérifier Ctrl+S sur projet nouveau ouvre choix de chemin.

- [ ] **IO-007** — Vérifier Ctrl+S sur projet existant écrit au bon endroit.

- [ ] **IO-008** — Vérifier Enregistrer sous vers nouveau chemin sans modifier l'original.

- [ ] **IO-009** — Vérifier annuler un dialogue de sauvegarde n'efface pas l'état dirty.

- [ ] **IO-010** — Vérifier état dirty exact après modifications/undo/redo.

- [ ] **IO-011** — Vérifier proposition sauvegarder/ignorer/annuler avant ouverture d'un autre projet.

- [ ] **IO-012** — Vérifier proposition sauvegarder/ignorer/annuler avant nouveau projet.

- [ ] **IO-013** — Vérifier proposition sauvegarder/ignorer/annuler à la fermeture.

- [ ] **IO-014** — Vérifier choix « Annuler » laisse document et sélection inchangés.

- [ ] **IO-015** — Vérifier sauvegarde échouée ne marque pas projet propre.

- [ ] **IO-016** — Vérifier sauvegarde atomique/stratégie contre corruption si implémentée.

- [ ] **IO-017** — Vérifier sauvegarde de document volumineux sans blocage prolongé.

- [ ] **IO-018** — Vérifier ouverture d'un projet incompatible avec message de version clair.

- [ ] **IO-019** — Vérifier projet partiellement corrompu avec chemin d'erreur sans crash.

- [ ] **IO-020** — Vérifier protection contre chemins relatifs mal résolus.

- [ ] **IO-021** — Vérifier fichiers récents : tri, doublons, accès manquants, nettoyage.

- [ ] **IO-022** — Vérifier glisser-déposer de fichiers sur fenêtre/canevas si prévu.

- [ ] **IO-023** — Vérifier importer/insérer sans remplacer le projet par accident.

- [ ] **IO-024** — Vérifier ouverture de plusieurs instances et éventuel verrouillage document.

- [ ] **IO-025** — Vérifier modification externe du fichier et conflit à l'enregistrement.

- [ ] **IO-026** — Vérifier récupération crash/autosave si fonction annoncée.

- [ ] **IO-027** — Vérifier aucun fichier temporaire orphelin après annulation.

- [ ] **IO-028** — Vérifier chemin contenant espaces, émojis, accents, apostrophes.

- [ ] **IO-029** — Vérifier chemin réseau lent, déconnecté ou non accessible.

- [ ] **IO-030** — Vérifier sauvegarde source image/segmentation dans OSP selon contrat.

- [ ] **IO-031** — Vérifier qu'aucun test ne modifie des fixtures du dépôt sur disque.

- [ ] **IO-032** — Vérifier réouverture conserve espace courant seulement si contrat voulu.


### HIST — Historique, transactions et modèle de document (21 vérifications)

- [ ] **HIST-001** — Vérifier une opération logique = une entrée d'historique.

- [ ] **HIST-002** — Vérifier aucune entrée pour clic sans changement.

- [ ] **HIST-003** — Vérifier une rafale d'un glisser = une entrée, non un point par mouvement.

- [ ] **HIST-004** — Vérifier sélection seule ne salit pas le document.

- [ ] **HIST-005** — Vérifier caméra/pan/zoom seuls ne salissent pas le document.

- [ ] **HIST-006** — Vérifier undo restauration géométrie, couleur et paramètres complets.

- [ ] **HIST-007** — Vérifier redo reproduit identiquement l'opération.

- [ ] **HIST-008** — Vérifier redo invalidé lorsqu'une nouvelle branche d'édition est créée.

- [ ] **HIST-009** — Vérifier titres undo/redo reflètent exactement la prochaine commande.

- [ ] **HIST-010** — Vérifier état disabled des commandes selon la pile réelle.

- [ ] **HIST-011** — Vérifier historique d'une opération multi-objets composite.

- [ ] **HIST-012** — Vérifier historique d'une régénération amont/aval et restauration de retouches.

- [ ] **HIST-013** — Vérifier historique d'une optimisation et d'une correction automatique.

- [ ] **HIST-014** — Vérifier historique d'une intervention compagnon IA approuvée.

- [ ] **HIST-015** — Vérifier sélection et identifiants cohérents après undo de suppression.

- [ ] **HIST-016** — Vérifier aucun pointeur mort après remplacement document.

- [ ] **HIST-017** — Vérifier historique ne se mélange pas entre deux projets.

- [ ] **HIST-018** — Vérifier dirty/clean selon point de sauvegarde et branches divergentes.

- [ ] **HIST-019** — Vérifier gestion des limites de taille mémoire de l'historique.

- [ ] **HIST-020** — Vérifier que navigation dans historique n'écrit pas une modification spontanée.

- [ ] **HIST-021** — Vérifier annulation impossible ne laisse pas d'état demi-modifié.


### ASY — Tâches asynchrones, latence, annulation et signaux (22 vérifications)

- [ ] **ASY-001** — Vérifier interface interactive pendant segmentation et génération.

- [ ] **ASY-002** — Vérifier progression quantifiable ou message d'attente fidèle.

- [ ] **ASY-003** — Vérifier bouton Annuler pendant chaque tâche longue.

- [ ] **ASY-004** — Vérifier annulation précoce, au milieu, proche de la fin.

- [ ] **ASY-005** — Vérifier résultat de tâche annulée jamais appliqué après annulation.

- [ ] **ASY-006** — Vérifier résultat de révision périmée jamais appliqué.

- [ ] **ASY-007** — Vérifier ouvrir un nouveau projet pendant un calcul et rejeter l'ancien résultat.

- [ ] **ASY-008** — Vérifier fermer application pendant une tâche sans crash.

- [ ] **ASY-009** — Vérifier enchaîner deux calculs du même type sans confusion de retour.

- [ ] **ASY-010** — Vérifier tâches concurrentes de familles différentes avec politique explicite.

- [ ] **ASY-011** — Vérifier destruction de contrôleur QML n'invalide pas le TaskManager.

- [ ] **ASY-012** — Vérifier que tous les signaux vers QML sont livrés sur le bon thread.

- [ ] **ASY-013** — Vérifier aucun accès concurrent non protégé au Project ou à la sélection.

- [ ] **ASY-014** — Vérifier que des signaux ne sont pas émis pendant destruction d'objets abonnés.

- [ ] **ASY-015** — Vérifier absence de deadlock sur annulation répétée.

- [ ] **ASY-016** — Vérifier absence de busy loop si progression à zéro.

- [ ] **ASY-017** — Vérifier debounce efficace sur sliders et frappes rapides.

- [ ] **ASY-018** — Vérifier absence de rendus lourds empilés suite à nombreuses révisions.

- [ ] **ASY-019** — Vérifier messages d'erreurs sur exception, mémoire insuffisante et tâche échouée.

- [ ] **ASY-020** — Vérifier stabilité après 100 cycles calcul/annulation.

- [ ] **ASY-021** — Vérifier indicateurs de chargement disparaissent après réussite/échec/annulation.

- [ ] **ASY-022** — Vérifier l'état UI reprend exactement après tâche annulée.


### GPU — Rendu GPU / Scene Graph / threading / fidélité (30 vérifications)

- [ ] **GPU-001** — Identifier backend Qt Quick effectivement utilisé et nom du pilote.

- [ ] **GPU-002** — Vérifier Direct3D/RHI sur Windows supporté.

- [ ] **GPU-003** — Vérifier OpenGL si pris en charge par build et pilote.

- [ ] **GPU-004** — Vérifier logiciel/WARP comme cas distinct du vrai GPU.

- [ ] **GPU-005** — Vérifier le rendu sur GPU intégré et GPU dédié quand possible.

- [ ] **GPU-006** — Vérifier perte de device/contexte et reconstruction des ressources.

- [ ] **GPU-007** — Vérifier changement d'écran et backend/DPI sans corruption visuelle.

- [ ] **GPU-008** — Vérifier Scene Graph ne lit jamais une ressource libérée.

- [ ] **GPU-009** — Vérifier ownership et destruction des nœuds QSG sur le bon thread.

- [ ] **GPU-010** — Vérifier changement de révision déclenche reconstruction nécessaire et unique.

- [ ] **GPU-011** — Vérifier simple pan/zoom ne reconstruit pas les buffers de points.

- [ ] **GPU-012** — Vérifier sélection seule reconstruit au plus les overlays nécessaires.

- [ ] **GPU-013** — Vérifier aucun objet QML créé par point ou segment de broderie.

- [ ] **GPU-014** — Vérifier aucune texture gigantesque recalculée inutilement.

- [ ] **GPU-015** — Vérifier précision des coordonnées pour grandes dimensions et fort zoom.

- [ ] **GPU-016** — Vérifier séparation des segments aux sauts et coupes.

- [ ] **GPU-017** — Vérifier lignes fines, MSAA et artefacts de pixel à 100/150/200 %.

- [ ] **GPU-018** — Vérifier transparence et ordre alpha entre source/régions/vecteurs/points.

- [ ] **GPU-019** — Vérifier rendu de 1 000, 10 000, 50 000, 100 000 et 500 000 points.

- [ ] **GPU-020** — Vérifier 60 FPS cible en navigation sur les scènes représentatives.

- [ ] **GPU-021** — Vérifier p50/p95/p99 frame time plutôt que seul FPS moyen.

- [ ] **GPU-022** — Vérifier latence des gestes utilisateur séparée du frame time.

- [ ] **GPU-023** — Vérifier temps première frame et temps de chargement.

- [ ] **GPU-024** — Vérifier CPU %, RAM résidente/pic et mémoire GPU si observable.

- [ ] **GPU-025** — Vérifier stabilité de mémoire après pan/zoom prolongé.

- [ ] **GPU-026** — Vérifier comportement sous limitation mémoire et faible VRAM.

- [ ] **GPU-027** — Vérifier comparaisons GPU/CPU avec mêmes documents, camera, sélection et palette.

- [ ] **GPU-028** — Vérifier métriques ne sont pas biaisées par le mode `--screenshot`.

- [ ] **GPU-029** — Vérifier capturer logs backend avec `QSG_INFO=1` ou mécanisme équivalent.

- [ ] **GPU-030** — Vérifier aucune régression de performance importante par rapport au baseline connu.


### CPU — Repli logiciel, compatibilité et dégradation progressive (20 vérifications)

- [ ] **CPU-001** — Lancer explicitement Studio avec `--software`.

- [ ] **CPU-002** — Vérifier rendu du canevas en logiciel à l'ouverture.

- [ ] **CPU-003** — Vérifier pan, zoom, sélection et overlays en logiciel.

- [ ] **CPU-004** — Vérifier exactitude visuelle comparée à GPU malgré différence de qualité documentée.

- [ ] **CPU-005** — Vérifier aucune dépendance cachée à un shader GPU pour les commandes métier.

- [ ] **CPU-006** — Vérifier fonctionnalités du shell conservées si backend GPU indisponible.

- [ ] **CPU-007** — Vérifier message clair si backend matériel refuse l'initialisation.

- [ ] **CPU-008** — Vérifier possibilité de relancer en logiciel après échec matériel.

- [ ] **CPU-009** — Vérifier taille des textures et viewport avec DPR en logiciel.

- [ ] **CPU-010** — Vérifier redraw complet sans trous après pan/resize.

- [ ] **CPU-011** — Vérifier absence de crash en quittant le mode logiciel.

- [ ] **CPU-012** — Vérifier performance acceptable à faible densité de points.

- [ ] **CPU-013** — Vérifier fréquence de repaint du backend logiciel sous charge.

- [ ] **CPU-014** — Vérifier rendu sans sous-système réseau actif.

- [ ] **CPU-015** — Vérifier compatibilité police/locale en rendu logiciel.

- [ ] **CPU-016** — Vérifier tests offscreen ne prétendent pas valider le comportement GPU.

- [ ] **CPU-017** — Vérifier suites de tests UI sur plateforme native Windows distinctes des tests headless.

- [ ] **CPU-018** — Vérifier erreurs Qt Quick/scenegraph clairement tracées dans les logs.

- [ ] **CPU-019** — Vérifier mode de repli ne change jamais le projet ou ses points.

- [ ] **CPU-020** — Vérifier messages et statuts GPU/CPU honnêtes dans barre d'état.


### AI — Compagnon IA, MCP, sécurité et transactions (22 vérifications)

- [ ] **AI-001** — Vérifier panneau Compagnon accessible et lisible.

- [ ] **AI-002** — Vérifier absence de backend configuré sans crash.

- [ ] **AI-003** — Vérifier état déconnecté/connexion/reconnexion.

- [ ] **AI-004** — Vérifier fournisseur simulé et vrai fournisseur seulement si autorisé.

- [ ] **AI-005** — Vérifier ouverture/fermeture de session liée au bon document.

- [ ] **AI-006** — Vérifier prévisualisation proposée sans mutation du document.

- [ ] **AI-007** — Vérifier différence avant/après cohérente avec la commande proposée.

- [ ] **AI-008** — Vérifier approbation explicite avant modification.

- [ ] **AI-009** — Vérifier refus ne modifie ni document ni historique.

- [ ] **AI-010** — Vérifier action approuvée en une seule transaction undo.

- [ ] **AI-011** — Vérifier proposition périmée après édition locale refusée.

- [ ] **AI-012** — Vérifier résultat lié à un autre projet refusé.

- [ ] **AI-013** — Vérifier application de commandes via `DocumentSession`, non manipulation QML directe.

- [ ] **AI-014** — Vérifier accès des commandes MCP aux mêmes préconditions que l'UI.

- [ ] **AI-015** — Vérifier timeout, erreur réseau et indisponibilité fournisseur.

- [ ] **AI-016** — Vérifier gestion d'une réponse malformée/partielle.

- [ ] **AI-017** — Vérifier journal d'actions compréhensible sans fuite de secrets.

- [ ] **AI-018** — Vérifier token/API key masqué dans l'UI et non enregistré dans l'OSP.

- [ ] **AI-019** — Vérifier permissions et limites d'action agent clairement définies.

- [ ] **AI-020** — Vérifier annulation ou fermeture pendant un aperçu IA.

- [ ] **AI-021** — Vérifier stabilité après plusieurs propositions successives.

- [ ] **AI-022** — Vérifier aucune modification silencieuse par agent en dehors des demandes autorisées.


### PREF — Préférences, personnalisation, localisation et aide (23 vérifications)

- [ ] **PREF-001** — Vérifier ouverture/fermeture des préférences depuis tous les espaces.

- [ ] **PREF-002** — Vérifier sauvegarde/restauration des préférences au redémarrage.

- [ ] **PREF-003** — Vérifier changement thème sombre/clair sans incohérence.

- [ ] **PREF-004** — Vérifier changement de densité sans chevauchement.

- [ ] **PREF-005** — Vérifier réglage réduire les animations respecté partout.

- [ ] **PREF-006** — Vérifier activation/désactivation des panneaux ou barres si supportée.

- [ ] **PREF-007** — Vérifier réinitialiser une disposition à l'état par défaut.

- [ ] **PREF-008** — Vérifier changement de paramètres par défaut des points nouveaux.

- [ ] **PREF-009** — Vérifier unités et préférences machine si exposées.

- [ ] **PREF-010** — Vérifier locale française partout dans labels, nombres et dates.

- [ ] **PREF-011** — Vérifier locale anglaise si supportée partout, y compris erreurs.

- [ ] **PREF-012** — Vérifier textes localisés sans chaîne concaténée grammaticalement erronée.

- [ ] **PREF-013** — Vérifier absence de textes temporaires, placeholders, « TODO » dans le produit.

- [ ] **PREF-014** — Vérifier longueur de traduction dans boutons et menus.

- [ ] **PREF-015** — Vérifier ressources d'aide et guide accessibles hors ligne.

- [ ] **PREF-016** — Vérifier feuille des raccourcis identique au catalogue de commandes.

- [ ] **PREF-017** — Vérifier aide contextuelle sur outils et paramètres critiques.

- [ ] **PREF-018** — Vérifier liens externes s'ouvrant sans interrompre le document.

- [ ] **PREF-019** — Vérifier préférences de l'IA distinctes des données du document.

- [ ] **PREF-020** — Vérifier absence de modification de préférences par simple survol/scroll.

- [ ] **PREF-021** — Vérifier choix de langue ne remplace pas les valeurs du document.

- [ ] **PREF-022** — Vérifier notifications d'un paramètre nécessitant redémarrage.

- [ ] **PREF-023** — Vérifier récupération après une préférence inconnue d'ancienne version.


### A11Y — Accessibilité et ergonomie multi-modalité (20 vérifications)

- [ ] **A11Y-001** — Vérifier libellé accessible pour chaque action uniquement iconographique.

- [ ] **A11Y-002** — Vérifier annonces de rôle accessible des boutons/checkboxes/comboboxes.

- [ ] **A11Y-003** — Vérifier état actif, sélectionné, coché, désactivé communiqué aux technologies d'assistance.

- [ ] **A11Y-004** — Vérifier noms accessibles des pastilles d'étape.

- [ ] **A11Y-005** — Vérifier navigation clavier sans piège de focus.

- [ ] **A11Y-006** — Vérifier taille et visibilité des cibles interactives.

- [ ] **A11Y-007** — Vérifier contraste texte normal >=4,5:1 pour combos réellement affichées.

- [ ] **A11Y-008** — Vérifier contraste éléments non textuels significatifs >=3:1.

- [ ] **A11Y-009** — Vérifier que les informations critiques ne reposent pas uniquement sur la couleur.

- [ ] **A11Y-010** — Vérifier réglage réduire les animations.

- [ ] **A11Y-011** — Vérifier absence de clignotement incontrôlable.

- [ ] **A11Y-012** — Vérifier texte utilisable à zoom système accru.

- [ ] **A11Y-013** — Vérifier sous-titres/équivalents textuels pour informations non textuelles si pertinents.

- [ ] **A11Y-014** — Vérifier ordre des éléments lu par lecteur d'écran cohérent avec structure.

- [ ] **A11Y-015** — Vérifier annonces d'erreurs et de tâches terminées si assistance supportée.

- [ ] **A11Y-016** — Vérifier alternative clavier pour action exclusivement réalisée par drag.

- [ ] **A11Y-017** — Vérifier possibilité de changer valeurs fines sans haute précision souris.

- [ ] **A11Y-018** — Vérifier tests avec Narrator Windows ou autre lecteur si environnement disponible.

- [ ] **A11Y-019** — Vérifier action importante compréhensible sans connaitre le vocabulaire technique.

- [ ] **A11Y-020** — Vérifier lisibilité en daltonisme sur palettes et états UI.


### ROB — Entrées pathologiques, sécurité, limites et reprise d'erreur (30 vérifications)

- [ ] **ROB-001** — Tester OSP vide, minimal, très gros, ancien et incompatible.

- [ ] **ROB-002** — Tester PNG/JPEG tronqué et fichier avec extension trompeuse.

- [ ] **ROB-003** — Tester SVG contenant très grand nombre de nœuds.

- [ ] **ROB-004** — Tester chemin vectoriel auto-intersecté et coordonnées extrêmes.

- [ ] **ROB-005** — Tester objet géométriquement vide/point unique/contour infiniment fin.

- [ ] **ROB-006** — Tester NaN, infini, valeurs négatives et dépassements numériques via UI/API.

- [ ] **ROB-007** — Tester couleurs invalides et références de fils absentes.

- [ ] **ROB-008** — Tester segmentation avec zéro région et millions de petits îlots.

- [ ] **ROB-009** — Tester fichier image aux dimensions élevées et limites mémoire.

- [ ] **ROB-010** — Tester 0/1/10/256 couleurs selon capacité effective.

- [ ] **ROB-011** — Tester OSP contenant des identifiants 64 bits supérieurs à 2^53.

- [ ] **ROB-012** — Tester noms d'objet Unicode, RTL, longs, emojis et caractères de contrôle.

- [ ] **ROB-013** — Tester fichiers dans répertoires restreints et chemins réseau.

- [ ] **ROB-014** — Tester suppression du fichier source entre choix et ouverture.

- [ ] **ROB-015** — Tester espace disque presque plein pendant enregistrement/export.

- [ ] **ROB-016** — Tester interruption brutale du processus pendant sauvegarde avec récupération si prévue.

- [ ] **ROB-017** — Tester déclenchements répétés rapides d'un même bouton.

- [ ] **ROB-018** — Tester clics sur actions différentes pendant progression.

- [ ] **ROB-019** — Tester changement de thème et espace pendant import/export.

- [ ] **ROB-020** — Tester fermeture forcée d'un popup et fuite d'abonnement.

- [ ] **ROB-021** — Tester cycles création/destruction de la fenêtre.

- [ ] **ROB-022** — Tester ouverture du même fichier depuis deux instances.

- [ ] **ROB-023** — Tester zoom très fort avec document très grand.

- [ ] **ROB-024** — Tester machine/format impossible et message d'échec précis.

- [ ] **ROB-025** — Tester injection de contenu malformé via MCP sans mutation du projet.

- [ ] **ROB-026** — Tester noms de fichier avec séparateurs interdits et chaînes vides.

- [ ] **ROB-027** — Tester format de fichier suspect sans exécution de scripts/contenus actifs.

- [ ] **ROB-028** — Tester dépassement de capacité de liste, index négatif et sélection d'objet supprimé.

- [ ] **ROB-029** — Tester données préservées après erreur récupérable.

- [ ] **ROB-030** — Tester erreurs non loguées comme réussite et aucune fausse notification « terminé ».


### PERF — Performance UX et sensation de fluidité (24 vérifications)

- [ ] **PERF-001** — Mesurer latence clic bouton → réaction visuelle.

- [ ] **PERF-002** — Mesurer latence sélection arbre → canevas.

- [ ] **PERF-003** — Mesurer latence sélection canevas → inspecteur.

- [ ] **PERF-004** — Mesurer latence ouverture et fermeture des panneaux.

- [ ] **PERF-005** — Mesurer latence changement d'espace avec document lourd.

- [ ] **PERF-006** — Mesurer latence pour ouvrir palette de commandes Ctrl+K.

- [ ] **PERF-007** — Mesurer latence de frappe dans recherche de l'arbre.

- [ ] **PERF-008** — Mesurer latence de mise à jour des paramètres d'inspecteur.

- [ ] **PERF-009** — Mesurer temps de première interaction possible après lancement.

- [ ] **PERF-010** — Mesurer temps de chargement et disponibilité de la sélection.

- [ ] **PERF-011** — Mesurer temps de segmentation sur scènes représentatives.

- [ ] **PERF-012** — Mesurer temps de vectorisation et de génération de points.

- [ ] **PERF-013** — Mesurer temps d'annulation et de fermeture de tâche lourde.

- [ ] **PERF-014** — Mesurer temps et mémoire d'export de fichiers lourds.

- [ ] **PERF-015** — Vérifier absence de saut perceptible de layout en interaction.

- [ ] **PERF-016** — Vérifier animations courtes et non bloquantes.

- [ ] **PERF-017** — Vérifier absence de « jank » durant défilement de listes.

- [ ] **PERF-018** — Vérifier absence d'animation sur changement sans effet métier.

- [ ] **PERF-019** — Vérifier progression/cursor busy uniquement quand l'attente est réelle.

- [ ] **PERF-020** — Vérifier feedback immédiat au clic même pour action longue.

- [ ] **PERF-021** — Vérifier touches de raccourci reconnues même pendant recalcul non bloquant.

- [ ] **PERF-022** — Vérifier charge CPU au repos proche du niveau de référence.

- [ ] **PERF-023** — Vérifier RAM stable après édition prolongée et 100 bascules de panneaux.

- [ ] **PERF-024** — Vérifier le scénario petit laptop ne soit pas pénalisé inutilement par des effets décoratifs.


### CI — Tests automatiques, scénarios UI réels et garde-fous CI (30 vérifications)

- [ ] **CI-001** — Exécuter l'ensemble des tests CTest pertinents sans filtrer les échecs gênants.

- [ ] **CI-002** — Exécuter le linter de design QML sur tous les sous-dossiers.

- [ ] **CI-003** — Exécuter formatage C++ et outils d'analyse statique disponibles.

- [ ] **CI-004** — Vérifier qu'aucun test n'est désactivé ou marqué skip pour faire passer une PR.

- [ ] **CI-005** — Vérifier qu'une erreur vcpkg/setup CI est séparée d'un échec applicatif.

- [ ] **CI-006** — Vérifier que le Windows MSVC CI construit le binaire Qt Quick.

- [ ] **CI-007** — Vérifier que Linux-core continue de compiler sans Qt.

- [ ] **CI-008** — Vérifier suites en Debug, Release et matériel natif.

- [ ] **CI-009** — Vérifier QTest crée un vrai `Main.qml` pour chaque parcours UI important.

- [ ] **CI-010** — Vérifier clic via `QTest::mouseClick` au lieu d'appels directs à `.click()` uniquement.

- [ ] **CI-011** — Vérifier tests de popup par clic avec repositionnement et défilement.

- [ ] **CI-012** — Vérifier tests de keyboard focus et raccourcis dans différents contextes.

- [ ] **CI-013** — Vérifier tests sur fenêtre réduite et HiDPI réel ou émulé distinctement.

- [ ] **CI-014** — Vérifier screenshots natifs vérifiables pour quatre espaces.

- [ ] **CI-015** — Vérifier GPU réel et repli logiciel dans suites distinctes.

- [ ] **CI-016** — Vérifier comparaison de captures avec masquage contrôlé des variations déterministes.

- [ ] **CI-017** — Vérifier absence de test à assertion triviale ou de `QSKIP` non justifié.

- [ ] **CI-018** — Vérifier cas d'erreur/régression à partir de chaque bug trouvé.

- [ ] **CI-019** — Vérifier tests qui testent l'effet métier, pas seulement la propriété visible.

- [ ] **CI-020** — Vérifier tests d'undo/redo pour toute commande mutante.

- [ ] **CI-021** — Vérifier roundtrip OSP + export DST après scénarios critiques.

- [ ] **CI-022** — Vérifier tests de résilience lors des interruptions et changements de document.

- [ ] **CI-023** — Vérifier tests de timing stables avec attente événementielle plutôt que sleep arbitraire.

- [ ] **CI-024** — Vérifier exécution répétée des cas instables, journaliser leur variance.

- [ ] **CI-025** — Vérifier garder les artefacts sur échec : logs, capture, crash dump, SHA.

- [ ] **CI-026** — Vérifier aucune fixture ni état utilisateur modifiés par les tests.

- [ ] **CI-027** — Vérifier que les changements de CMake conservent l'union des cibles après merges.

- [ ] **CI-028** — Vérifier CI après rebase, puis de nouveau après dernier commit.

- [ ] **CI-029** — Vérifier statut des checks requis et conflits GitHub avant proposition de merge.

- [ ] **CI-030** — Vérifier que la documentation d'installation reflète les dépendances effectives.


### PACK — Livraison Windows, packaging et distribution (21 vérifications)

- [ ] **PACK-001** — Vérifier nom du binaire attendu `openstitch.exe` en sortie Studio selon CMake actuel.

- [ ] **PACK-002** — Vérifier V1 reste identifiable si le packaging l'inclut.

- [ ] **PACK-003** — Vérifier packaging depuis artefacts Release propres.

- [ ] **PACK-004** — Vérifier `windeployqt` inclut modules Qt Quick/QML nécessaires.

- [ ] **PACK-005** — Vérifier tous les plugins d'image et de plateforme requis.

- [ ] **PACK-006** — Vérifier assets, icônes, polices autorisées et traductions inclus.

- [ ] **PACK-007** — Vérifier lancement sur Windows sans Qt préinstallé.

- [ ] **PACK-008** — Vérifier lancement sans Visual Studio installé.

- [ ] **PACK-009** — Vérifier lancement par double-clic depuis l'explorateur.

- [ ] **PACK-010** — Vérifier ouverture fichier `.osp` via association si prévue.

- [ ] **PACK-011** — Vérifier installation avec chemin non ASCII et espace.

- [ ] **PACK-012** — Vérifier installation avec droits utilisateur standard.

- [ ] **PACK-013** — Vérifier désinstallation sans suppression des projets utilisateur.

- [ ] **PACK-014** — Vérifier mise à jour depuis version précédente et migration de préférences.

- [ ] **PACK-015** — Vérifier antivirus/SmartScreen/code signing si pipeline prévu.

- [ ] **PACK-016** — Vérifier taille de paquet, fichiers manquants et DLL en double.

- [ ] **PACK-017** — Vérifier exe MCP/ressources associées si packagés avec le desktop.

- [ ] **PACK-018** — Vérifier logs de crash et emplacement connu de l'utilisateur.

- [ ] **PACK-019** — Vérifier numéro de version et lien release correspondant au commit.

- [ ] **PACK-020** — Vérifier licence/notice des dépendances distribuées.

- [ ] **PACK-021** — Vérifier installation propre suivie du parcours image → DST.


### POL — Qualité produit, cohérence du workflow et « juice » (25 vérifications)

- [ ] **POL-001** — Vérifier qu'une action essentielle est découvrable en moins de deux niveaux d'UI si possible.

- [ ] **POL-002** — Vérifier états vides avec prochaine action pertinente.

- [ ] **POL-003** — Vérifier hiérarchie claire action principale/secondaire/dangereuse.

- [ ] **POL-004** — Vérifier répartition logique des outils par espace.

- [ ] **POL-005** — Vérifier absence de fonctionnalité présente uniquement via un raccourci secret.

- [ ] **POL-006** — Vérifier absence de bouton qui ne fait qu'ouvrir un autre panneau inutilement.

- [ ] **POL-007** — Vérifier libellés en termes utilisateur et vocabulaire stable.

- [ ] **POL-008** — Vérifier explication des statuts obsolète/bloqué, avec résolution concrète.

- [ ] **POL-009** — Vérifier que le feedback d'une action réussie ne parasite pas la création.

- [ ] **POL-010** — Vérifier que l'interaction réussie se voit et se ressent rapidement.

- [ ] **POL-011** — Vérifier que hover, press et sélection racontent le même état.

- [ ] **POL-012** — Vérifier aucune animation n'altère la précision de la numérisation.

- [ ] **POL-013** — Vérifier que les confirmations n'interrompent que les actions réellement risquées.

- [ ] **POL-014** — Vérifier réglages fréquents sans navigation excessive.

- [ ] **POL-015** — Vérifier réglages avancés disponibles sans encombrer l'écran principal.

- [ ] **POL-016** — Vérifier l'inspecteur reste contextuel après sélection et changement d'outil.

- [ ] **POL-017** — Vérifier cohérence entre actions identiques accessibles par plusieurs points d'entrée.

- [ ] **POL-018** — Vérifier que les ajustements de layout n'altèrent pas le workflow.

- [ ] **POL-019** — Vérifier que les toolbars trop longues ont menu overflow ou scroll explicite.

- [ ] **POL-020** — Vérifier alignement de tous les panneaux et barres du Studio.

- [ ] **POL-021** — Vérifier absence de libellés coupés « Simulation », « Analyse », etc.

- [ ] **POL-022** — Vérifier pas de « clic mort » ni de contrôle qui disparaît sous curseur.

- [ ] **POL-023** — Vérifier accès à une commande critique sans devoir agrandir la fenêtre.

- [ ] **POL-024** — Vérifier workflow avec panneau Compagnon ouvert/fermé.

- [ ] **POL-025** — Vérifier qualité sur projet banal et projet très complexe.


### REG — Matrice de régression croisée entre sous-systèmes (25 vérifications)

- [ ] **REG-001** — Modifier image → vérifier invalidation segmentation, vecteurs, points.

- [ ] **REG-002** — Modifier segmentation → vérifier mise à jour contrôlée des vecteurs.

- [ ] **REG-003** — Modifier vecteur → vérifier points/rendu/diagnostic sans reconstruire l'image.

- [ ] **REG-004** — Modifier type de point → vérifier preview/simulation/export.

- [ ] **REG-005** — Modifier fil → vérifier couleurs, ordre, analyse et sortie DST.

- [ ] **REG-006** — Modifier cadre → vérifier diagnostic et contrôle machine.

- [ ] **REG-007** — Modifier retouche de point → vérifier export et conservation OSP.

- [ ] **REG-008** — Modifier tirage → vérifier preview de risque et sortie si applicable.

- [ ] **REG-009** — Modifier satin → vérifier rails, points et finitions.

- [ ] **REG-010** — Modifier fondu → vérifier mélange, sauts, simulation.

- [ ] **REG-011** — Modifier visibilité calque → vérifier canevas, sélection et export.

- [ ] **REG-012** — Modifier ordre d'objet → vérifier affichage et ordre de couture.

- [ ] **REG-013** — Annuler correction région → vérifier que l'aval retrouve sa cohérence.

- [ ] **REG-014** — Annuler réduction de fils → vérifier palette et rendu restaurés.

- [ ] **REG-015** — Annuler optimisation → vérifier ordre initial rétabli.

- [ ] **REG-016** — Changer d'espace pendant sélection → vérifier sélection et caméra.

- [ ] **REG-017** — Changer d'espace pendant calcul → vérifier resultats, état et absence de crash.

- [ ] **REG-018** — Changer de thème pendant popup → vérifier cliquabilité et lisibilité.

- [ ] **REG-019** — Réduire fenêtre pendant simulation → vérifier barre et contenu.

- [ ] **REG-020** — Réouvrir OSP après chaque famille d'édition → vérifier équivalence.

- [ ] **REG-021** — Exporter DST après undo/redo → vérifier identité des points attendus.

- [ ] **REG-022** — Charger depuis fichier récent après dirty → vérifier confirmation.

- [ ] **REG-023** — Passer GPU ↔ CPU sur document identique → vérifier mêmes données document.

- [ ] **REG-024** — Activer le compagnon IA puis remplacer le projet → vérifier session invalidée.

- [ ] **REG-025** — Mettre à jour une PR concurrente → revérifier tests et captures des composants partagés.


### JRN — Parcours de recette complets avec fichier réel (30 vérifications)

- [ ] **JRN-001** — Créer projet vide → dessiner un rectangle → choisir tatami → sauvegarder → exporter DST.

- [ ] **JRN-002** — Importer image colorée → prétraiter → segmenter → vectoriser → points → export.

- [ ] **JRN-003** — Importer image à fond transparent → détourage → broderie sans fond.

- [ ] **JRN-004** — Importer image 10 couleurs max → nettoyer les îlots → DST.

- [ ] **JRN-005** — Importer image avec éléments très fins → conserver détails pertinents.

- [ ] **JRN-006** — Créer puis corriger une région fusionnée par erreur.

- [ ] **JRN-007** — Découper un pont entre deux zones et conserver la séparation.

- [ ] **JRN-008** — Regénérer l'aval après correction région sans effacer les objets manuels.

- [ ] **JRN-009** — Regénérer après retouches → vérifier confirmation puis restauration via undo.

- [ ] **JRN-010** — Ouvrir OSP existant → modifier couleur → enregistrer sous → réouvrir.

- [ ] **JRN-011** — Ouvrir OSP → masquer un objet → exporter → vérifier son absence du DST.

- [ ] **JRN-012** — Dessiner vecteur → appliquer rotation/échelle → générer points.

- [ ] **JRN-013** — Importer SVG → inspecter formes → choisir plusieurs types de points.

- [ ] **JRN-014** — Créer satin avec deux rails → éditer les nœuds → exporter.

- [ ] **JRN-015** — Créer remplissage spirale → contrôler continuité et nombre de coupes.

- [ ] **JRN-016** — Créer remplissage radial sans retrait → contrôler limite des points.

- [ ] **JRN-017** — Créer fondu entre fils → comparer simulation puis export.

- [ ] **JRN-018** — Créer appliqué → vérifier séquence et contour de découpe si supporté.

- [ ] **JRN-019** — Sélectionner dix objets → changer densité/fil → annuler une fois.

- [ ] **JRN-020** — Lancer diagnostic → cliquer anomalie → corriger → vérifier disparition.

- [ ] **JRN-021** — Réduire palette de fils → vérifier nombre de bobines et undo.

- [ ] **JRN-022** — Optimiser les chemins → vérifier bilan sauts/coupes avant-après.

- [ ] **JRN-023** — Lancer simulation → naviguer par couleur et objet → exporter.

- [ ] **JRN-024** — Tester une broderie qui dépasse le cadre → refuser ou corriger avant export.

- [ ] **JRN-025** — Tester ouverture fichier corrompu → retrouver document précédent intact.

- [ ] **JRN-026** — Tester calcul long → annuler → changer de projet → absence de résultat tardif.

- [ ] **JRN-027** — Tester quatre espaces sur fenêtre 1000×640 → commandes toujours accessibles.

- [ ] **JRN-028** — Tester même projet GPU réel puis `--software` avec captures comparables.

- [ ] **JRN-029** — Tester session compagnon IA proposée → refuser puis approuver une autre commande.

- [ ] **JRN-030** — Installer paquet Release propre → importer image → sauvegarder OSP → exporter DST.


### DOC — Documentation, parité, exemples et clôture (22 vérifications)

- [ ] **DOC-001** — Actualiser le journal d'avancement avec résultats constatés, non intentions.

- [ ] **DOC-002** — Actualiser la matrice parité par fonction, preuve et surface Studio.

- [ ] **DOC-003** — Générer la matrice avec `scripts/parity/build_studio_matrix.py` et contrôler les différences.

- [ ] **DOC-004** — Vérifier `VALIDATED` uniquement avec tests d'usage concluants.

- [ ] **DOC-005** — Vérifier chaque fonction `ABSENT` est explicitement classée et non masquée.

- [ ] **DOC-006** — Vérifier chaque fonction `BLOCKED` possède cause et décision requise.

- [ ] **DOC-007** — Vérifier que les capacités nouvelles Studio sont ajoutées à l'inventaire.

- [ ] **DOC-008** — Vérifier docs obsolètes distinguées de l'état présent du code.

- [ ] **DOC-009** — Vérifier description du lancement et du nom réel de l'exécutable.

- [ ] **DOC-010** — Vérifier documentation des quatre espaces et du bandeau amont/aval.

- [ ] **DOC-011** — Vérifier fiche des raccourcis cohérente avec le catalogue de commandes.

- [ ] **DOC-012** — Vérifier documentation des avertissements production et limites physiques.

- [ ] **DOC-013** — Vérifier description explicite des limitations GPU/CPU.

- [ ] **DOC-014** — Vérifier procédure de reproduction de chaque benchmark publiée.

- [ ] **DOC-015** — Vérifier provenance et métadonnées des captures réelles.

- [ ] **DOC-016** — Vérifier rapport de non-régression avant/après chaque correction.

- [ ] **DOC-017** — Vérifier qu'une anomalie résolue possède test protecteur.

- [ ] **DOC-018** — Vérifier qu'aucune nouvelle PR QA n'est créée sans inspection des PR concurrentes.

- [ ] **DOC-019** — Vérifier status Git propre et pas d'artifacts temporaires commités.

- [ ] **DOC-020** — Vérifier absence de régression connue non annoncée dans rapport final.

- [ ] **DOC-021** — Vérifier qu'une livraison n'est pas déclarée « zéro défaut » sur seule base des tests automatisés.

- [ ] **DOC-022** — Vérifier liste des vérifications non exécutées avec raison et procédure.

---

## Méthode d'exécution attendue de l'agent

1. **Cartographier** les fichiers, composants, commandes, variantes d'état, tests et capacités. Repartir de la tête Git réelle et des PR ouvertes ; ne pas recycler aveuglément les chiffres anciens des docs.
2. **Établir une baseline** Debug/Release : compiler, exécuter CTest, journaliser les échecs de code et les pannes d'infrastructure séparément ; fabriquer des fixtures de recette non destructives.
3. **Recetter le shell réellement exécuté** : utiliser des événements souris/clavier sur le `Main.qml` véritable ; ouvrir menus, combobox, inspecteurs ; tester les 4 espaces et tailles prévues ; enregistrer captures natives et logs.
4. **Recetter tous les parcours métiers** : import image/SVG/OSP → préparation → segmentation → vecteurs → génération/édition de points → analyse/simulation → sauvegarde/export → réouverture/équivalence.
5. **Établir une anomalie par symptôme reproductible**, avec famille/ID/état, sévérité, causes probables et lien vers preuve. Une anomalie qui touche plusieurs écrans conserve toutes ses références.
6. **Corriger au niveau architectural le plus bas approprié** : C++ métier dans service C++, orchestration dans contrôleur, affichage/gestes dans QML ; conserver `DocumentSession`, `SelectionModel`, `TaskManager`, le modèle révisionnel, le design system et les tests existants. Ne pas dupliquer les calculs dans QML.
7. **Écrire le test avant ou avec le fix** : reproduire l'échec avec un clic réel ou un scénario document, puis prouver le succès avec `QTest`, Catch2, CTest, comparaison OSP/DST, capture ou mesure. Les tests UI ne doivent pas seulement invoquer les méthodes des contrôleurs.
8. **Prévenir les conflits** : ne pas écraser les travaux des PR existantes, réserver les fichiers partagés à un intégrateur, rebaser, relancer les tests sur la tête définitive. Ne créer plusieurs PR que si les changements sont réellement indépendants ; préférer une PR d'intégration QA par lot cohérent pour ne pas marcher sur les agents UI existants.
9. **Vérifier la preuve humaine/machine** : captures des 4 espaces et états clés, feuille de comparaison GPU/CPU, logs backend, matrice PASS/FAIL/ABSENT et liste des éléments non testables sans vrai matériel.
10. **Clôturer honnêtement** : fournir couverture atteinte, liste des défauts corrigés, incidents restants, régressions exclues, statut CI réel, limites des tests et décision de readiness. Ne pas dire « aucune coquille » si un contrôle n'a pas été essayé.

### Ordre de correction conseillé (sans supprimer les tests de faible priorité)

S0 conservation et sécurité des fichiers → S1 crash/CI/UI non cliquable/canvas non mis à jour → parcours 4 espaces et chaîne de production → écrans réduits/popups/focus → exactitude GPU/CPU et formats → performances → esthétique/animations/copy → tests rares et résiduels.

### Commandes de départ à adapter à la branche réelle

```powershell
# Ne rien modifier tant que la branche Studio active et les PR ne sont pas recensées.
gh pr list --repo ElZacho200a/OpenStitch --state open
cmake --preset msvc -DOPENSTITCH_BUILD_STUDIO=ON
cmake --build --preset msvc-debug --target openstitch_v2_studio
cmake --build --preset msvc-release --target openstitch_v2_studio
ctest --preset msvc-debug -R 'studio|param|desktop_v2_core' --output-on-failure
ctest --preset msvc-release -R 'studio|param|desktop_v2_core' --output-on-failure
# Les tests natifs avec vraie fenêtre sont distincts des CTest headless.
$env:QSG_INFO='1'
# Sur le build actuel la cible openstitch_v2_studio produit openstitch.exe ;
# localiser le véritable chemin de sortie au lieu de reprendre un ancien manuel.
```

### Exigences de rapports

- `qa/inventory.md` : fichiers/commandes/contrôles recensés, couverture et écarts de parité.
- `qa/checklist.csv` : `id,category,status,severity,sha,environment,evidence,issue,fix,test,notes`.
- `qa/findings.md` : défauts regroupés par S0…S3 avec reproduction et causes.
- `qa/screenshots/` : captures réellement issues des builds natifs, avec manifeste SHA/machine/DPR/backend.
- `qa/test-results/` : journaux Debug/Release/CI, comparaison avant/après.
- `qa/remaining.md` : `NOT_RUN`, `ABSENT`, `BLOCKED`, risques acceptés et décisions attendues.
- `qa/closure.md` : % de couverture calculé sur lignes vérifiées, sans compter `NOT_RUN` comme vert ; avis final circonstancié.

## Références du dépôt consultées le 8 novembre 2026

- `docs/desktop-v2/23-espaces-studio.md`
- `docs/desktop-v2/21-studio-neo-theme.md`
- `docs/desktop-v2/19-studio-matrice-migration.md`
- `docs/desktop-v2/05-parcours.md`
- `docs/desktop-v2/13-retards-v2-vs-v1.md` (historique, pas état courant garanti)
- `docs/desktop-v2/14-dependances-parcours-et-visuel.md`
- `docs/desktop-v2/17-studio-gpu.md`, `18-studio-benchmarks.md`
- `apps/desktop_v2/studio/CMakeLists.txt`, `tests/unit/desktop_v2/CMakeLists.txt`
- PRs #291 et #289 (statuts observés au moment de lecture, à recontrôler)