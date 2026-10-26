# Format de travail sémantique pour agents — openstitch.semantic/1

État : le format est **importable nativement** dans un OSP depuis le 2026-10-23 : opérations
`semantic_import` et `semantic_replace` (CLI `project op`, MCP), validation par `semantic_validate`
(MCP) ou `scripts/openstitch_semantic.py`, carte de provenance par `semantic_map`. Le desktop n'appelle
pas encore ces opérations (pas d'annulation côté IHM, voir `docs/ai/autodigitize-architecture.md` F4).

Le manifeste évite qu'une segmentation par couleurs fasse perdre l'identité
des structures : pierre, lierre, roseau, reflet et nénuphar. Il décrit les
familles, instances, masques, priorités, intentions de point et occlusions.

Ce format s'appuie sur le vocabulaire de
.claude/skills/openstitch-digitize/references/segmentation-semantique.md.

## Exemple minimal

~~~json
{
  "schema": "openstitch.semantic/1",
  "source_image": {"path": "source.png", "width_px": 1536, "height_px": 1024},
  "design": {"width_mm": 300, "height_mm": 200},
  "regions": [
    {
      "semantic_id": "architecture.arc1",
      "family": "architecture",
      "level": "instance",
      "role": "arche gothique",
      "priority": "protected",
      "stitch_intent": null,
      "parent_id": null
    },
    {
      "semantic_id": "architecture.arc1.pierre1",
      "parent_id": "architecture.arc1",
      "family": "architecture",
      "level": "technical",
      "role": "pierre claire",
      "priority": "important",
      "protected_detail": true,
      "stitch_intent": "tatami",
      "thread_rgb": "#E8CB91",
      "mask_path": "masks/pierre1.png",
      "under": ["vegetation.lierre1"]
    },
    {
      "semantic_id": "vegetation.lierre1",
      "parent_id": null,
      "family": "vegetation",
      "level": "technical",
      "role": "lierre sur l'arche",
      "priority": "important",
      "stitch_intent": "auto_satin",
      "thread_rgb": "#738643",
      "mask_path": "masks/lierre1.png",
      "over": ["architecture.arc1.pierre1"]
    }
  ]
}
~~~

Un objet structurel a un stitch_intent nul et peut ne pas avoir de masque.
Un objet cousu indique un masque PNG de mêmes dimensions en pixels que
l'image source. Le chemin du masque est relatif au fichier JSON du manifeste.

Champs reconnus :
- semantic_id : identifiant stable unique (ASCII alphanumérique, _, ., :, -).
- parent_id : référence existante et sans cycle dans l'arborescence.
- family, role : catégorie et fonction visuelle explicites.
- level : family, instance, component, technical.
- priority : protected, important, secondary, optional.
- stitch_intent : tatami, auto_satin, running, directional, satin,
  unstitched ou null.
- thread_rgb : teinte #RRGGBB des fils (distincte de l'identité sémantique).
- under, over : identifiants de régions ; graphe d'occlusion sans cycle.
- protected_detail, visible_on_fabric : booléens.
- decision : keep, merge, remove ou review.

D'autres champs de provenance peuvent être conservés par l'agent (instance_id,
merge_group, vector_ids, embroidery_ids), sans être modifiés par le validateur.

## Utilisation

~~~bash
python3 scripts/openstitch_semantic.py travail/carte.json
python3 scripts/openstitch_semantic.py travail/carte.json --skip-files
python3 -m unittest discover -s tests/scripts -p 'test_openstitch_semantic.py'
~~~

La sortie est un document JSON de schéma openstitch.semantic-validation/1
avec un indicateur ok et des erreurs structurées (code, path, message).
Code de sortie : 0 en cas de succès et 2 en cas d'échec. Le mode --skip-files
permet de valider la structure avant la fabrication des masques.

## Limites et étape suivante

Le validateur lit seulement la signature PNG et l'en-tête IHDR, pas
l'alpha ou la couverture des masques. Il ne génère aucun point, ne modifie
aucun OSP, n'effectue aucune fusion géométrique et ne calcule pas une
correspondance entre régions sémantiques et objets de broderie.

B02 : opérations atomiques sur masques et fusion contrôlée par famille/instance (restant).
B03 : import dans un projet OSP conservant les identifiants sémantiques — **natif** (`semantic_import`, identité dans le nom d'objet `sem:<id> <n>`) ; intentions `directional`/`satin` sans guides refusées.
B04 : re-numérisation locale — **native** (`semantic_replace`, même place de couture, autres objets intacts, IDs natifs renouvelés).

Le validateur Python ne couvre toujours que la structure ; la validation native fait foi pour l'import.
Extensions prévues (rétrocompatibles, voir `docs/ai/`) : boîte et score par région, provenance du backend
(`backend_id`, `model_hash`), relations typées.
