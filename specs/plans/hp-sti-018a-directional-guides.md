# HP-STI-018.a — Guides directionnels par squelette (plan)

Statut : plan, aucun code. Entrée : `docs/roadmap-parite-hatch.md` (HP-STI-018.a).
Fiche Notion : F21-01 « Remplissage directionnel et guides ».

## Objectif

Une fonction pure, sans Qt, qui déduit des guides directionnels d'une région :

```cpp
// libs/stitch_generation : directional_guides.hpp
struct DirectionalGuideOptions {
    Micrometers min_edge_length{2000};   // arêtes plus courtes ignorées
    Micrometers resample_step{1000};     // pas de ré-échantillonnage
    std::size_t max_guides{8};
};
struct DirectionalGuideResult {
    std::vector<geometry::Path> guides;   // ouverts, dans la région
    std::string diagnostic;               // vide si succès ; code stable sinon
};
[[nodiscard]] Result<DirectionalGuideResult>
directional_guides_from_region(const geometry::PathSet& region,
                               const DirectionalGuideOptions& options);
```

Réutilise `auto_satin::analyze_region` et `SkeletonGraph` (`SkeletonEdge::centerline`).
Ne réintroduit pas d'auto-satin : seul le squelette est lu.

## Décisions

1. Forme branchée : un guide par branche significative, principal d'abord, maximum 8 (tranché 2026-10).
2. Guides existants : remplacés, y compris le guide par défaut de la conversion tatami (tranché 2026-10).
3. Disponibilité : tatami **et** directionnel. Sur tatami, conversion puis remplacement ; sur directionnel, remplacement (l'option « Ajouter » est reportée).
4. Cercle ou quasi rond : refus propre avec le diagnostic stable `E_ROUND_SHAPE` (erreur codée) ; pas de guides radiaux (le remplissage radial existe déjà).

## Déterminisme et invariants

- Même entrée → même sortie octet pour octet : arêtes triées par (longueur décroissante, id).
- Coordonnées en `Micrometers`/`Vec2um` uniquement.
- Aucun point de guide hors de la région (test d'appartenance après lissage ; point hors région → arête écartée).

## Tests cœur (Catch2, `tests/unit/stitch/test_directional_guides.cpp`)

Rectangle long → un guide central droit ; courbe simple → guide dans l'axe ; T/Y → un guide par branche, ordre documenté ; cercle → `E_ROUND_SHAPE` (erreur codée), aucun guide ; aucun guide hors région ; deux exécutions identiques.

## Livraison

- PR 1 : ce plan. PR 2 : fonction pure et tests. PR 3 : commande annulable dans `libs/commands` puis câblage Studio (une seule `ICommand`, annulation exacte, aucune mutation si échec).
- Hors périmètre : UI Studio tant que PR 2 n'est pas fusionnée.
