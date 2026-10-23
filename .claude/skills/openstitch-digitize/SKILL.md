---
name: openstitch-digitize
description: Numériser, analyser et améliorer une broderie OpenStitch de façon interprétative, de l'image initiale aux objets sémantiques, au choix satin/tatami/contour, au projet OSP et au DST. Pour une image, un OSP existant, un motif trop simplifié, trop d'îlots/coupes, des satins inadaptés ou une broderie à retravailler.
---

# Numérisation textile interprétative — OpenStitch

**Mission.** Produire la meilleure broderie raisonnablement réalisable avec OpenStitch, et pas simplement convertir des pixels en DST. L'agent analyse, reconstruit, segmente, choisit les points, rend, critique, modifie et re-teste. Le **projet .osp éditable est la source de vérité** ; le DST n'est qu'un export. Un rendu logiciel ne valide jamais un essai physique sur machine.

## Documents à consulter selon le besoin

- [Segmentation sémantique et fusion raisonnée](references/segmentation-semantique.md) — masques, instances, structures porteuses, reconstructions et refus de fusion.
- [Sélection expérimentale des trois satins](references/selection-satins.md) — squelette, balayage, axe médian, couverture et rendu.
- [Audit des contours et retouches/validation](references/retouches-et-validation.md) — suppression, conversion, densité, coupes, tests et livrables.
- [Cas chiffré des ruines V1 à V9](references/cas-ruines-v1-v9.md) — hypothèses, gains réels et échecs.
- [Réglages](references/parametres.md), [diagnostic](references/diagnostic.md), [journal d'essais historiques](references/journal-essais.md).
- Hors Claude Code, ce même texte est servi par le serveur MCP `openstitch-mcp` (outil `digitize_guide`, ressources, prompt `digitize` ; voir [mcp.md](../../../docs/source/mcp.md)) : modifier ce skill suffit, le serveur l'embarque au build.
- [CLI projet](../../../docs/source/cli-project.md), [auto-numérisation](../../../docs/source/auto-numerisation.md), [satin-squelette](../../../docs/source/satin-squelette.md), [segmentation](../../../docs/source/segmentation.md), [workflow.sh](scripts/workflow.sh).

## Principes de décision

1. **Rôle textile avant couleur et géométrie.** Une pierre, la lune et un reflet peuvent partager le même fil beige tout en étant trois objets totalement distincts. Deux zones de même teinte ne sont jamais fusionnées sur cette seule base.
2. **Priorité à la lisibilité.** Préserver silhouettes, joints d'arches, nervures, épis, reflets, variation de feuillage. Ne pas réduire la complexité du dessin seulement pour faire baisser les coupes. Une simplification graphique trop forte est irréversible lors de la numérisation.
3. **Séparer puis réunir intelligemment.** Identifier les familles, **instances** et structures porteuses avant de créer des régions brodables. Fusionner des fragments d'une même liane peut aider ; relier deux nénuphars indépendants serait une erreur.
4. **Satin n'est pas un seul point.** Un objet admissible exige, si l'enjeu le justifie, une comparaison *skeleton / parallel_sweep / medial_axis*. Un satin techniquement produit peut rester laid, non couvert ou excessivement dense.
5. **Zéro erreur OpenStitch n'est pas une preuve de brodabilité.** Les densités sont simulées ; la tension réelle et les déformations sur pull nécessitent des essais physiques. Une baisse du nombre de points ou de coupes ne suffit jamais à conclure.
6. **Mesurer à échelle stable.** Les métriques de fidélité à la source ne sont directement comparables que si la source, la taille brodée, l'échelle et le fond considéré sont les mêmes ; le tissu non brodé intentionnel peut fausser un « manque de couverture » global.

## Phase 0 — Entrées, sauvegardes, CLI

L'utilisateur fournit l'image, éventuellement le projet existant, et la taille brodée. **Ne demander que la dimension si elle manque.** Si connues : tissu (extensible ou non), couleur du support, stabilisateur, palette, machine et cadre. Choisir et signaler les hypothèses restantes.

Conserver immuables original et projets témoins ; tous les essais dans un dossier temporaire **en dehors du dépôt**. Employer le binaire réel OpenStitch :

~~~bash
command -v openstitch-cli || true
# Si absent et sur Linux x86-64, depuis la racine du dépôt :
bash scripts/install-cli-linux.sh
~/.local/bin/openstitch-cli --version
~/.local/bin/openstitch-cli --help
~/.local/bin/openstitch-cli project --help
~/.local/bin/openstitch-cli project operations --json
~~~

L'installateur du dépôt vérifie SHA-256 et privilégie la release persistante ou une CI réussie de main. Ne pas exécuter d'artefacts inconnus issus d'une PR. Si indisponible, suivre la documentation d'installation ou compiler. Ne jamais prétendre avoir exécuté OpenStitch sans démarrer le binaire.

Calculer DPI = largeur_source_px × 25,4 / largeur_souhaitée_mm **si ces deux largeurs couvrent le même sujet**. Sinon mesurer la boîte englobante réellement brodée et recaler. Vérifier les dimensions du DST ; le cadre est plus grand que la dimension du motif et doit comporter une marge.

## Phase 1 — Lecture sémantique et reconstruction graphique

Avant toute quantification : inventorier les structures et leurs sous-structures, leurs recouvrements et leurs contours significatifs. Exemple des ruines :

- Architecture : arches, intrados/extrados, piliers, blocs, joints, ombres, pierres derrière lierre.
- Végétation : lierre accroché à telle arche, masses de feuillage, touffes de roseaux avec tiges individuelles.
- Ciel : lune, nuages et ciel qui traversent visuellement le soleil.
- Eau : eau de fond, reflets de lune, reflets verticaux, nénuphars **individuels**.
- Tissu visible : grandes ombres et interstices intentionnels à ne pas remplir.

Produire une reconstruction contrôlée avec des **aplats nets, régions fermées, bords propres, sans halos ni bavures**. Garder la source intacte. Important : ne pas refaire une illustration simplifiée « jolie » qui efface les petits joints, roseaux et reflets de l'original. Comparer au besoin une préparation *fidèle* et une préparation *allégée* au même gabarit.

Affecter des **identifiants techniques indépendants des fils** et une arborescence : scène → famille → instance → composant → objet brodable. Documenter instances, parents, sous-couches/occlusions et détails protégés. Ne jamais utiliser seulement le RGB comme identité.

## Phase 2 — Segmentation sémantique puis technique

Créer des masques / objets logiques distincts pour les familles et les **instances**. Les fausses couleurs techniques servent à différencier deux régions à broder même si elles utilisent le même fil final.

Puis découper techniquement selon les limites des points : sous-diviser lierre ramifié aux jonctions ; séparer bande satin de masse tatami ; distinguer pierre, joint de pierre, liane qui la recouvre ; préserver des trous et la transparence du tissu. Mesurer aires et largeurs **en mm**. Conserver les informations dans **carte_semantique.png**, **segmentation_technique.png**, **regions.csv** et **palette.json** avec un identifiant stable par objet. S'assurer qu'aucune région ne fuit, ne crée de halo ou ne se mélange avec une instance voisine.

La segmentation sémantique **n'est pas** une commande native promise par le CLI actuel : les masques peuvent être préparés en Python/OpenCV, SVG ou par un outil d'image, avec validation géométrique explicite.

## Phase 3 — Fusion sémantique AVANT la vectorisation

Créer un graphe de voisinage contenant : instance, famille, structure porteuse, distance en mm, continuité de direction, intersections/occlusions et rôle textile. Pour chaque fragment : conserver, rattacher, reconstruire une continuité, conserver comme trait, supprimer comme doublon, ou demander un réexamen.

**Fusionner seulement** avec preuve que les deux morceaux appartiennent à une **même structure visuelle** et que la nouvelle forme demeure brodable : pas de passage à travers un autre objet, pas de jonction trop large pour le satin, pas de disparition de contour significatif. Tester les raccords sur copies, comptabiliser leur aire ajoutée et comparer rendu/coupes/densité. L'outil de nettoyage des îlots par proximité colorimétrique ne remplace pas cette analyse : il peut effacer une branche ou un joint.

Si on part d'un OSP existant, la fusion native peut réunir des polygones **disjoints** sans les faire coudre en une seule traversée : ne pas promettre des coupes en moins. Le cas V8 mesuré le démontre ; une reconstruction *avant* segmentation est prioritaire pour les grosses réductions d'îlots.

## Phase 4 — Intention de point et palette finale

Attribuer **par fonction visuelle**, jamais par simple taille de région :

| Intention | Candidats | Risques / réévaluation |
|---|---|---|
| Tissu visible | ombre, grande masse sombre | doit être compatible avec la couleur du support |
| Tatami | ciel, eau, lune, surface de nénuphar, pierre large | interstices, sous-couche et uniformité de direction |
| Auto-satin | branches, lianes, roseaux, bordures, rubans, reliefs de pierre | jonctions, largeurs, couvertures, éventails disgracieux |
| Point droit | nervures, petites séparations, joints fins | faux « contours » issus de mini-régions de remplissage |
| Supprimer | parasite ou doublon sans fonction propre | vérifier qu'aucun détail utile n'est perdu |

Puis réduire la palette réelle de fils **sans écraser les identités techniques**. Une famille peut employer plusieurs fils, un fil peut servir dans plusieurs familles. Les « 5 fils » d'une illustration n'ont pas valeur universelle. Penser orientation locale, recouvrement, ordre de couture et zones protégées.

## Phase 5 — Première numérisation OpenStitch

Pour des aplats, utiliser le mode **shapes** ; mode **contours** pour un dessin de traits ; **stipple** pour certaines sources tonales. Les réglages se découvrent sur le binaire installé (voir documentation). Un balayage de quelques paramètres est pertinent **après** nettoyage sémantique, jamais comme remplacement de celui-ci.

~~~bash
# Exemple seulement : DPI, cadre et fond doivent être recalculés !
openstitch-cli project sweep image_preparee.png --dpi 140 --max-colors 12 \
  --vary island-cleanup=0,3 --vary fill-inset=0,0.1 --sheet sweep.png
openstitch-cli project digitize image_preparee.png -o essai_01.osp \
  --dst essai_01.dst --dpi 140 --skip-background no \
  --debug-dir debug_01 --report digitize_01.json
openstitch-cli project set essai_01.osp --width-mm 260 --height-mm 200
openstitch-cli project inspect essai_01.osp --json > inspect_01.json
~~~

**Attention au fond :** l'option auto peut parfois ignorer la lune ou une grande surface utile ; à l'inverse il faut exclure les zones transparentes/papier sans les coudre. Observer les décisions réelles et la palette d'objets de l'OSP. Le script [workflow.sh](scripts/workflow.sh) permet une première passe technique mais ne fait **pas automatiquement** la reconstruction et le classement sémantique.

## Phase 6 — Tester les trois satins sur les objets pertinents

Chaque candidat identifié en auto-satin et important visuellement doit être essayé, sur **trois copies/versionnements**, dans la mesure des capacités du moteur :

- **skeleton** : cohérent sur certaines ramifications mais parfois dense ;
- **parallel_sweep** : parfois moins dense, peut laisser une forme incomplète et produire des éventails ;
- **medial_axis** : intéressant ponctuellement, mais peut produire **zéro point** ou une couverture faible.

Ne pas décider de la méthode sur la seule famille ou sur le nombre de points. Comparer **couverture par objet, absence de points, refus, pertes de bord, direction du fil, aspect du rendu OpenStitch, densité et coupes**. Si aucune stratégie ne convient : scinder la géométrie, revenir au tatami/point droit ou laisser vide suivant le rôle.

OpenStitch expose les trois stratégies via l'opération **auto_satin** ou les paramètres **strategy** des objets existants. **set_stitch_type ne convertit pas en auto-satin** ; employer auto_satin et vérifier son résultat. Lire [selection-satins.md](references/selection-satins.md), notamment les 11 objets où medial_axis n'avait produit aucun point dans une expérience réelle.

## Phase 7 — Retouches, îlots, « Région en contour », densités

Sur **une copie du projet** :

1. Auditer **tous les running**. Conserver un trait volontaire ; tester en satin une petite zone de repli réellement allongée ; retirer seulement les satellites redondants (attention aux joints de pierre et aux tiges). Une conversion est refusée si non couverte ou inadaptée à la topologie.
2. Auditer les îlots satin/tatami, chercher des structures porteuses **en amont**. La fusion native sans continuité de géométrie ne garantit aucune économie de coupe.
3. Lire le heatmap de densité : alléger des sous-couches inutiles, espacements, recouvrements et ordre ; ne pas supprimer arbitrairement des détails.
4. Tester ordre et trajets cachés **après** le travail géométrique, sans mélanger les couleurs ou inverser les occlusions.
5. Examiner **vraiment** le rendu global et les zooms. Si défaut de segmentation : retour à l'image et **nouveau .osp** ; si défaut de paramètre/type : retouche du .osp. Ne jamais inventer une commande pour remplacer la source d'un OSP.

Effectuer plusieurs cycles *mesure → diagnostic de cause → correction → nouveau rendu → comparaison* pour un motif complexe. Conserver la meilleure version connue, et des journaux de décisions/rejets. Les 52 contours convertis et 38 supprimés du cas V9 sont un exemple **non universel**, pas une recette à recopier.

## Phase 8 — Analyse reproductible et contrôle

~~~bash
openstitch-cli project check essai_01.osp --json > check_01.json
openstitch-cli project diagnose essai_01.osp --json > diagnose_01.json
openstitch-cli project fidelity essai_01.osp --map fidelite_01.png --json > fidelite_01.json
openstitch-cli project density essai_01.osp --heatmap densite_01.png --json > densite_01.json
openstitch-cli project render essai_01.osp -o preview_01.png --px-per-mm 7
openstitch-cli project select essai_01.osp --where type=running --json > running_01.json
openstitch-cli project at essai_01.osp --px 500,320
openstitch-cli stats essai_01.dst
openstitch-cli project diff essai_01.osp essai_02.osp --json > diff.json
~~~

Lire **project operations --json** pour les noms et schémas effectifs des opérations. Pour une retouche en lot, employer **project apply** avec le schéma **openstitch.batch/1** ; exécuter d'abord **--dry-run**, vérifier ses résultats transactionnels et les IDs (toujours issus de inspect), puis appliquer vers un nouveau fichier avec **-o nouvelle_version.osp**. Les erreurs de batch laissent le projet inchangé. Voir [retouches-et-validation.md](references/retouches-et-validation.md).

Faire figurer côte à côte, à source identique : dimensions, objets par type, points, coupes, sauts, changements de fil, zones ≥ 4 couches, manques intentionnels, défauts de couverture, rendu sur couleur de tissu choisie. Les mesures de densité sont **simulées** ; la fidélité à l'image intégrée ne juge pas automatiquement de la justesse artistique. Nombre d'avertissements imprimés potentiellement plafonné ; **0 erreur bloquante ≠ validé machine**.

## Phase 9 — Livrables et critères d'arrêt

~~~text
travail/
  source_originale.png
  source_reference.osp                   # si fournie
  semantique/
    reconstruction_fidele.png
    carte_semantique.png
    segmentation_technique.png
    regions.csv
    palette.json
  essais/
    01_initial/{projet.osp,preview.png,mesures.json,notes.md}
    02_satins/{projet.osp,preview.png,selection_satins.csv}
    03_fusions/{projet.osp,preview.png,fusions_semantiques.csv}
    04_contours/{projet.osp,preview.png,decisions_contours.csv}
  final/
    broderie.osp
    broderie.dst
    preview.png
    rapport.md
  broderie_complete.zip
~~~

**Obligatoires** si la mission est complète : OSP éditable, DST exporté de **ce même OSP**, rendu **project render**, carte sémantique, segmentation technique, table de fils et régions, rapport des essais et limites, archive ZIP. Vérifier dimensions finales, cadre, points/coupes/fil et cohérence des sorties.

S'arrêter si les silhouettes et détails structurants sont fidèles, si la majorité des îlots restant a une fonction, si aucune erreur bloquante n'est détectée, si les problèmes de densité sont compris et si des variantes ultérieures n'apportent plus de gain significatif. Ne pas promettre qu'un pull sera brodable sans **échantillon réel**, stabilisateur et vérification des tensions.

## Frontières et sécurité de méthode

- **Skill de workflow**, pas nouvelle commande OpenStitch : la segmentation sémantique et son graphe d'instances peuvent nécessiter OpenCV/Python/SVG.
- Ne pas activer **--satin-bands** par défaut sans relire les réserves de brevet dans [parametres.md](references/parametres.md).
- Ne jamais inventer métriques, exécutions, noms de commandes ou validation sur machine.
- Ne pas modifier le code source ni ouvrir une PR au cours d'une simple mission de numérisation, sauf demande explicite.
- Documenter toute nouvelle expérimentation (avant/après, dimensions, source) plutôt que transformer des heuristiques locales en règles universelles.
