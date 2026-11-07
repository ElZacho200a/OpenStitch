---
name: openstitch-notion
description: Coordonner OpenStitch avec Notion en source produit, réserver des lots entre agents, éviter les collisions, réconcilier Notion et GitHub et ne valider qu'avec des preuves. Obligatoire pour « continue OpenStitch », « prochaine tâche », « autonomie », « audit d'avancement », « coordination agents », « valide la roadmap » et avant de déléguer plusieurs tâches.
---

# Notion, tableau blanc partagé d'OpenStitch

**Lire d'abord** `docs/agents/notion-protocol.md`, puis le HQ Notion et la gouvernance. Le protocole détaillé y vit pour éviter des instructions divergentes entre agents.

## Commandes de session
1. Charger les Skills Notion activés avec `notion-get-enabled-skills` ou l'outil équivalent, si disponible ; notamment « Continuer OpenStitch à partir de Notion » et « Auditer et valider le travail des agents OpenStitch ».
2. Lire les tâches déjà « En cours », « En PR », « À valider », leurs baux et les PR GitHub ; priorité aux lots engagés et à la migration **Studio**.
3. Prendre une tâche existante avec code stable et critères ; comparer périmètres et dépendances, réserver dans la fiche Notion, puis relire. Notion ne fournit pas de lock atomique : une concurrence ambiguë impose arbitrage, pas poursuite aveugle.
4. Développer dans une branche/worktree dédiée, chemins disjoints, fichiers transverses réservés au lead. Émettre heartbeat seulement à un jalon réel ; reporter résultat, SHA, CI, tests, captures, risques.
5. Laisser la validation à un relecteur distinct et ne marquer « Validé » qu'après code sur `main`, preuves, UI Studio réelle / essais physiques quand nécessaires. Une fusion seule conduit à « À valider ».
6. Donner un résumé de reprise en ≤ 15 lignes. En cas de Notion indisponible : pas de nouvelle prise autonome, rapporter le blocage.

## Liens stables
- [HQ](https://app.notion.com/p/411bb207e2da818686c3f8d6db4991d5)
- [Gouvernance](https://app.notion.com/p/411bb207e2da819a8133e9bf9dedc634)
- [Skill Continuer](https://app.notion.com/p/411bb207e2da814bab8fed113d06b841)
- [Skill Validation](https://app.notion.com/p/411bb207e2da8127a253f87d1f1c0f4e)

**N'invente jamais un état de réservation, une CI verte ou une recette.**
