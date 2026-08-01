# Recherche — moteur d'auto-satin

Références étudiées, choix d'algorithmes et licences pour la conversion
automatique d'une région en colonnes satin. Aucune ligne de code tierce n'est
copiée (voir §Licences).

## Choix d'algorithme de squelette

| Méthode | Disponibilité ici | Décision |
|---|---|---|
| `cv::ximgproc::thinning` | **NON** — le module contrib `ximgproc` n'est pas dans notre build OpenCV (features `core,png,jpeg,tiff` uniquement) | écartée |
| Amincissement **Zhang-Suen** | réimplémentable, simple, déterministe | **RETENU** (v1) |
| Amincissement Guo-Hall | idem, alternative | réévaluable |
| Medial axis par Voronoï (CGAL) | CGAL non intégrée, licence GPL/LGPL, lourde | écartée |
| Maxima de la transformée de distance | crêtes parallèles, bruité | non seul (sert au poids) |

**Décision** : rasterisation physique (masque binaire) → **transformée de
distance** (OpenCV, `cv::distanceTransform`, disponible dans `imgproc`) pour la
demi-largeur locale et l'élagage → **amincissement Zhang-Suen** (implémenté en
interne, sans dépendance) → conversion en **graphe** (nœuds = extrémités /
jonctions ; arêtes = polylignes de pixels de degré 2 compressées). Le graphe
porte le rayon local (distance) le long de chaque arête, ce qui guide l'élagage
et l'analyse de satinabilité.

Rationale : Zhang-Suen est un standard publié (méthode classique, non
protégeable), déterministe, et suffisant pour une première version. La
transformée de distance d'OpenCV fournit une demi-largeur robuste.

## Analyse de satinabilité

Réf. : métriques de forme classiques. On calcule aire, périmètre, largeur
moyenne (`2·aire/périmètre`), largeur maximale (`2·max` de la transformée de
distance), largeur minimale (le long du squelette), élongation
(`longueur_axe / largeur_moyenne`), nombre de trous, nombre d'extrémités et de
jonctions du graphe. Le **statut** en découle (Suitable, SuitableWithWarnings,
RequiresDecomposition, Ambiguous, Unsuitable).

## Licences

| Dépendance | Licence | Usage ici |
|---|---|---|
| OpenCV (core, imgproc) | Apache-2.0 | rasterisation + transformée de distance (encapsulée dans `libs/auto_satin`) |
| Clipper2 | BSL-1.0 | nettoyage polygonal (via `libs/geometry`) |
| Ink/Stitch | **GPL-3.0** | **étude conceptuelle uniquement** du modèle rails+barreaux — **aucune copie** |

Point juridique : notre projet est **Apache-2.0**. Ink/Stitch est GPL ; intégrer
son code imposerait une redistribution copyleft. On n'en reprend donc **ni code
ni structure expressive** ; les algorithmes (Zhang-Suen, distance transform,
skeleton pruning, medial axis) sont des méthodes publiées réimplémentées
indépendamment. Toute réutilisation de code tiers ferait l'objet d'une
vérification de licence spécifique.

## Parties réutilisées de tiers

Aucune. Zhang-Suen, la conversion squelette→graphe et l'élagage sont
réimplémentés. OpenCV et Clipper2 (permissives) sont utilisées via des
interfaces internes.

## Déterminisme

Toutes les étapes sont déterministes : rasterisation à résolution fixe,
transformée de distance déterministe, Zhang-Suen déterministe, tri stable des
nœuds/arêtes (par coordonnées puis identifiant). Aucun `unordered_map` dans les
chemins de décision.

## Risques

- **Résolution** : un pixel trop grossier perd les formes fines ; trop fin
  produit des images énormes. Résolution par défaut 0,05 mm, bornée par une
  dimension maximale.
- **Squelette bruité** : les branches parasites sont élaguées par score
  (longueur × rayon local) — implémenté en mission ultérieure ; pour l'instant le
  graphe brut est exposé et diagnostiqué.
- **Formes ambiguës** : cercle plein et formes très larges refusés proprement.
  Un anneau fin à un trou est désormais décomposé en quatre sections ouvertes
  raccordées ; un anneau non appariable reste refusé.
