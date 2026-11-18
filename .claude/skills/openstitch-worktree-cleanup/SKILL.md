---
name: openstitch-worktree-cleanup
description: Nettoyer les worktrees git d'OpenStitch (worktrees/, .claude/worktrees/, dossiers frères comme ../emb-*) sans perdre de travail. À utiliser quand l'utilisateur demande de « nettoyer les worktrees », de faire de la place, ou quand `git worktree list` déborde de chantiers terminés.
---

# Nettoyage des worktrees

Principe : ne supprimer qu'un worktree **propre ET dont le travail est prouvé livré**. En cas de doute, garder et signaler.

## 1. Inventaire (une commande)

```bash
git fetch origin --prune -q
git worktree list --porcelain | awk '/^worktree /{p=$2} /^branch /{sub("refs/heads/","",$2); print p"|"$2} /^detached/{print p"|DETACHED"}' | while IFS='|' read p b; do
  dirty=$(git -C "$p" status --porcelain | wc -l)
  anc=$(git merge-base --is-ancestor "$(git -C "$p" rev-parse HEAD)" origin/main && echo ANC || echo -)
  pr=$( [ "$b" != DETACHED ] && gh pr list --head "$b" --state all --json number,state --jq '.[0]|"#\(.number) \(.state)"')
  echo "$(basename $p) | $b | dirty=$dirty | $anc | ${pr:-nopr}"
done
```

## 2. Règles de décision

| État | Action |
|---|---|
| `dirty>0` | **Garder** (travail non committé ; le dire à l'utilisateur) |
| `locked` dans `git worktree list` | **Garder** (agent en cours) |
| Worktree principal | Jamais |
| Propre + `ANC` (HEAD ancêtre de `origin/main`) | Supprimer |
| Propre + PR `MERGED` **et** HEAD local == `headRefOid` de la PR | Supprimer |
| Propre + PR `MERGED` mais HEAD différent | `git cherry origin/main HEAD \| grep -c '^+'` : si > 0, **garder** (commits jamais livrés, les squash-merges masquent l'ancestralité) |
| PR `OPEN`, aucune PR, `DETACHED` non ancêtre | **Garder** (signaler) |

Un squash-merge rend `merge-base --is-ancestor` faux : la PR `MERGED` + comparaison du `headRefOid` est le vrai test.

## 3. Suppression

```bash
git worktree remove <chemin>      # jamais --force sans accord explicite
git worktree prune
```

- Windows, « Filename too long » : `git worktree remove` échoue à mi-chemin ; finir avec `cmd //c "rmdir /s /q <chemin avec backslashes>"` puis `git worktree prune`.
- Ne pas supprimer les branches locales d'office : un worktree retiré ne perd rien, la branche reste. Proposer `git branch -d` (jamais `-D`) à part.
- Ne jamais toucher à `.claude/worktrees/agent-*` verrouillés ni à un worktree utilisé par une session concurrente.

## 4. Rapport

Lister : supprimés (pourquoi), gardés avec raison (sale / PR ouverte / commits uniques / verrouillé), et les branches candidates à suppression.
