# Audit de l'intégration Claude Code – OpenStitch MCP (28 octobre 2026)

## Objectif cible

L'utilisateur doit pouvoir commander le projet ACTUELLEMENT OUVERT dans OpenStitch
à l'aide de Claude Code, avec un chat dans le logiciel, des actions observables,
un aperçu avant chaque mutation, une approbation, et un Ctrl+Z fiable.

## Périmètre regroupé dans une seule PR

La PR #188 contient la série cumulative des PR #180–#187 (ADR, pont de document vivant,
MCP local, dock, fournisseur API séparé, planification satin, packaging, lanceur),
les corrections du client MCP autonome et le mode chat Claude Code via CLI headless.

Fonctionnalités proposées :
- Chat Claude Code dans le dock via QProcess et sortie JSON, sans nouvelle clé API.
- Mode terminal externe conservé pour l'usage interactif complet de Claude Code.
- Workspace isolé par PID, configuration MCP --live --pid N, guide et skills.
- Outils live : contexte, ciblage par couleur de fil, plan satin comparatif,
  aperçu avant/après, projet appliqué exclusivement après approbation.
- Prévention des modifications perdues, isolation des checkpoints, validation
  du protocole et limite de requête sur le serveur autonome.
- Isolation de l'historique lorsque l'utilisateur change de document et
  opérations atomiques / révisions / annulation native.
- Distinction explicite entre CLI Claude Code et API Anthropic optionnelle.

## Bloqueurs P0 avant validation

1. Compiler Windows Debug et Release, linux-core, exécuter Catch2 / QTest.
2. Contrôler le format clang-format 18 et la garde de sécurité.
3. Exécuter une session réelle Claude Code dans le dock à partir d'une
   installation propre, vérifier l'authentification et l'accord MCP.
4. Tester la compatibilité des wrappers Windows .cmd et des exécutables natifs.
5. Demander une modification satin sur un OSP non sauvegardé : aperçu,
   approbation, changement visuel immédiat, annulation sans perte.
6. Tester le rejet, la fermeture et la modification concurrente (E_STALE_REVISION).
7. Ouvrir deux instances Desktop et vérifier que chacune cible seulement son PID.
8. Tester interruption, reprise de session et limite de tours.

## Couverture connue et limites honnêtes

- La CLI Claude Code doit être installée et authentifiée par l'utilisateur.
- Le mode intégré utilise la CLI en mode non interactif, pas un terminal
  virtuel complet (ConPTY/VT). Les fonctions purement interactives de la CLI
  restent disponibles via le lancement terminal externe.
- Le mode API Anthropic du dock est indépendant et peut être facturé séparément.
- OpenStitch ne maîtrise pas le comportement des modèles externes ; les garde-fous
  de validation et la révision du projet sont essentiels.
- MCP autonome : project sweep, debug-dir, stats d'un DST externe et progression
  fine des longues opérations restent des travaux complémentaires.
- Les tests ajoutés ne sont pas une preuve de recette sans vraie CLI.
- Le format OSP/DST et les algorithmes de broderie ne sont pas modifiés par cette PR.

Roadmap Hatch : aucune entrée métier concernée ; travail centré sur l'intégration
agent, les contrats et les parcours UI.
