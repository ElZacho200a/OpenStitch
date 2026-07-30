# Audit des écarts du moteur de broderie

Public : mainteneur. État **vérifié dans le code** (pas seulement la doc PDF) au
début du plan de livraison « moteur de broderie ». Sert de base au Lot 1
(auto-satin géométrique) et aux lots suivants.

Niveaux employés : *Présent* · *Testé numériquement* · *Validé visuellement*
(SVG) · *Validé simulateur* · *Test physique requis* · *Validé physiquement*.

---

## 1. Fonctionnalités réellement présentes (code + tests)

| Fonction | Où | Niveau |
|---|---|---|
| Point droit / double / triple | `stitch_generation/running_stitch.cpp` (`run_stitch`, `apply_repeat_mode`) | Testé numériquement, validé visuellement (SVG) |
| Fondations arc-length | `geometry/polyline.cpp` (`flatten`, `resample_run`, `point_at_length`) | Testé numériquement |
| Tatami scanline + routage graphe | `stitch_generation/tatami.cpp` (`fill_tatami`, `connector_invalid`) | Testé (trous, U, L tous angles), validé visuellement |
| Satin 2 rails (zigzag alterné) | `stitch_generation/satin.cpp` (`fill_satin`) | Testé numériquement ; **rails d'origine douteuse** |
| Auto-satin **fondations** | `auto_satin/` : `rasterize`, `distance_transform`, `thin_zhang_suen`, `build_skeleton_graph`, `prune_graph`, `evaluate_satinability` | Testé numériquement (corpus de formes) |
| Analyse de points | `stitch_analysis/analyze.cpp` | Testé numériquement |
| Assemblage séquence | `stitch_generation/generate.cpp` (`generate_sequence`) : jump début, ColorChange, End, `source` par commande | Testé, intégration |
| Ordre de couture | `optimization/order.cpp` | Testé numériquement |
| Export/Import DST | `formats/dst.cpp` | Testé (aller-retour octet à octet) |
| SVG de diagnostic | `stitch_generation` golden + `auto_satin/debug_export.cpp` (squelette) | Validé visuellement |

## 2. Fonctionnalités seulement exposées dans l'interface (pas de moteur derrière)

- **Aucune** ne ment aujourd'hui : l'UI ne propose que ce qui existe (running,
  tatami, satin manuel, orientation, filtres). La classification auto **route
  vers tatami** ; le satin auto naïf est désactivé (`AutoOptions::use_naive_satin`).

## 3. Fonctionnalités partielles

- **Satin** : `fill_satin` correct pour deux rails **donnés**, mais les rails
  viennent de `rails_from_contour` (heuristique « deux sommets les plus
  éloignés » → **déborde** sur formes concaves/branchues, mesuré à 57 %). Pas de
  barreaux, pas de correspondance par sections, pas de short/split, pas de
  terminaisons, pas de sous-couches, densité mesurée **le long du rail** (pas
  perpendiculairement).
- **Tatami** : scanline + routage validé géométriquement ; **manquent** :
  sous-couches, underpath caché (les liaisons non cousables deviennent des
  sauts), entrée/sortie imposées, motifs de phase avancés, gestion d'îlots
  explicite.
