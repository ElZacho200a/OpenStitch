# Recherche — moteur de génération de points

Ce document recense les références étudiées pour reconstruire le moteur, leurs
licences, les algorithmes retenus, et les écarts avec notre implémentation.
Aucune ligne de code tierce n'est copiée (voir §Licences).

## 1. Sources étudiées et licences

| Source | Licence | Compatible réutilisation dans un projet Apache-2.0 ? | Usage autorisé ici |
|---|---|---|---|
| **Ink/Stitch** (inkstitch/inkstitch) | **GPL-3.0** | **NON** — le copyleft GPL est incompatible avec l'incorporation dans un projet Apache-2.0 | Étude **conceptuelle** uniquement (idées d'algorithmes, découpage). **Aucune copie de code**, aucune traduction ligne à ligne. |
| **pyembroidery** (EmbroidePy) | **MIT** | Oui (attribution) | Référence pour le modèle de commandes machine (STITCH/JUMP/TRIM/COLOR_CHANGE/STOP/END) et les conventions de format. Concepts réimplémentés. |
| **libembroidery** (Embroidermodder) | zlib | Oui (attribution) | Référence croisée du format DST (déjà utilisée en Phase 7). |
| **Clipper2** (AngusJohnson/Clipper2) | BSL-1.0 | Oui (déjà intégrée) | Booléens/offsets polygonaux robustes (union, inflate). Utilisée via `libs/geometry`. |
| **Boost.Geometry** | BSL-1.0 | Oui | Non intégrée : Clipper2 couvre nos besoins polygonaux ; réévaluable pour les prédicats/segments si nécessaire. |

### Point juridique déterminant

Notre projet est **Apache-2.0** (ADR-002). **Ink/Stitch est GPL-3.0** : on ne
peut **pas** copier, adapter, ni traduire son code source dans ce dépôt sans
faire basculer le projet sous GPL. La démarche retenue est donc stricte :

1. lire la documentation et, éventuellement, le code d'Ink/Stitch pour
   **comprendre les algorithmes** (les algorithmes eux-mêmes — Douglas-Peucker,
   scanline fill, parcours eulérien, ré-échantillonnage par longueur d'arc — ne
   sont pas protégeables : ce sont des méthodes classiques publiées) ;
2. **réimplémenter proprement** en C++ à partir de la littérature géométrique,
   sans reprise de structure de fichiers ni de code ;
3. n'intégrer du code tiers que sous licence permissive (MIT/BSD/BSL/zlib), avec
   attribution dans `THIRD_PARTY_LICENSES.md`.

À ce jour, **aucune** portion de code d'Ink/Stitch ou de pyembroidery n'est
présente dans le dépôt.

## 2. Algorithmes retenus (fondations + running stitch)

### 2.1 Aplatissement adaptatif de Bézier cubique

Réf. : littérature classique (subdivision de De Casteljau, critère de platitude
par distance des points de contrôle à la corde). Un segment `[P0,P1,P2,P3]` est
subdivisé récursivement tant que la distance des contrôles `P1,P2` à la droite
`P0P3` dépasse la tolérance, avec une profondeur maximale de sécurité. Produit
une polyligne dont l'erreur est bornée par la tolérance.

### 2.2 Paramétrisation par longueur d'arc

Réf. : arc-length reparameterization (tables de longueur cumulée + recherche
binaire). Sur la polyligne aplatie, on construit les longueurs cumulées, puis
`point_at_length(s)` interpole linéairement le point à l'abscisse curviligne
`s`. C'est exact sur une polyligne (l'aplatissement ayant déjà borné l'erreur
de courbure).

### 2.3 Ré-échantillonnage équilibré

Réf. : §6.2 du cahier des charges. Pour un tronçon de longueur `L` et une cible
`d` : `n = max(1, ceil(L/d))`, pas réel `L/n`, positions `s_i = i·L/n`. Extrémités
exactes, aucun résidu court.

