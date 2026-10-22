# OpenStitch — Roadmap pour agents (CLI / automatisation)

*État : piste de développement dédiée ; statut du code à vérifier dans chaque PR.*
*Cette roadmap complète la parité Hatch (`docs/roadmap-parite-hatch.md`) et ne la remplace pas.*

## Objectif

Permettre à un agent de comprendre une scène, préparer des masques sémantiques,
numériser vers un projet OSP éditable, appliquer des retouches ciblées, mesurer et
comparer les résultats, puis exporter depuis **le même OSP**. Le moteur doit rester
sans dépendance à un service LLM, déterministe, sans Qt, et toute mutation du
document doit passer par une `ICommand`.

## Priorités et dépendances

| ID | Priorité | Livrable | État |
|---|---|---|---|
| A01 | P0 | Installation / diagnostic d'environnement | ◐ Script agent `doctor` ; pas encore de commande native |
| A02 | P0 | Schémas et erreurs contractuels pour agents | ◐ Le CLI expose déjà `commands --json` et `project operations --json` |
| A03 | P0 | Inspection compacte, sélection, classement | ◐ Pont de lecture `scripts/openstitch_agent.py inspect` |
| B01 | P0 | Manifeste sémantique familles/instances/masques | ☐ |
| B02 | P0 | Éditeur de segmentation sémantique | ☐ ; dépend de B01 |
| B03 | P0 | Import d'objets sémantiques vers un OSP | ◐ Pont Python CLI ; import natif/desktop restant |
| B04 | P0 | Re-numérisation locale sans perte d'identité | ◐ Pont Python sur copie OSP ; ID sémantique stable, IDs natifs renouvelés |
| C01 | P1 | Retouches collectives selon critères sémantiques | ☐ ; dépend de B03 |
| C02 | P1 | Benchmark satin objectivé par objet | ☐ |
| C03 | P1 | Diagnostics avec propositions réversibles | ☐ |
| C04 | P1 | Rendus annotés et comparaison régionale | ☐ |
| D01 | P1 | Gestion d'expériences, cache et reprise | ☐ |
| D02 | P1 | Profils tissu/machine/cadre | ☐ |
| D03 | P1 | Paquet de livraison validé et traçable | ☐ |
| D04 | P2 | SDK et MCP optionnel au-dessus du CLI | ☐ ; dépend de A/B |
| D05 | P1 | Corpus visuel et textile de régression | ☐ |

Ne marquer « fait » que lorsque **tests + critères d'acceptation** sont
réunis sur `main`. Un script ne doit pas être présenté comme une
commande native. Les travaux du cœur/desktop sont distincts du pont de lecture.

## Premier lot : pont de lecture agent (A01, A03 partiels)

Sans dépendance Python supplémentaire :

```bash
python3 scripts/openstitch_agent.py doctor
python3 scripts/openstitch_agent.py --cli /chemin/openstitch-cli doctor
python3 scripts/openstitch_agent.py inspect motif.osp --sort jumps --limit 20
python3 scripts/openstitch_agent.py inspect motif.osp --type auto_satin --min-area-mm2 2
python3 -m unittest discover -s tests/scripts -p test_openstitch_agent.py
```

- Exécutable : argument `--cli`, variable `OPENSTITCH_CLI`, PATH, chemin local
  ou compilation Linux.
- Format : JSON strict, `schema=openstitch.agent/1`, code de sortie 0/2 ;
  aucun téléchargement et aucune mutation OSP.
- `doctor` appelle le binaire officiel et contrôle les schémas connus
  plutôt que de déduire sa version de son nom de fichier.
- `inspect` filtre par type/couleur/aire, trie et limite les informations
  transmises au contexte de l'agent ; les IDs proviennent du projet inspecté.
- Le classement par coupes/sauts est un **outil d'exploration**,
  pas un verdict de brodabilité : compléter avec `project check`,
  `project diagnose` et des essais sur machine.

### Critères d'acceptation des prochains lots

**B01** : schéma versionné et validateur d'instances (identifiants stables,
parents existants, relations sans cycles, masque non ambigu, unités, priorité).
**B02** : opérations atomiques, masques réellement modifiés, comparaison
avant/après, protection des détails et traçabilité.
**B03** : chaque objet OSP conserve sa provenance sémantique à travers la
vectorisation et la segmentation ; aucune association par seul RGB.
**C02** : les trois stratégies satin produisent des essais comparables sur
la même géométrie ; absence de points et trous sont des motifs de rejet.
**D03** : contrôles de cohérence OSP/DST, taille brodée, fichiers présents,
rendu OpenStitch, erreurs bloquantes et rapport transparent.

## Historique

- 2026-10-22 : création de la piste agent-first ; premier pont Python de
  diagnostic de capacités et d'inspection compacte. À poursuivre par B01.
