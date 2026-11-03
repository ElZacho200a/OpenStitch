# Efficacité en tokens de Claude Code sur OpenStitch — audit du 2026-10-20

*Périmètre : configuration Claude Code du dépôt uniquement. Aucun code OpenStitch modifié.*
Méthode courte : `.claude/skills/openstitch-token-efficiency/SKILL.md`.

## 1. Ce qui est mesuré, ce qui est supposé

**Mesuré** (transcripts locaux, `scripts/session-usage.py`, 3 sessions récentes encore présentes ;
`rtk gain`) :

| Mesure | Valeur |
|---|---|
| Préfixe fixe au 1er appel (système + outils + MCP + CLAUDE.md + liste de skills) | 78 à 85 k tokens |
| Contexte par requête, session longue (276 requêtes) | médiane 346 k, fin 613 k |
| Contexte par requête, session moyenne (115 req.) | médiane 231 k |
| Cache lu / écrit sur la session longue | 95,5 M / 0,96 M tokens (≈ 99 % lus du cache) ; sortie 276 k |
| Résultats d'outils les plus lourds (3 sessions) | Read 0,37 Mcar, Bash 0,31 Mcar, Grep 0,10 Mcar |
| RTK (9 401 commandes, global) | 58,1 % d'entrée économisée (4,5 M), surtout `rtk read` (2,6 M) et `rtk grep` (1,3 M) |
| Sous-agents dans ces 3 sessions | 0 |

Conclusion mesurée : le coût dominant n'est pas le préfixe mais **le contexte qui grossit** (chaque
requête relit tout, au tarif du cache). Une session de 276 requêtes à 346 k de médiane pèse ≈ 95 M
tokens relus, soit ≈ 15 fois une session courte de 30 requêtes. Les leviers : finir en moins de
requêtes, ne pas faire entrer de contenu volumineux, couper les sessions.

