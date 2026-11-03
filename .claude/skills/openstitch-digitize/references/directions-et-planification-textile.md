# Planification textile, directions de fil et portes de validation

*Retour d'expérience OpenStitch : scène maritime du navire (2026-10-23). Ce texte complète le skill principal ; les observations sont propres à cette expérience et **ne constituent pas** des réglages universels.*

## 1. Quatre portes avant la livraison

- **A — lecture graphique** : reconstruire la composition, nommer les instances et les objets porteurs, conserver voiles/coque/mâts/cordages et les principales crêtes ; comparer à l'image originale à taille identique. Si la lecture échoue **avant les points**, reprendre les masques, pas le DST.
- **B — plan textile par objet** : décider du point (tatami, directional, satin, running, absence de fil) à partir du rôle, de la largeur **locale**, de la topologie et du mouvement graphique. Écrire la justification et les directions souhaitées. Les régions de même fil ne sont pas nécessairement le même objet.
- **C — projet OSP réel** : importer des masques indépendants du RGB si possible, fixer les guides et les relations de superposition, générer les points avec le vrai moteur, contrôler les objets effectivement créés. Les scripts Python de pont sémantique et `project digitize` sont des **méthodes différentes**.
- **D — décision de garder/rejeter** : rendre en entier et en zoom avec `project render`, comparer au témoin, contrôler couverture, directions, sous-couches, recouvrements, tenue des silhouettes et risque physique ; exporter le DST **depuis l'OSP retenu**. Un échec artistique bloque la livraison « finale » même si `check` renvoie zéro erreur.

Documenter une fiche par objet : `semantic_id, instance_id, role, priority, over, under, mask_path, width_local_mm, stitch_intent, direction_intent, fabric_visibility, underlay, pull_compensation, rationale`. Certains champs sont des **annotations de l'agent**, pas nécessairement des champs supportés par le manifeste ou l'OSP ; ne jamais prétendre que la CLI les importe tous.

## 2. Décider selon la structure, pas la couleur ni la seule boîte englobante

| Région | Option de départ | Ce qui peut invalider l'option |
|---|---|---|
| Grandes voiles, larges nuages | tatami | angle arbitraire, registre, sous-couche excessive |
| Vague continue avec courbe dominante | directional guidé **si géométrie adaptée** | guides tournant à contresens, jonctions, nombreuses coupes |
| Crête ou bande étroite continue | auto-satin testé A/B/C | changement de largeur, embranchements, éventails, trous |
| Mât/cordage très fin | running ou colonne satin étroite | largeur insuffisante, sauts apparents |
| Coque | volumes tatami + arêtes running/satin séparées | superpositions excessives, silhouette perdue |
| Ombre de fond sur textile assorti | tissu apparent intentionnel | support réel de couleur différente |

Ne pas convertir un gros objet en satin parce que sa *bounding box* est allongée : vérifier largeur locale, bras et jonctions. Ne pas remplir automatiquement tout le fond sombre sur un pull marine. Priorité au **sujet principal** lors des arbitrages (ex. coque/voiles du navire).

## 3. Direction des fils : ce qu'OpenStitch sait effectivement faire

Le CLI expose **`directional_guides`** : cette opération convertit tatami/directional en `directional` et génère des guides **depuis l'axe médian de la forme**. Cela n'est **pas** équivalent à fournir une courbe artistique tracée par l'agent et ne garantit pas un flot esthétique correct. Les champs `guides` et `break_lines` existent dans les paramètres directional ; vérifier leur contrat actuel via `project operations --json` et `project inspect`.

Exemple à adapter à l'objet **réellement inspecté** (ne jamais supposer l'ID) :

~~~bash
openstitch-cli project inspect scene.osp --json > inspect.json
openstitch-cli project op scene.osp directional_guides --arg object=42 -o scene_dir.osp --json
openstitch-cli project inspect scene_dir.osp --object 42 --json > objet_42.json
openstitch-cli project render scene_dir.osp -o preview_dir.png --px-per-mm 8
~~~

Pour des vagues complexes : couper la masse en **rubans appartenant à la même vague** si un unique axe médian tourne mal. Visualiser l'orientation des rangées avant d'approuver. La documentation Ink/Stitch sur le *Guided Fill* est une **référence conceptuelle**, pas une fonctionnalité OpenStitch équivalente certifiée.

## 4. Satin : classification puis trois essais, pas une conversion massive

1. Isoler des colonnes lisibles et fermées : largeurs locales et jonctions cohérentes ; rails et traverses lorsqu'une géométrie manuelle le permet.
2. Ne convertir en satin que les éléments justifiés (liserés, rubans, petits reliefs), jamais une liste automatique de « toutes les formes de moins de X mm de boîte ».
3. Sur une **même géométrie** et à paramètres comparables, essayer `skeleton`, `parallel_sweep` et `medial_axis`.
4. Rejeter une sortie à zéro point, fortement lacunaire, avec éventail/vrillage, mauvais sens ou pic de densité, même si `auto_satin` a renvoyé un succès.
5. Si aucune stratégie n'est acceptable : **re-segmenter / ajouter guides / découper**, ou revenir à un running/tatami.
6. Mettre à jour `selection_satins.csv` et ne jamais dire qu'un satin a été « testé » sans vraie exécution ni aperçu.

