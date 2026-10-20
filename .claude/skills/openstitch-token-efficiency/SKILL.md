---
name: openstitch-token-efficiency
description: Méthode pour dépenser peu de tokens sans perdre en qualité sur OpenStitch (recherche ciblée avant lecture, choix du modèle, délégation rentable, sorties de commandes courtes, longues sessions et reprise, anti-boucles, mesure). À utiliser au début d'un chantier long ou multi-fichiers, quand une session grossit, quand l'utilisateur parle de tokens, de quota, de coût ou de contexte, ou avant de lancer des sous-agents.
---

# Efficacité en tokens (sans rogner la qualité)

Ce qui coûte, mesuré sur les sessions de ce dépôt : le préfixe fixe (~80 k tokens au premier appel)
est relu à chaque requête, puis le **contexte grossit** (médiane 346 k, jusqu'à 613 k sur une
session de 276 requêtes) et chaque requête paie tout le contexte. Les économies viennent donc de
(a) ne pas faire entrer de contenu inutile, (b) finir les tâches en moins de requêtes, (c) couper
une session avant qu'elle ne devienne lourde. Jamais en testant moins, en réfléchissant moins ou
en bâclant la vérification.

## 1. Chercher avant de lire

1. Symbole ou texte : Grep (type/glob), `analyze_impact("<symbole>")` pour appelants et CMake.
2. Doc : `search_docs` (voir `openstitch-docs`), sinon Grep sur les titres puis lecture par plage.
3. Lire avec `offset`/`limit` autour du résultat. Fichier > ~15 Ko : jamais en entier sans raison
   écrite (audit global d'un sous-système). `satin.md` ≈ 350 Ko, `moteur-de-points.md` ≈ 58 Ko.
4. Une recherche se fait une fois : noter le résultat (chemin:ligne) et le réutiliser.

## 2. Modèle selon la difficulté

Sonnet par défaut. Haiku : localisation, tri, résumé de sortie. Opus : conception d'un
sous-système, bug résistant à deux hypothèses, arbitrage coûteux à défaire. Effort bas pour les
tâches mécaniques, haut pour la conception. Le changer exige une raison, pas un réflexe.

## 3. Déléguer seulement si rentable

Un sous-agent redémarre à froid (~80 k de préfixe) : il se justifie par du parallélisme réel, par
une sortie volumineuse gardée hors du contexte principal, ou par une unité indépendante de
plus d'~1 h. Sinon, faire soi-même. Politique complète : `openstitch-multi-agent`.

## 4. Sessions longues et reprise

- Une session = un objectif. Objectif terminé et PR ouverte : s'arrêter (mission ciblée).
- Contexte lourd (échanges nombreux, gros fichiers lus, ~200 k+) alors qu'il reste du travail :
  écrire un **point de reprise** et recommander `/clear` (ou laisser la compaction auto, jamais
  désactivée) puis coller le point de reprise :

  ```
  Objectif : …            Branche/commit : …            État git : propre | fichiers modifiés
  Décisions : (3 max, avec la raison)
  Fait / vérifié : (tests lancés + résultat chiffré)
  Reste : (prochaines actions ordonnées, fichiers:lignes)
  Pièges : (ce qui a échoué, à ne pas refaire)
  ```
- Ne pas recopier l'historique ; ne pas relire ce qu'on vient d'écrire (Edit/Write échouent
  bruyamment s'ils ont échoué).

## 5. Sorties de commandes

Builds et tests : `scripts/qrun.ps1` (statut + bilan, journal complet dans
`build/qrun-logs/`). `git status -sb`, `git diff --stat` avant tout diff complet. Pas de
`--verbose`, pas de `ctest -V` sans cible. Ne jamais relancer une commande pour « revoir » :
lire le journal par plage. `rg` n'existe pas dans le shell Bash : outil Grep.

## 6. Réutiliser le déjà-documenté

Avant d'investiguer : `search_docs` sur le symptôme (les bugs satin ont leurs causes racines
écrites), le journal du skill concerné, la roadmap pour un statut. Après : écrire la
conclusion au bon endroit (doc, journal) pour que la prochaine session ne refasse rien.

## 7. Prévenir les boucles

Deux échecs de la même approche → s'arrêter, écrire ce qui a été appris, changer de
méthode (hypothèse différente, `openstitch-bugfix`, `os-architect`) ou demander. Pas de
3e relance identique, pas de « corriger puis re-tester » sans nouvelle hypothèse. Un test
qui bloque 300 s : `--timeout 400` une fois (voir pièges de `openstitch-multi-agent`).

## 8. Valider efficacement (sans supprimer de tests)

Itération : cible + exécutable de test. Livraison : suite complète proportionnée à l'impact
(`openstitch-build-test` §2) — une fois, à la fin, pas après chaque micro-changement. Format sur
les fichiers modifiés. Les garde-fous de la CI restent tous exécutés.

## 9. Mesurer

`python scripts/session-usage.py --since AAAA-MM-JJ ~/.claude/projects/<dossier-projet>/`
(lecture seule des transcripts) : tokens par session, contexte moyen, appels d'outils, agents
par modèle, lectures sans `limit`, volume de résultats par outil. Comparer **une même sorte de
tâche** avant/après sur : requêtes, contexte médian, sorties d'outils, nombre d'agents, itérations
jusqu'au vert. Dans Claude Code : `/usage` (tokens, cache, répartition skills/agents/MCP),
`/context` (poids du préfixe). Détail : `docs/token-efficiency.md`.

## 10. Exceptions

Mission difficile (conception, bug rebelle, audit global) : le coût est légitime. Lire en entier un
document, lancer Opus, ou paralléliser largement est permis si c'est écrit en une ligne dans le
message (« lecture complète de X car audit global »). Ces règles servent la qualité, pas
l'inverse : en cas de conflit, la qualité et la sécurité (Tier 0/1 de `GUARDRAILS.md`) gagnent.