**Supposé / non mesurable ici** : coût réel des sous-agents (aucun dans l'échantillon, les
transcripts plus anciens ont été purgés par `cleanupPeriodDays`) ; poids exact des MCP
(`/mcp`, `/context` sont des commandes interactives, inaccessibles à l'agent) ; part du skill
digitize réellement chargée (il ne l'est qu'à l'invocation).

## 2. Constats et corrections

| # | Constat (preuve) | Importance | Correction |
|---|---|---|---|
| 1 | `.claudeignore` sans effet : la doc officielle (permissions) le dit explicitement ; c'était un gabarit Liza ; | moyenne (fausse sécurité) | supprimé ; règles `Read` deny dans `.claude/settings.json` (logs, SVG de debug, PDF de doc, venv sam-worker) |
| 2 | CLAUDE.md 12,1 Ko (≈ 3,5 k tokens, relus à chaque requête) dont tableau des modules (doublon de `module-reference.md`) et liste des skills (doublon des descriptions) ; ordonnait de lire `satin.md` (350 Ko) et `satin-squelette.md` avant toute modif satin | haute (consigne incitant à ~100 k tokens de lecture) | CLAUDE.md 5,5 Ko : invariants, build, recherche ciblée, modes de session ; GUARDRAILS.md : « sections pertinentes par plages », audit global seul = lecture complète |
| 3 | `openstitch-digitize` 36,6 Ko chargé d'un bloc, dont 25 Ko de journal d'essais | haute à l'invocation | SKILL.md 9,3 Ko (opérationnel) + `references/{parametres,diagnostic,journal-essais}.md` à la demande ; contenu technique conservé intégralement |
| 4 | `openstitch-multi-agent` : « préférer au séquentiel dès que le découpage est propre » (réflexe) ; aucun choix de modèle | haute | matrice de décision bénéfice/coût, prompts minimaux, validation centralisée, agents `os-explorer` (haiku), `os-dev` (sonnet, worktree), `os-architect` (opus) |
| 5 | Aucun modèle par défaut pour les sous-agents ; la session peut passer en Opus et les agents héritent | moyenne | `CLAUDE_CODE_SUBAGENT_MODEL=sonnet` dans `settings.json` (bloc `env`). Ordre officiel (v2.1.281) : paramètre d'appel > frontmatter > variable > session ; Opus/Haiku restent donc choisissables |
| 6 | RTK : `rtk hook claude` ne s'applique qu'à l'outil Bash ; les builds passent par PowerShell ; RTK n'a pas de filtre cmake/ctest/MSBuild (`rtk --help`) | haute pour les builds | `scripts/qrun.ps1` : statut + bilan sur succès, erreurs dédupliquées + journal complet en fichier sur échec, code de sortie conservé (testé : succès, échec) |
| 7 | `rg` absent du PATH Bash (message RTK « Binary 'rg' not found ») | moyenne | ripgrep 15.2.0 installé via winget (2026-10-20, PATH après redémarrage du shell) ; instructions par défaut sur l'outil Grep |
| 8 | `openstitch-commit` + `openstitch-roadmap` : « enchaîner sur le lot suivant sans attendre » = boucle illimitée par défaut | haute | trois modes (ciblée par défaut / autonomie continue sur demande / audit) ; trigger « tâche terminée → choisir la suivante » retiré de la description de la roadmap |
| 9 | Pas de procédure de reprise ; sessions jusqu'à 613 k | haute | point de reprise ≤ 15 lignes dans le skill `token-efficiency` §4 ; compaction auto laissée active |

Non modifié, volontairement : hooks de sécurité (`git-guard`, `rtk-guard`, `worktree-path-guard`,
`arch-plan-consistency-guard`) ; `enableAllProjectMcpServers` (mesh-mcp utile pour `search_docs`) ;
liste d'autorisations héritée de Liza (skills inexistants, inoffensifs, hors périmètre) ;
`disable-model-invocation` (aucun skill n'est purement manuel ; les descriptions déjà ciblées
suffisent — la liste de skills ne coûte que ses descriptions).

Mise à jour : `rtk proxy` est désormais autorisé dans `rtk-guard.sh` (la lecture des tee RTK reste bloquée). Les journaux de `qrun.ps1` sont des fichiers propres
(lisibles) et n'entrent pas dans ce conflit.

## 3. Compromis et revue critique

- Les nouvelles règles ajoutent ≈ 6 Ko de skills (chargés à la demande) pour 6,8 Ko retirés du
  CLAUDE.md (chargé en permanence) : bilan net positif sur le préfixe, et neutre ailleurs.
- Risque : « lecture par plages » peut manquer un contexte. Atténué : l'exception (§10 du skill)
  autorise la lecture complète pour un audit global, et GUARDRAILS exige toujours la lecture des
  sections pertinentes.
- `qrun.ps1` masque la sortie de succès : la ligne de bilan dépend de motifs (`tests passed`,
  `Build succeeded`…). Si un format change, le journal complet reste sur disque.
- Haiku pour `os-explorer` : suffisant pour localiser, insuffisant pour juger ; sa consigne le
  limite à des réponses de localisation.
- `CLAUDE_CODE_SUBAGENT_MODEL=sonnet` : sur `main` en Opus, les agents génériques passent en Sonnet ;
  utiliser le paramètre `model` d'appel pour exiger Opus.
- Multi-agents et autonomie préservés : worktrees isolés, intégration séquentielle par le chef de
  lot, modes explicites.

## 4. Protocole de mesure reproductible

1. Choisir une tâche type : (a) trouver un symbole et ses dépendances, (b) bug localisé, (c) nouveau
   paramètre (`openstitch-stitch-param`), (d) changement multi-modules, (e) audit.
2. La faire dans une session dédiée ; noter la date.
3. `python scripts/session-usage.py --since AAAA-MM-JJ ~/.claude/projects/<dossier>/` (un dossier par
   worktree) : requêtes, contexte, cache, appels d'outils, agents par modèle, lectures sans `limit`.
4. En session : `/usage` (cache, répartition par skill/agent/MCP), `/context`.
5. Comparer sur : requêtes jusqu'au vert, contexte médian, Mcar de résultats d'outils, nombre
   d'agents, résultat des tests. Réf. avant : tableau §1 (CLAUDE.md 12,1 Ko, digitize 36,6 Ko).
6. Conserver les transcripts avant la purge (`cleanupPeriodDays`) si une comparaison est prévue.

Limite : un avant/après chiffré sur tâches identiques n'existe pas encore (aucune tâche rejouée,
par choix : un benchmark coûterait plus que l'économie visée).
