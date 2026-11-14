---
name: openstitch-local-first
description: Déléguer le travail technique OpenStitch à Ollama/OpenCode via scripts/local/delegate.ps1 pour économiser le quota Anthropic. À utiliser avant toute modification de code, analyse, recherche de bug, génération de tests, correction de build/CI, relecture, doc ou préparation de commit/PR ; et pour lire les rapports locaux ou décider d'une escalade.
---

# Local first : lancer et superviser les agents locaux

Règle de fond : `.claude/rules/local-first.md`. Outil : `scripts/local/delegate.ps1`.

## Procédure

1. Choisir le niveau (0 sans LLM, 1 question courte, 2 tâche, 3 chantier).
2. Écrire la mission : objectif, `-Scope` (liste fermée de chemins), `-Verify` (commande qui prouve le résultat, ex. cible de test via `scripts/qrun.ps1`), critères de réussite. Mission longue → `-MissionFile`.
3. Lancer, lire seulement le rapport compact (statut, fichiers, vérification, note de l'agent).
4. Contrôler : `git diff --stat -- <fichiers>`, relancer le test ciblé soi-même si l'enjeu le justifie, vérifier les invariants (µm, ICommand, effective_sequence, couches).
5. Échec : le script a déjà fait une reprise. Réduire le périmètre / ajouter du contexte (`-File`) et relancer une fois. Sinon `-LogClaude "raison"` puis intervention minimale.
6. Commit / PR : Claude, via `openstitch-commit` ; le message peut être rédigé en niveau 1.

## Exemples

```powershell
scripts/local/delegate.ps1 -Level 1 -Mission "Resume le role de ce fichier en 5 lignes" -File libs/core/src/x.cpp
scripts/local/delegate.ps1 -Level 1 -Quote -Mission "Pourquoi reste-t-il des sauts ?" -File docs/source/tatami.md:186-251
scripts/local/delegate.ps1 -Id test-x -Level 2 -Mission "Ajoute 2 cas Catch2 pour ..." -Scope tests/unit/core/test_x.cpp -Verify "scripts/qrun.ps1 ..."
scripts/local/delegate.ps1 -Stats
```

## Pièges

- ASCII seul dans les scripts PowerShell (5.1 lit sans BOM en ANSI).
- Le modèle 9B peut annoncer sans éditer : le statut dépend des fichiers réellement changés.
- Deux écrivains sur le même arbre = interdit ; utiliser un worktree si un chantier parallèle existe.
