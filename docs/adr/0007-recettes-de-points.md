# ADR 0007 — Recettes de points : préréglages intégrés plutôt qu'un générateur par fiche

*État : accepté pour STCH-CROS-01 (L1). Révisable si une recette exige un plan multi-passes
que les types existants ne savent pas porter.*

## Contexte

Le catalogue Crosstela recense 362 points de broderie à la main
([matrice](../research/crosstela-stitch-matrix.csv), [synthèse](../research/crosstela.md)).
Une fois classés pour la machine à point noué, 13 sont couverts tels quels (A), 27 sont des
réglages de types existants (B), 140 demandent une primitive (C), 110 ne sont qu'une simulation
visuelle (D) et 72 sont inadaptés (E). Les classes C se regroupent en peu de primitives : attache
aux intersections (treillis, couchage), phase en briques, semis, croisement central du satin,
tuiles de motif sur grille, rayons vers un centre.

Le document sait déjà nommer un jeu de paramètres (`document::StitchPreset`, HP-ENG-013) et
changer le type d'un objet (`set_stitch_type` → `commands::ConvertFillGroupCommand`, un pas
d'annulation). Studio expose les deux dans l'inspecteur.

## Options

| Option | Intérêt | Coût et limites |
| --- | --- | --- |
| Un type `StitchParams` par fiche | Fidélité nominale maximale | 362 alternatives de variant, sérialisation, inspecteur et tests à maintenir ; la plupart dupliquent un générateur existant |
| Moteur générique `RecipeDefinition → RecipeCompiler → StitchPlan` | Composition libre de passes | Nouveau format persistant et nouveau pipeline avant d'avoir une seule recette utile ; risque de doubler sous-couches, compensation et routage |
| **Catalogue intégré de recettes compilées en `StitchParams`** | Réutilise générateurs, sérialisation, inspecteur et la conversion de type ; aucune migration OSP | Une recette multi-passes doit d'abord devenir une primitive d'un type existant (C) |

## Décision

1. Une recette est une entrée **en lecture seule et versionnée** d'un catalogue livré avec le
   moteur (`document::stitch_recipes()`) : identifiant stable (identifiant d'URL Crosstela),
   version, noms FR/EN, cible (contour ou remplissage), fidélité (`Faithful` ou
   `Approximation`), source et `StitchParams` produits.
2. Appliquer une recette passe par l'opération de lot `apply_recipe` (`project_ops`), qui
   exécute `ConvertFillGroupCommand` comme `set_stitch_type` : même conversion (groupe de
   sections satin compris), un pas d'annulation, même chemin pour Studio, le CLI et le MCP.
   Le document ne garde que les paramètres obtenus ; ils restent modifiables dans l'inspecteur. **Pas de champ `recipe_id` dans l'OSP** : un fichier reste
   lisible par une version sans catalogue, et une recette révisée ne modifie jamais un projet
   déjà enregistré.
3. Une classe C devient d'abord une **primitive d'un type existant** (champ optionnel, défaut
   neutre, sérialisé seulement s'il diffère), puis une recette qui la règle. Exemples prévus :
   attaches aux intersections du croisé (treillis), phase en briques du remplissage.
4. Une recette D porte `Approximation` ; Studio et le CLI l'affichent comme une simulation.
   Aucune recette E.
5. Ni texte ni illustration Crosstela dans le dépôt : l'URL suffit comme provenance. Les
   vignettes du catalogue Studio sont rendues par OpenStitch.

## Conséquences

- Déterminisme et performance inchangés : les recettes passent par les générateurs actuels.
- Le moteur générique reste possible plus tard si plusieurs recettes exigent des passes
  hétérogènes (Bayeux : posé + traverses + attaches) ; il partirait alors des primitives créées ici.
- Un test garde l'unicité des identifiants et la génération de chaque recette sur sa forme cible.
- Le satin n'est pas une recette : sur une forme, il exige la planification de l'opération
  `auto_satin` (refus explicite si aucune colonne), pas une affectation de paramètres.
