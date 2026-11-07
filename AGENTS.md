# Instructions multi-agents — OpenStitch

Ce fichier s'applique à Codex et à tout agent qui sait lire le dépôt. Pour Claude Code, lire également `CLAUDE.md` ; les garde-fous de `GUARDRAILS.md` restent prioritaires.

## Démarrage obligatoire des sessions de projet

1. Lire `CLAUDE.md`, `GUARDRAILS.md`, puis [le protocole Notion](docs/agents/notion-protocol.md).
2. Si le client expose des Skills Notion, **récupérer la liste des skills activés à chaque nouvelle session** (par ex. `notion-get-enabled-skills` ou action équivalente), puis charger ceux qui concernent la mission.
3. Pour « continue OpenStitch », « prochaine tâche » ou une autonomie générale : lire [Project HQ](https://app.notion.com/p/411bb207e2da818686c3f8d6db4991d5), [Gouvernance](https://app.notion.com/p/411bb207e2da819a8133e9bf9dedc634), puis les bases Roadmap/Backlog/Validation et l'état des PR ouvertes. Ne jamais choisir une tâche uniquement dans un fichier de roadmap local sans cette réconciliation.
4. Les demandes ciblées restent prioritaires ; ne pas commencer un nouveau chantier autonome si Notion est inaccessible ou si la réservation ne peut pas être vérifiée.
5. Avant de modifier le code, vérifier le code de tâche existant, la PR/branche liée, les dépendances, le propriétaire, l'expiration du bail et les fichiers réservés.

## Règles non négociables

- Notion : source produit (priorités, décisions, ownership, statut de recette). GitHub : source technique (HEAD, code, CI, PR, traces). Toute divergence est signalée, jamais gommée silencieusement.
- Une seule tâche canonique par code stable. **Pas de travail simultané sur les mêmes fichiers.** Worktrees et branches distincts ; les fichiers transverses sont détenus par l'intégrateur.
- Les baux Notion sont *souples* et ne protègent pas contre les courses entre deux sessions : relire après réservation ; sous concurrence réelle, utiliser un seul orchestrateur, sinon stopper et arbitrer.
- « En PR » n'est pas « Validé ». Validation uniquement avec commit fusionné sur `main`, tests vérifiés, preuve pertinente dans **Studio** si interface, revue indépendante et essai physique si une fonctionnalité textile en dépend.
- Jamais de test, capture, CI verte ou broderie physique inventés. Ne pas fusionner une PR rouge, en conflit ou dont le parent n'est pas intégré. Les règles de non-merge du `CLAUDE.md` demeurent applicables aux agents.
- Chaque changement significatif laisse un rapport dans la fiche Notion : résultats observés, liens, risque, prochaine action. En fin de session, résumé de reprise de 15 lignes maximum.

## Skill du dépôt

Lire `.claude/skills/openstitch-notion/SKILL.md` pour le déroulé détaillé (réservation, heartbeats, réconciliation, handoff et gate qualité). Pour les sous-agents, lire aussi `.claude/skills/openstitch-multi-agent/SKILL.md`.

**Priorité technique actuelle :** la transition vers OpenStitch Studio ; ne pas déclarer la parité V1/V2 sur la seule disponibilité d'un service C++.
