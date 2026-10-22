# OpenStitch Agent Trials — laboratoire d'expérimentation

**Branche persistante : `agent-trials` — à ne jamais fusionner dans `main`.**

Cette branche est le **journal public des expériences**, bonnes ou mauvaises, des agents qui
numérisent des motifs avec OpenStitch. Elle est indépendante des PR de code :
- `main` : code, skills, workflows et tests validés ;
- `agent-trials` : résultats, hypothèses, échecs, scores, comparaisons, preuves et références
  vers les artefacts de chaque tentative.

## Structure contractuelle

```text
experiments/
  README.md                         # présent sur cette branche
  runs/
    AAAA-MM-JJ/
      <run_id>/
        result.json                  # petit index et résultats (toujours conservé)
        notes.md                     # optional : compte rendu humain
  benchmarks/
    README.md                        # références immuables des images de test
```

Un `run_id` doit être unique et stable, par exemple
`20261023T080000Z-tentabrode-c0ffee1` (UTC + sujet + identifiant non ambigu).
**Un essai = un répertoire ; ne jamais écraser un résultat précédent.**
En cas de reprise, créer une nouvelle entrée avec `parent_run_id` et `previous_best`.
Ne pas maintenir un index central modifié par plusieurs agents concurrents.

## Contenu minimal de `result.json`

```json
{
  "schema": "openstitch.trial/1",
  "run_id": "20261023T080000Z-tentabrode-c0ffee1",
  "created_utc": "2026-10-23T08:00:00Z",
  "status": "blocked",
  "source": {
    "repo_path": "tests/fixtures/tentabrode.png",
    "sha256": "to-be-calculated-from-real-file",
    "size_mm": [240, 240]
  },
  "openstitch_commit": "real-commit-sha",
  "cli_version": "read-from-executable",
  "reason": "Example schema only: replace with an observed blocker",
  "score_before": null,
  "score_after": null,
  "evaluation": null,
  "artifact": {
    "kind": "github-actions-artifact",
    "url": null,
    "retention_days": null
  },
  "changes": [],
  "lessons": [],
  "parent_run_id": null
}
```

Cet exemple n'est **pas une expérimentation réalisée** : ne jamais le publier sous
`runs/` comme un résultat authentique. `status` vaut `completed`,
`failed`, `blocked` ou `incomplete` selon les faits observés.
Si une exécution est bloquée avant génération de l'OSP, conserver l'obstacle et
ne pas inventer de note ou d'artefact. L'absence de test sur machine se note.

## Stockage des gros fichiers et limites de rétention

**Ne pas committer** les images sources en doublon, ZIP, OSP, DST, PNG, captures ou
rapports volumineux dans `agent-trials`. Un lot d'artefacts d'une exécution
GitHub Actions est adapté pour le rendu, l'OSP et les diagnostics. **Attention** :
les Actions artifacts expirent selon la politique de rétention du dépôt.
Le lien et la date d'expiration doivent figurer dans `result.json`.
Pour assurer une conservation longue durée des OSP/DST, définir ensuite un
stockage pérenne distinct (release dédiée, bucket ou dépôt d'artefacts) plutôt
que supposer qu'un artifact Actions est permanent.

Conserver les JSON légers, scores, erreurs et leçons sur cette branche même
après expiration des binaires. Des résultats sans preuve exploitable doivent être
explicitement qualifiés `unverified` ; ne pas comparer leur score à une version
inspectée.

## Comment enregistrer une nouvelle exécution

1. Se placer dans un espace de travail isolé et consulter `main` pour les
   skills courants : `.claude/skills/openstitch-digitize/SKILL.md` et
   `.claude/skills/openstitch-evaluate/SKILL.md`.
2. Numériser réellement l'image, générer OSP/DST, cartes sémantiques, rendu et
   rapports JSON natifs. Faire l'évaluation indépendante /20 en conservant
   `review.json` et `score.json`.
3. Déposer les gros fichiers dans un artefact d'exécution, consigner son lien
   **réel**, son ID et sa rétention. Si l'hébergement est indisponible, signaler
   explicitement `artifact.kind = "unavailable"`.
4. Dans `agent-trials`, créer un **nouveau** dossier
   `experiments/runs/AAAA-MM-JJ/<run_id>/`. Le commit n'ajoute que
   `result.json` et éventuellement `notes.md`. Inclure :
   source+hash+dimensions ; SHA d'OpenStitch ; décisions et versions ;
   meilleurs scores ; erreurs, rapports techniques, leçon utile et liens.
5. En cas de collision concurrente ou ref obsolète, relire la branche,
   réconcilier puis retenter, **sans force-push** et sans réécrire les anciens essais.
6. Les corrections de l'algorithme ou du skill font l'objet d'une PR **séparée**
   vers `main` après tests de non-régression ; jamais depuis le journal.

## Sécurité et confiance

- Ne jamais utiliser les résultats d'une même image de test pour optimiser
  ET prouver l'amélioration générale sans lot de validation indépendant.
- Toute note doit être fondée sur le rendu du vrai moteur ; une estimation sans
  exécution est un `blocked`/`incomplete`, pas un 0/20 imaginaire.
- Protéger les sources personnelles : aucune image privée ou sous licence non
  autorisée ne doit être publiée dans les artefacts publics.
- `agent-trials` est un registre expérimental, pas un espace où exécuter du
  code non relu venant des rapports.