- **Auto-satin** : fondations présentes (jusqu'au graphe + satinabilité), **pas
  de rails/barreaux/`SatinColumnGeometry`** — `analyze_region` s'arrête au
  rapport (voir `auto_satin.cpp`, commentaire « NE génère PAS encore les rails »).
- **Édition de nœuds** : déplacement seul.

## 4. Fonctionnalités désactivées

- **Satin automatique naïf** (`autodigitize`) : `use_naive_satin=false` par
  défaut. Le code existe mais n'est plus utilisé par la numérisation.

## 5. Algorithmes utilisés

- Bézier : aplatissement adaptatif De Casteljau ; échantillonnage par longueur
  d'arc (jamais par pas de `t`).
- Tatami : balayage (scanline) pair-impair, graphe d'adjacence des segments de
  rangée, validation géométrique des connecteurs (`proper_intersect` +
  point-in-region pour les diagonales larges).
- Satin : ré-échantillonnage des deux rails par fraction d'abscisse curviligne,
  zigzag alterné.
- Auto-satin : rasterisation (cv::fillPoly encapsulé), transformée de distance
  (cv::distanceTransform), amincissement **Zhang-Suen**, compression du squelette
  en graphe, classification des nœuds par **nombre de croisement** (robuste aux
  escaliers diagonaux), élagage des branches terminales courtes, satinabilité par
  seuils (largeur, élongation = longueur squelette / largeur moyenne, trous).

## 6. Limitations connues

- Rails satin naïfs → débordement ; densité non perpendiculaire.
- Pas de barreaux stockés ; le satin ne « connaît » pas ses sections.
- Pas de modèle de **passes** (underlay/top/travel/lock) : un objet = un seul
  type, la génération produit une séquence plate (pas de passes séparées
  affichables/analysables).
- `SatinParams` ne porte que deux rails + densité/compensation/sous-couche
  centrale (booléen) + `max_width`. Pas de rungs, pas de short/split, pas de
  terminaisons, pas de compensation détaillée, pas d'entrée/sortie/lock.
- Sérialisation `.osp` : `schemaVersion = 1`, pas de mécanisme de migration
  (lecture tolérante via `value(clé, défaut)` seulement).

## 7. Tests existants

- `tests/unit/stitch/` : running, tatami (trou/U/L tous angles), satin, stats.
- `tests/unit/auto_satin/test_pipeline.cpp` : raster, distance, squelette, graphe,
  satinabilité sur le corpus (`rectangle/capsule/ribbon/s/y/cross/circle/ring/
  wide`). **Aucun test de rails/barreaux** (ils n'existent pas encore).
- `tests/unit/commands/`, `project_io/`, `formats/`, intégration `test_pipeline`.
- Total : **163 tests** au vert (Debug + Release).

## 8. Erreurs / risques connus

- `rails_from_contour` : débordement structurel (documenté, désactivé par défaut).
- Satin : accumulation de pénétrations aux extrémités pointues (non géré).
- Tatami : underpath = saut (pas de trajet caché).
- Pas de garde contre séquences non finies (à couvrir par invariants génériques
  quand les nouveaux générateurs arriveront).

## 9. Ordre recommandé des travaux

Conforme au plan de la mission :

1. **Lot 1 — auto-satin géométrique** : sections transversales, stabilité
   gauche/droite, rails, barreaux, `SatinColumnGeometry`, validation, formes
   simples + Y (décomposition) + refus cercle/anneau/large, SVG, intégration
   document (rungs sérialisés + migration), undo/redo, action UI. **(cette
   mission)**
2. Lot 2 — générateur satin correct (consomme `SatinColumnGeometry`,
   correspondance par sections, espacement perpendiculaire).
3. Lot 3 — short + split + terminaisons. 4. Sous-couches + compensation.
   5. Entrée/sortie + lock. 6. Routage. 7. Tatami avancé. 8. Édition manuelle.
   9. Contours. 10. Remplissages. 11. Analyse. 12. Palettes/profils. 13.
   Appliqué. 14. Validation physique.

## 10. Décision d'architecture pour le Lot 1

- Les fondations `auto_satin` (raster→graphe→satinabilité) sont **conservées**.
- Nouveau : `auto_satin/satin_column.*` produit `SatinColumnGeometry`
  (rails + barreaux) **éditable**, sans toucher au générateur (Lot 2).
- Le document gagne des **barreaux** dans `SatinParams` (rétrocompatibles).
- La génération de points reste `fill_satin(rail_a, rail_b)` **mais** sur des
  rails désormais **corrects** (issus du squelette, pas du contour). Le passage
  du générateur à une correspondance par barreaux est **Lot 2** — pas ici.
- Honnêteté : le résultat du Lot 1 est *Présent + Testé numériquement + Validé
  visuellement (SVG)*. Pas de validation simulateur/physique.
