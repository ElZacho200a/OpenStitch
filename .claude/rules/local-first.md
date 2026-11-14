# Local first (Claude superviseur, Ollama exécutant)

Priorité : économiser le quota Anthropic. Claude comprend, découpe, délègue, vérifie les critères essentiels, arbitre.
Avant tout travail de code ou d'analyse : « Ollama peut-il le faire ? » Si oui, déléguer.

## Outil unique

`scripts/local/delegate.ps1` (PowerShell). Rapport compact en sortie ; détails dans `build/local-first/<id>/` (hors commits).

| Niveau | Quand | Appel |
|---|---|---|
| 0 | Déterministe (format, grep, tests, métriques) | commande directe, aucun LLM |
| 1 | Résumé, explication, revue d'erreur, doc courte | `-Level 1 -Mission … -File <chemins>` |
| 2 | Une tâche : explorer, modifier, tester | `-Level 2 -Scope <chemins> -Verify "<cmd>"` |
| 3 | Chantier multi-fichiers | `-Level 3` (budget 60 min) |
| 4 | Passes analyse → impl → relecture | enchaîner des appels, un seul écrivain à la fois |
| 5 | Escalade Claude (exception) | `-LogClaude "<raison>"` puis intervention minimale |

Statistiques : `scripts/local/delegate.ps1 -Stats`.

## À déléguer d'abord

Exploration, lecture de code, recherche de bugs, refactoring, implémentation, tests, perf, erreurs de compilation, CI, relecture, doc, nettoyage, dépendances, messages de commit et de PR.
Un sous-agent Claude (`Agent`, `os-*`) n'est pas local : il consomme le quota. Ne l'utiliser que si l'utilisateur le demande ou après échec local documenté.

## Escalade (niveau 5) seulement si

Plusieurs tentatives locales échouées, arbitrage d'architecture majeur, impact critique multi-composants, résultat local manifestement faux, opération sensible (GitHub/Notion/authentification), tâche hors de portée locale.
Avant : réduire le périmètre, découper, enrichir le contexte. Consigner la raison.

## Contrôle (ne jamais croire « SUCCESS » sur parole)

Le statut est calculé par le script : fichiers réellement modifiés, périmètre respecté, `-Verify` à code 0, fournisseur `ollama` et coût 0.
Claude vérifie ensuite : `git diff --stat`, un test ciblé, les invariants de CLAUDE.md. Lire des extraits, jamais des fichiers entiers ni des logs complets.

## Interdits

Pas de secret dans les prompts locaux. Pas de commit/push/merge/force-push/suppression par l'agent. Pas de deux écrivains simultanés sur le même arbre. Respecter les worktrees, branches et réservations Notion en cours ; le périmètre `-Scope` est obligatoire dès que d'autres chantiers modifient l'arbre.
Claude garde la main sur GitHub (PR) et Notion (authentification).

## Place mesurée du modèle local (essais du 2026-11-14, `openstitch-agent` 9B)

| Usage | Résultat | Consigne |
|---|---|---|
| Lire un extrait et répondre, `-Level 1 -Quote -File chemin:début-fin` | juste, citations vérifiées par le script, 2-3 s | par défaut pour toute lecture de doc ou de code |
| Message de commit ou de PR depuis un diff (`-File` sur le diff) | fidèle au diff, 3 s | par défaut |
| Tri d'erreurs de compilation sans le code | réponse générique | joindre la plage de code en cause, sinon inutile |
| Écrire un test C++ (`-Level 2`) | 0 fichier modifié en 2 essais (114 s) | un seul essai, puis écriture directe consignée par `-LogClaude` |

Règles d'appel du niveau 1 : toujours passer les fichiers (un fichier introuvable bloque l'appel) ;
préférer une plage `chemin:début-fin` à un fichier entier ; `-Quote` dès que la réponse sert à
décider ; un statut `PARTIAL` signifie qu'une citation est introuvable : ne pas croire ce point.
`-Stats` donne les tokens réellement traités en local.

## Limites connues

qwen3.5:9b appelle mal les outils : le script relance une fois avec la cause locale. Contexte de départ 16-32 K ; ne l'augmenter qu'après mesure.
Contournement ponctuel du hook : `LOCAL_FIRST_BYPASS=1` (à justifier dans le message).
