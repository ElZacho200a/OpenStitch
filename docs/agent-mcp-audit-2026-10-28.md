# Audit MCP OpenStitch — 2026-10-28

Base inspectée : main, libs/mcp, apps/mcp, tests/unit/mcp, docs/source/mcp.md, CLI project_ops, PR Companion #182 et #186.

## État au départ

29 outils MCP : numérisation, inspection, modification via project_ops/ICommand, palette, rendu PNG, analyse, import/remplacement sémantique, comparaison satin, checkpoints, export machine. Ressources de guide et prompt digitize sur stdio JSON-RPC.

## Défaillances confirmées, corrigées dans cette PR

| Gravité | Défaut | Correctif |
| --- | --- | --- |
| Critique | project_new/open/digitize remplaçaient le projet modifié sans confirmation | E_UNSAVED_CHANGES ; sauvegarde ou discard_changes=true |
| Haute | Checkpoints conservés après changement de projet | Effacement sur new/open/digitize/close |
| Haute | JSON-RPC sans version/identifiant valide accepté | Validation enveloppe, id et params |
| Haute | Version de protocole inconnue répercutée au client | Négociation bornée aux versions supportées |
| Haute | Requêtes non bornées | 8 Mio max et drainage de ligne côté stdio |
| Moyenne | Champs inconnus ou types/enum incohérents ignorés par les outils | Validation top-level ; opérations métier restent dans project_ops |
| Moyenne | Pas de statut léger ni de fermeture sûre | project_status, project_close |
| Moyenne | Agents sans indications lecture/écriture | annotations dans tools/list |
| Moyenne | Guide ne décrit pas le risque de perte d'état | Guide MCP actualisé |

## Non couvert — chantiers à traiter sans duplication

- Synchronisation et approbation des mutations du projet **ouvert dans le desktop** : pont live de la pile Companion, PR #182 et #186, non fusionné sur main lors de l'audit.
- Parité CLI partielle : project sweep, --debug-dir, statistiques d'un DST importé.
- Annulation/progression réelle d'un appel long : moteur synchrone, notifications cancelled non exploitées.
- Concurrence : une session par processus, pas de gestion multi-clients ou verrouillage inter-processus.
- Validation profonde de settings / operations assurée par project_ops plutôt qu'un JSON Schema externe complet.

## Compatibilité, tests et limites

Version MCP 1.2.0 ; OSP/DST inchangés. Les anciens agents doivent appeler project_save ou confirmer discard_changes=true avant remplacement d'un projet dirty. Le catalogue tools/list est étendu de deux outils.

Tests Catch2 ajoutés pour JSON-RPC invalide, négociation, limite, schémas d'arguments, annotations, sessions sales et isolation des checkpoints. **Aucune compilation locale possible depuis le connecteur GitHub :** compilation, clang-format 18 et ctest sont à vérifier par CI avant fusion.

Roadmap de parité Hatch : aucune entrée concernée ; amélioration strictement MCP.