**Écart assumé au §6.2** : le cahier des charges suggère `round`. Nous utilisons
`ceil` : comme les `n` parts sont **égales** (`L/n`), les deux évitent le segment
résiduel minuscule, mais `ceil` garantit en plus que **chaque point ne dépasse
jamais la longueur cible** — traitée ici comme une longueur *maximale*, ce qui
est le comportement physiquement attendu d'un `stitch_length` en broderie.
`round` pourrait produire des points jusqu'à 1,5× la cible.

### 2.4 Préservation des angles vifs

Réf. : approche « corner splitting » (comparable à Ink/Stitch, mais
réimplémentée). On détecte les sommets dont l'angle de rotation dépasse un seuil
(défaut 35°), on découpe le chemin en tronçons entre coins, et on
ré-échantillonne **chaque tronçon indépendamment** par longueur d'arc. Résultat :
les coins sont des pénétrations exactes ; les portions lisses ont un espacement
régulier ; aucune rafale de micro-points.

### 2.5 Chemins fermés

Boucle traitée comme un anneau. Sans coin (cercle), ré-échantillonnage global
`n = round(L/d)` produisant `n` points, le `n`-ième coïncidant avec le départ
(pas de doublon final ajouté). Avec coins, tronçons enchaînés autour de la
boucle. Options `direction` (sens), `phase` (décalage curviligne initial),
`start` (projection d'un point de départ imposé sur le chemin).

### 2.6 Double / triple / backstitch

Réf. : §8–9. Modes explicites, réimplémentés :

- **SinglePass** : un passage.
- **BackAndForth** : aller complet puis retour complet (termine au départ).
- **BeanStitch(n)** : chaque intervalle `A→B` cousu `n` fois (n impair) sans
  mouvement nul ; comptage exact des traversées testé.
- **Backstitch** : progression avec recouvrement `P0→P1→P0→P2→P1→P3…`, couvrant
  chaque intervalle, sans saut.

## 3. Différences avec l'implémentation précédente

| Aspect | Avant | Après |
|---|---|---|
| Échantillonnage | par arête (sommets imposés) | par **longueur d'arc** globale entre coins |
| Espacement sur courbe | = densité des sommets (faux) | = longueur cible (correct) |
| Points courts | sommets toujours gardés | coins seulement ; nettoyage §6.3 |
| Bézier | tangentes ignorées | aplatissement adaptatif |
| Résultat | `vector<Vec2um>` | `RunningResult { points, warnings, stats }` |
| Fermé | départ nœud 0 uniquement | phase/sens/départ imposables |
| Répétitions | 2 modes en dur | énum de modes + comptage de traversées testé |

## 4. Parties réutilisées de tiers

Aucune. Tout est réimplémenté. Dépendances tierces inchangées (Clipper2 pour les
opérations polygonales, déjà encapsulée). Attribution : voir
`THIRD_PARTY_LICENSES.md`.

## 5. Risques

- **Juridique** : ne jamais copier de code Ink/Stitch (GPL). Revue de PR
  systématique. Ce document sert de garde-fou.
- **Technique** : la préservation des coins dépend d'un seuil d'angle ; un seuil
  trop bas fragmente, trop haut coupe les angles. Défaut 35°, configurable,
  testé. L'aplatissement de Bézier a une profondeur max de sécurité.

## 6. Rapport avant / après (running stitch)

Mesures reproductibles par les tests `tests/unit/stitch/test_running_stitch.cpp`
et les SVG de diagnostic `tests/golden/stitch-generation/*.svg` (régénérables
via `openstitch-cli stitchdebug`, jamais réécrits par les tests).

| Cas | Avant | Après |
|---|---|---|
| Cercle r=10 mm, d=3 mm | 64 points (~0,98 mm) — faux | ~21 points (~3 mm) |
| Polyligne 200×0,1 mm, d=3, min=0,5 | 200 points (0,1 mm) | écarts ≥ 0,5 mm |
| Coin en L, d=3 mm | coin conservé (ok) mais espacement réinitialisé | coin exact + espacement régulier de part et d'autre |
| Bézier demi-boucle | segment droit | suit la courbe (erreur < tolérance) |
| Chemin d'un seul nœud | `{}` silencieux | warning `PathTooShort` |

Les chiffres exacts sont vérifiés par les tests (voir le message de livraison
pour la sortie réelle de `ctest`).
