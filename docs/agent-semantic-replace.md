# Renumerisation locale semantique (B04 partiel)

Le pont `scripts/openstitch_semantic_replace.py` remplace les objets d'un seul identifiant semantique. Il utilise le fichier OSP existant, l'inspection native, l'import de SVG et les lots transactionnels ; les autres objets de broderie ne sont pas renumerises.

    python3 scripts/openstitch_semantic_replace.py scene.osp scene.json --semantic-id morse12.defense --mask masks/defense_corrigee.png --out scene-v2.osp --cli openstitch-cli

Les noms semantiques restent stables. Les IDs numeriques OpenStitch de l'objet remplace changent et le nouveau sidecar est mis a jour ; les objets non cibles conservent leur geometrie/parametres. La sortie est refusee si elle existe. Aucun projet original n'est modifie. Les operations natives de remplacement sont validees par `project apply --dry-run` avant application sur une copie temporaire.

**Limites :** pont CLI Python, non operation C++ atomique avec undo/redo bureau. Un remplacement changeant fortement les pieces ou leur surface requiert un controle manuel du rendu, du nombre de coupes et de la densite. Le protocole ne tente pas de garder les IDs internes des objets supprimes.
