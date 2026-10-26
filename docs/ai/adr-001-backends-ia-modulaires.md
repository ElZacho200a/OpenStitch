# ADR-001 — Backends de vision IA modulaires, isolés et facultatifs

*Statut : proposée (PR-00). Date : 2026-10-26.*

## Contexte

Le seul chemin IA actuel est SAM 2.1 en mode « génération automatique de masques », via un worker Python dont le client est lié à Qt (voir [README](README.md), F1/F2). Le domaine évolue vite (SAM 3, HQ-SAM 2, MobileSAM, Florence-2, Grounding DINO) et aucun résultat publié ne remplace une mesure sur des illustrations brodables.

## Décision

1. **Processus séparé par backend**, protocole **JSON Lines versionné** indépendant du modèle : `capabilities`, `predict`, `refine`, `cancel`, `health`. stdout réservé au protocole, logs sur stderr.
2. **Un contrat, plusieurs moteurs.** Chaque réponse porte `backend_id`, `backend_revision`, `model_hash` (SHA-256 du checkpoint), les transformations d'image appliquées (échelle, recadrage) et des masques normalisés (coordonnées de l'image source). Une version de protocole incompatible est un refus explicite, jamais une dégradation silencieuse.
3. **Client sans Qt** dans `libs/ai_segmentation`; le desktop n'en est qu'un adaptateur. Le CLI et le MCP peuvent alors piloter les mêmes backends.
4. **SAM 2.1 est le backend de référence et ne change pas de comportement par défaut** ; son adaptation au contrat est un refactoring protégé par les tests du worker existants.
5. **Aucun backend n'est adopté sans benchmark** ([plan-benchmark.md](plan-benchmark.md)) : critères d'adoption mesurés, jamais supposés.
6. **Facultatif et hors ligne.** Pas de dépendance de build, pas de poids dans le dépôt, les binaires ou la CI de base, pas de transmission cloud sans consentement explicite. Repli : segmentation CIELAB.
7. **L'IA ne génère jamais de points.** Elle fournit masques et relations ; mutation du document uniquement par `ICommand` (opérations `semantic_*`), coordonnées en µm entiers aux frontières.

## Alternatives écartées

- *Lier PyTorch/ONNX Runtime au cœur C++* : viole l'isolation, alourdit CI et distribution.
- *Étendre le protocole SAM 2 existant champ par champ* : couple le contrat à un modèle et masque les incompatibilités.
- *Choisir SAM 3 d'emblée* : licence « SAM License » (non Apache-2.0), poids à accès conditionnel ; à évaluer après validation juridique et benchmark (PR-10).

## Conséquences

- Un worker de plus à maintenir par famille de modèles, mais un coût de remplacement faible.
- Le catalogue de modèles Python/C++ dupliqué devient une découverte dynamique (`capabilities`) ; la liste figée de `model_catalog.cpp` devient un repli.
- Reste ouvert : packaging sans WSL (HP-AUTO-011), installation guidée (HP-DIST-006).