**Limitation du pont existant :** `scripts/openstitch_semantic_import.py` sait actuellement importer des masques tatami/running/auto_satin, et initialise l'auto-satin à `skeleton` ; il ne prend pas directement `directional` ni des colonnes satin manuelles complexes. Après cet import, utiliser les opérations natives, contrôler le résultat et conserver la traçabilité `sem:<id>` + sidecar. Ne jamais contourner cette limitation en laissant supposer que le manifeste a importé directement les guides.

## 5. Plan physique : ordre, dessous, sous-couche et tissu

- Construire le graphe `under/over` **avant** de trier par couleurs ou proximité.
- Poser les grands fonds et les supports de manière compatible avec les superpositions ; détails de structure devant, souvent après. Optimiser le routage **après** satisfaction des occlusions.
- Préserver une zone de recouvrement contrôlée au bord, mais éviter les surfaces entières cachées **quand la géométrie peut être soustraite sans perdre le maintien**. `project merge --mode subtract` est une opération booléenne par objet, **pas** un effaceur professionnel automatique des points de dessous ; tester sur une copie.
- Vérifier sous-couches cumulées, espacements, compensation push/pull et risques propres au tissu. Les valeurs 0,4 mm d'espacement ou 0,2–0,4 mm de compensation sont des **points d'essai**, jamais des normes pour toutes les machines.
- Un motif textile dense ou grand nécessite un **échantillon réel** sur le tissu et stabilisateur cible ; une heatmap n'est pas une carte physique de tensions validée.

## 6. Audit de l'essai « navire directionnel » et anti-règles

Les nombres suivants proviennent de deux OSP OpenStitch testés à **la même échelle** ; ils comparent des métriques simulées, pas une mesure de broderie sur machine :

| Mesure | `navire_restart_satin.osp` | `navire_guided.osp` |
|---|---:|---:|
| Tatami / auto-satin / directional | 90 / 23 / 0 | 49 / 23 / 41 |
| Points | 107 362 | 122 695 |
| Coupes | 425 | 705 |
| Sauts | 501 | 961 |
| Zone hotspot estimée | 1 679 mm² | 1 877 mm² |
| Erreurs bloquantes au `check` | 0 | 0 |
| Avertissements au `check` | 52 | 52 |

L'essai à 41 objets directionnels produit des **flux visuellement plus courbes**, mais augmente points, coupes et hotspot. L'idée est intéressante ; une conversion massive de toutes les vagues/nuages n'est pas validée pour la production. Préférer ensuite des **tests locaux** de quelques rubans importants, comparer systématiquement les deux versions, conserver l'ancienne si la nouvelle gêne la lisibilité ou le tissu. Dans l'essai, `directional_guides` a été exécuté mais la tentative de retouche en lot de ses paramètres a été refusée ; ne pas présenter ce second réglage comme réalisé.

Ce que cet essai **n'a pas prouvé** : segmentation entièrement sémantique, colonnes satin avec traverses manuelles, suppression des sous-couches cachées, essai physique ou validation d'un champ de courbe fourni manuellement.

## 7. Contrôle, traçabilité et sorties honnêtes

~~~bash
openstitch-cli project inspect candidat.osp --json > inspect.json
openstitch-cli project check candidat.osp --json > check.json
openstitch-cli project diagnose candidat.osp --json > diagnose.json
openstitch-cli project density candidat.osp --heatmap densite.png --json > densite.json
openstitch-cli project fidelity candidat.osp --map fidelite.png --json > fidelite.json
openstitch-cli project render candidat.osp -o rendu.png --fabric '#0A1F44' --px-per-mm 8
openstitch-cli project export candidat.osp --format dst -o broderie.dst
~~~

Les options doivent être confirmées par `--help` du binaire présent. Exemples de mesures non interchangeables : fidélité **à l'image technique** ≠ fidélité artistique à l'original ; zéro erreur du moteur ≠ validé sur tissu ; meilleur score de couverture ≠ meilleur sens de fil ; nombre de points plus faible ≠ meilleure broderie.

Livrer si possible `source`, `reconstruction`, `carte_semantique`, `masques`, `regions`, `palette`, `OSP`, `DST exporté de cet OSP`, `preview render`, `diagnostics`, `rapport de décisions et limites`. En cas d'impossibilité technique, rendre les fichiers réellement obtenus et décrire précisément l'étape non réalisée, sans fabriquer un OSP ou un aperçu.

## Références professionnelles (inspiration et vérification)

- Ink/Stitch : [colonne satin](https://inkstitch.org/fr/docs/stitches/satin-column/), [remplissage guidé](https://inkstitch.org/fr/docs/stitches/guided-fill/), [sous-couche](https://inkstitch.org/tutorials/underlay/).
- Wilcom : [dessin technique initial](https://docs.wilcom.com/embroiderystudio/26/en/OnlineHelp/Setup/hardware/Prepare_enlargement_drawings.htm), [suppression des points sous-jacents](https://docs.wilcom.com/embroiderystudio/26/en/OnlineHelp/Quality/quality/Removing_underlying_stitching.htm), [compensation](https://docs.wilcom.com/embroiderystudio/26/en/OnlineHelp/Quality/underlays/Apply_automatic_pull_compensation.htm).
- Madeira : [prévention des plis](https://www.madeira.com/fr/service-/-assistance/conseils-dutilisation-en-broderie/comment-eviter-les-plis-pendant-la-broderie).
- Les concepts décrits par d'autres éditeurs ne doivent pas être assimilés à des fonctions déjà présentes dans OpenStitch ; ne pas copier de code ou d'algorithme sous licences incompatibles (cf. `CLAUDE.md`).
