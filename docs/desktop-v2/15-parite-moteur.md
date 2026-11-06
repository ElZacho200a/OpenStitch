# 15 — Parité moteur → bureau V2 : mesure et reste à faire

État au 2026-11-06. Objectif posé : **tout ce que le moteur sait faire doit être accessible dans le
bureau**. Ce chapitre mesure l'écart de façon reproductible (`python3 scripts/engine_parity.py`,
lecture seule, sans compilation) et dit ce qui reste.

## Méthode et limites (à lire avant les chiffres)

Le script compte les **opérations** de `libs/project_ops` (51) et les **commandes annulables** de
`libs/commands` (86), puis cherche chacune par son NOM dans `apps/desktop_v2` (et dans `apps/desktop`
pour les commandes, afin de savoir ce que la V1 savait déjà faire).

- « Non référencée » veut dire **à qualifier à la main**, jamais « absente » : une opération peut
  être exposée par une commande directe au lieu d'un lot JSON.
- Un nom présent dans le code ne prouve pas qu'on l'atteigne depuis l'interface.
- Ne sont PAS mesurés : les ~150 champs de réglage de points (niveau « expert » de l'inspecteur,
  voir `09`), les outils du CLI (`stitchdebug`, `dst2svg`, `stats`…), les analyses de `project_ops`
  (qualité, rapport, balayage, sémantique, repérage des couleurs, découpe d'appliqué, dégradé).

## Opérations du moteur : 12 sur 51 non référencées par la V2

| Opération | Verdict | Détail |
|---|---|---|
| `seg_clean_islands`, `seg_merge_colors`, `seg_merge_regions`, `seg_recolor_region`, `seg_remove_region` | ✅ exposées | par des commandes directes (panneau Régions), pas par ces noms |
| `set_design_points` | ✅ exposée | départ / arrivée dans « Options machine » |
| `set_order` | ✅ exposée | panneau d'ordre de couture (commande directe) |
| `remove_holes` | ◐ | suppression d'UN trou précis (commande « Supprimer le trou du nœud choisi », ce lot) ; « Combler les trous » par seuil d'aire existait déjà |
| `fill_block_holes` | ⬜ | combler les vides sans fil d'un bloc uni : non exposée |
| `set_geometry` | ⬜ | remplacer toute la géométrie d'un objet par un polygone : non exposée, faible valeur |
| `set_semantic` | ⬜ | sens sémantique d'un objet : non exposée |
| `discard_overrides` | ⬜ à qualifier | retouches manuelles de points : un « rétablir » existe en V2, à vérifier qu'il couvre ceci |

`remove_text` n'était pas référencée : « Supprimer le texte sélectionné » l'expose (ce lot).

## Commandes annulables : 29 utilisées par la V1 et pas par la V2

Beaucoup passent en V2 par des opérations par lot (renommer, supprimer, déplacer, type de points…) :
elles ne sont donc pas toutes des manques. Ce que je tiens pour de **vrais manques**, d'après le
code :

| Commande du moteur | Ce que ça permet | Verdict |
|---|---|---|
| `SetFabricCommand` | choisir le **tissu et le stabilisateur** du projet (alimente le risque de plissement et le tirage prédit) | ⬜ la V2 les lit mais ne les règle pas |
| `ConvertFillsToTatamiCommand` | convertir les remplissages en tatami en un geste | ⬜ |
| `EditAutoSatinCommand` | édition fine d'un auto-satin | ⬜ (`13` n°19) |
| `AddSatinGuidesCommand`, `MoveSatinGuidesCommand`, `RemoveSatinGuidesCommand` | guides de satin multi-sections | ⬜ (la V2 a les barreaux d'une colonne, pas ces guides) |
| `InsertDesignCommand`, `MoveInsertedPointCommand` | insérer un design et déplacer ses points | ⬜ à qualifier (l'import machine a été fait au rattrapage) |
| `MoveStitchPointCommand` | glisser un point cousu | ⬜ (la V2 retouche les points au clic) |
| `SetEmbroideryKnockoutCommand` | réglage d'« évidement » par objet | ⬜ |
| `SetEmbroideryJoinModeCommand`, `SetEmbroideryConnectorModeCommand` | jonction et connecteur par objet (les opérations `set_join` / `set_connector` sont référencées, à vérifier qu'une action les atteint) | ⬜ à qualifier |

Les autres (ajout / suppression / renommage d'objets, visibilité, verrou, déplacement, échelle,
type et paramètres de points, texte, segmentation…) sont à **qualifier à la main** : probablement
couvertes par des opérations par lot.

## Fait dans ce lot (édition vectorielle)

- **Type de nœud** (anguleux ↔ lisse) sur un ou plusieurs nœuds, en une commande annulable ;
  « lisse » crée les poignées qui manquent. La V2 ne pouvait pas faire d'angle franc sur une courbe.
- **Suppression d'un trou précis**, choisi par un de ses nœuds.
- **Suppression d'un texte entier** avec toutes ses lettres.

## Ordre proposé pour la suite

1. Formes prédéfinies et magnétisme des nœuds (`13` n° 24-25, moteur prêt : bibliothèque du CLI).
2. **Tissu et stabilisateur** (`SetFabricCommand`) : petit, et rend la prédiction de tirage réglable.
3. Convertir en tatami, guides de satin multi-sections, édition de l'auto-satin.
4. Qualifier à la main les 12 + 29 « à qualifier », puis relancer le script.
5. Mesurer ce qui n'est pas encore compté : champs experts, outils du CLI, analyses de `project_ops`.
