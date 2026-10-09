# Auto-satin par squelette — prochaines étapes (plan 2026-10)

Hypothèse : le propriétaire fait ses essais manuels (bureau + machine) en parallèle.
Rien ici n'est fusionné sur `main` ; chaque lot = commits testés sur la branche de recherche.

## Architecture cible (état après migration)

```
document::AutoSatinParams  ──►  stitch_generation::generate_auto_satin
   (source-derived, guides)        │  chaîne les colonnes, finish_satin_stations
                                   ▼
                   auto_satin::generate_skeleton_satin
          (skeleton → Axis → sample_axis → chord → cellules → diagnostics)
                                   ▼
        desktop : aperçu / refus motivés / guides / inspecteur (aucune logique métier)
```

Dépendances : `apps/* → libs/* → core`, cœur sans Qt. `autodigitize` n'utilise que les
primitives de squelette (pas le moteur satin).

## Lot 0 — Retours des essais manuels (bloquant pour la suite)
- Grille de relevé : forme, espacement, guides, défaut observé (trous de couverture,
  fil double, virages, entrée/sortie, aspect machine), capture + `.osp`.
- Chaque défaut reproductible → test dans le corpus (`shapes.cpp` + seuil) AVANT correctif
  (workflow `openstitch-bugfix`).

## Lot 1 — Qualité du moteur (selon retours)
1. `deep_channel` / bras très larges (≈ 0,65) : découpage en bandes parallèles à l'axe
   ou refus motivé « trop large » (décision à mesurer, pas à deviner).
2. Densité selon la largeur (RD-PAT-013, US5343401A) : `h` modulé par la largeur locale,
   borné ; paramètre opt-in, chaîne complète (modèle → .osp → commande → génération → UI → tests → docs).
3. Perf : cache du squelette par empreinte de contour ; budget < 100 ms sur le corpus.
4. Comparaison d'axe médian exact (Boost.Polygon, BSL-1.0) vs squelette raster : adopter
   seulement si gain de couverture mesuré sur le corpus.

## Lot 2 — Réintégration dans l'auto-numérisation (RD-PAT-002/003/004)
- Banc de comparaison tatami vs satin sur le corpus d'images (couverture, fil double, courtes).
- Option **nommée**, défaut désactivé, éligibilité via `SkeletonSatinDiagnostics`
  (bandes fines uniquement), satin entier ou repli entier, code de raison tracé dans l'UI.
- Option désactivée = zéro différence (DST byte-identiques).

## Lot 3 — Robustesse et dette
- Fuzz/adversarial : contours auto-intersectés, trous minuscules, très grandes formes, µm extrêmes.
- Retirer `geometry::cut_path_set` s'il reste sans usage ; nettoyer `satin.md` (sections mortes)
  en un chapitre ramené au satin manuel.
- Goldens SVG du nouveau moteur via `satin-auto-debug --output-svg` (références diagnostiques).

## Lot 4 — Validation machine et mise en production
- Protocole d'essai machine (formes du corpus, 3 espacements, tissu/fil notés) ; résultats dans `docs/`.
- Revue finale, résumé des écarts, **demande d'autorisation de fusion** au propriétaire (jamais avant).

## Portes d'acceptation (tous lots)
Tests verts Release+Debug ; déterminisme byte-à-byte ; `clang-format` propre ; `.osp` v1–v5 relus ;
docs régénérées sans problème ; statuts roadmap jamais `☑` tant que non livré sur `main`.

## Risques
Squelette raster sensible à la résolution (sous-pixel) ; guides orphelins après édition de forme ;
couverture estimée ≠ rendu réel machine (d'où le Lot 4).
