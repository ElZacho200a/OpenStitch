# Notion ↔ GitHub : protocole de coordination des agents OpenStitch

Version : 2026-11-07. **Notion est le tableau blanc et la source de vérité produit ; GitHub reste la source de vérité du code.** Ce protocole complète, sans remplacer, `CLAUDE.md` et `GUARDRAILS.md`.

- [HQ](https://app.notion.com/p/411bb207e2da818686c3f8d6db4991d5)
- [Gouvernance](https://app.notion.com/p/411bb207e2da819a8133e9bf9dedc634)
- [Skill Notion : continuer](https://app.notion.com/p/411bb207e2da814bab8fed113d06b841)
- [Skill Notion : auditer/valider](https://app.notion.com/p/411bb207e2da8127a253f87d1f1c0f4e)

## 1. Unités de travail et sources

Ne pas reconstruire un deuxième backlog :
- **Roadmap — Chantiers** : lots `E##` et grandes dépendances.
- **Backlog — Fonctionnalités** : entrées `F##` / `F##-##` ; fiche canonique quand le travail porte sur une fonctionnalité.
- **Parité — 44 familles** : couverture de capacités, pas une seconde file de tâches concurrente.
- **Validation — Recettes** : protocoles et preuves `T##`, exigences transverses.
- **Risques & dépendances** : risques `R##`.
- **GitHub** : PR, commits, CI, scripts, captures et artefacts ; preuve technique primaire.

Si un lot et une fonctionnalité couvrent la même modification, la *fiche d'implémentation la plus fine* porte la réservation ; la roadmap y renvoie. Une PR peut couvrir plusieurs codes, mais chaque code garde sa fiche et sa preuve. Chercher dans les bases avant création.

Champs de réservation ajoutés aux bases Roadmap et Backlog : `Agent responsable`, `Bail jusqu'au`, `Périmètre réservé`, `Dernière vérification`, `Preuves et recette`, `Relecteur`. Utiliser le corps de la fiche pour branche/HEAD, dépendances et prochaine action.

## 2. Prise de chantier (claim)

1. Charger les skills Notion activés, lire le HQ et les conventions du dépôt.
2. Rechercher les travaux **En cours / En PR / À valider**, leurs agents, leurs baux et les PR GitHub ouvertes. Prioriser terminer/réparer/valider l'existant. En particulier, suivre les dépendances déclarées #282 → #283 avant nouveau lot Studio.
3. Vérifier critères d'acceptation, priorité, dépendances et périmètre de fichiers. Si le périmètre se chevauche avec un agent actif, découper autrement ou demander l'arbitrage du lead.
4. Déclarer dans la fiche : `Agent responsable` (identifiant de session non ambigu), `Bail jusqu'au` (recommandation : 2 h), `Périmètre réservé` (chemins explicites), branche/worktree, commit initial, tests prévus, prochaine action, puis mettre « En cours » uniquement si le travail démarre réellement.
5. **Relire immédiatement la fiche** et les autres baux pertinents. Si propriétaire ou périmètre conflictuel, interrompre la modification et remonter le conflit.

**Limite essentielle :** Notion n'offre pas, avec ces propriétés, de compare-and-swap / verrou atomique. Deux agents écrivant au même instant peuvent tous deux croire détenir un bail. Pour une véritable exécution simultanée sans conflit garanti, exiger un orchestrateur unique, ou implémenter ultérieurement un arbitre transactionnel. Ne jamais présenter le protocole souple comme un système de lock strict.

## 3. Développement et coordination

- Un worktree/branche par lot. Ne pas éditer les mêmes fichiers entre agents.
- Les fichiers de convergence (`CMakeLists.txt`, contrats partagés, thème global, roadmap, `CLAUDE.md`) appartiennent au lead/intégrateur ; les sous-agents transmettent leurs demandes.
- Chaque sous-agent reçoit des fichiers bornés, les tests à produire et un critère de sortie. Pour l'arbitrage coût : exploration simple avec agent économique, développement borné avec agent généraliste, architecture complexe seulement si nécessaire.
- Heartbeat à chaque jalon ou environ 30 min pendant une session longue : revérifier la réservation et renouveler le bail si le travail continue. Ne pas mettre une présence fictive.
- Une PR empilée annonce le parent, la condition de rebase/retarget et l'ordre de fusion. Aucune intégration silencieuse de conflits.
- Si la tâche devient bloquée, renseigner *cause, dépendance, déblocage attendu et point de reprise* ; libérer le bail si aucune progression possible.

## 4. États et preuves requises

| Statut | Condition |
| --- | --- |
| À auditer | Besoin non établi / non reproduit |
| À faire | Manque confirmé, acceptation mesurable |
| En cours | Bail actif, développement réellement démarré |
| En PR | PR ouverte, lien documenté (CI peut être en attente ou rouge) |
| Bloqué | Motif, dépendance et action explicites |
| À valider | Code déjà sur `main`, recette ou revue encore incomplète |
| Validé | Commit `main`, critères atteints, CI pertinente vérifiée, preuve Studio/CLI/physique selon le cas, revue distincte |

Le développeur ne valide pas sa propre livraison. La présence d'un backend ne valide pas l'UI Studio ; une capture de maquette n'est pas une capture Qt réelle ; une prévisualisation n'est pas une broderie physique. Les tests indiquent le SHA, l'environnement et le résultat (pass/fail/non exécuté). Une PR mergée sans recette va vers « À valider ». Un « Validé » qui régresse peut être rouvert avec preuve datée.

## 5. Rapport standard (à recopier dans la fiche, ≤ 15 lignes)

```text
Tâche : <code> | Agent : <id/session> | Bail : <UTC ISO 8601>
Objectif / critère : <résultat observable>
Branche / worktree / HEAD : <liens ou chemins>
Périmètre : <fichiers réservés>
Faits démontrés : <résultats vérifiés, pas d'opinions>
Tests : <commande, SHA, résultat / non lancé>
CI : <run GitHub, SHA, état ou « non vérifié »>
Studio : <capture Qt réelle/recette ou « non vérifié »>
Textile : <essai physique ou « hors périmètre / non vérifié »>
PR : <lien, base, dépendances et conflits>
Décisions / risques : <explicites>
Blocage et cause : <sinon aucun>
Prochaine action : <action concrète>
Relecteur / verdict : <identité, date, preuve ou pending>
```

## 6. Reconciliation régulière

À chaque démarrage, avant PR, après merge et en fin de session : contrôler les statuts, les baux et les liens. Au moins lors de la revue quotidienne de l'équipe, comparer GitHub et Notion :
- PR ouverte mais sans fiche, branche parent inconnue, conflit ou CI rouge ;
- fiche « En PR » alors que la PR a été fusionnée/fermée ;
- fiche « Validé » sans preuve sur `main` ou sans recette ;
- bail expiré (enquêter avant de reprendre), chevauchements de fichiers ;
- divergences produit/technique et décisions non enregistrées.

Corriger chaque entrée avec date de vérification, provenance et le constat exact ; ne pas inférer une réussite. **Ce document décrit une cadence, pas un service périodique en cours d'exécution** : un scheduler/automatisation doit être configuré séparément.

## 7. Conditions d'échec

Sans accès Notion, ne pas prétendre réserver ni synchroniser ; une demande ciblée peut être traitée localement avec limitations annoncées, mais une mission autonome « continue selon Notion » doit attendre le rétablissement de la coordination. Sans accès GitHub, ne pas valider la CI/PR. En cas de concurrence, un seul orchestrateur arbitre. Ne jamais fusionner malgré une CI rouge ou une dépendance non satisfaite.
