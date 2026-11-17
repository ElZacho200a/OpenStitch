# R02 — Architecture de calcul proposée

## Principe

Le GPU n'est un accélérateur que si une donnée y reste résidente sur plusieurs étapes et que le
résultat est exact ou purement visuel. Les mesures R01 et R02 montrent qu'aucune étape actuelle
du moteur ne remplit ces conditions. L'architecture retenue garde donc **un seul chemin de
calcul, sur CPU, déterministe**. Elle prépare les points d'insertion d'un backend GPU sans en
dépendre.

```
apps/desktop (Qt)                      libs/* (sans Qt)
┌──────────────────────────┐          ┌─────────────────────────────────────────┐
│ Scene graph RHI/D3D11    │  affiche  │ effective_sequence(project)             │
│ (points, réaliste, cadre)│◀──────────│  ├─ génération (parallel_for par unité) │
│                          │           │  ├─ finitions (grille par bandes, plans │
│ BusyIndicator::run       │ instantané│  │   en parallèle, assemblage séquentiel)│
│ (fil de travail)         │──────────▶│  └─ tirage prédit (parallel_for/point) │
└──────────────────────────┘          │ simulate_fabric_pull (parallel_for)      │
                                      │ render_threads (aperçu CLI/MCP)         │
                                      └─────────────────────────────────────────┘
```

## Règles de conception appliquées par R02

1. **Algorithme avant parallélisme.** Réduire le travail (intervalle utile au lieu de la boîte
   englobante, rejet exact, grandeurs précalculées), puis paralléliser ce qui reste.
2. **Trois phases déterministes.** (a) lister les unités de travail en séquence, (b) calculer
   en parallèle dans des cases disjointes à partir de données constantes, (c) assembler en
   séquence. Les ponts cachés suivent ce modèle : pré-calcul des plans, puis boucle de décision
   d'origine inchangée qui consomme les plans (ou calcule elle-même un plan manquant).
3. **Exactitude par construction.** Toute réécriture garde les expressions flottantes de
   l'original. Un filtre ou un rejet anticipé n'écarte que ce que le test exact écarterait,
   avec une marge prouvée, et le test d'origine tranche dans la zone ambiguë. Un test compare
   le résultat bit à bit à une copie de l'implémentation d'origine.
4. **Pas de surcoût en monofil.** Le découpage en bandes dépend du nombre de fils ; à un fil, le
   code suit le chemin séquentiel d'origine. Le résultat ne dépend jamais du découpage.

## Contrat d'un futur backend GPU (non implémenté)

Repris de l'[ADR 0006](../../adr/0006-compute-moteur-experimental.md), précisé par R02 :

- Interface interne dans le cœur, par exemple `compute::IMaskBackend` : entrée immuable
  (dimensions, octets, révision), sortie hôte typée, diagnostics. Implémentation CPU de
  référence toujours présente et utilisée par défaut.
- Adaptateur GPU dans une bibliothèque séparée et facultative (jamais dans `libs/core`), choisi
  à l'exécution. Le CLI et `linux-core` n'en dépendent pas.
- **Politique de dispatch mesurée** : le GPU n'est choisi qu'au-dessus d'un seuil de travail
  mesuré sur la machine (calibration au premier usage, conservée par adaptateur et version de
  pilote). Sous le seuil, ou en cas d'échec, de perte de périphérique ou d'adaptateur absent,
  repli CPU signalé.
- **Résidence** : l'image source et les étiquettes envoyées une fois par révision. Les étapes
  chaînées sur GPU ne relisent que le résultat final.
- **Exactitude** : uniquement des noyaux entiers ou des sorties visuelles. Test de parité bit à
  bit contre la référence CPU sur le corpus de chaque version de pilote qualifiée.

## Améliorations d'architecture CPU à suivre

Voir la [roadmap](roadmap.md) : pool de fils persistant, aperçu CPU par bandes dynamiques,
étiquetage unique de la segmentation, invalidation fine des finitions et du tirage après une
édition locale.
