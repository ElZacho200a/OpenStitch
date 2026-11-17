# R02 — Qualité et non-régression

Règle : **aucune sortie ne change**. Les optimisations R02 sont des réécritures exactes. Aucune
tolérance numérique n'est admise : coordonnées, ordre des commandes, types, passes et
empreintes sont identiques octet pour octet.

## Preuves

| Contrôle | Portée | Résultat |
|---|---|---|
| Empreintes séquence / étiquettes / aperçu, banc R01 | 4 motifs × 2 réglages de fils : toutes les campagnes R02 (`profile_engine.ps1`) et l'A/B entrelacé `r02-ab2` (528 passages de la branche) | identiques à la baseline |
| `pull_field equivalence : identique bit a bit a l evaluation d origine` | 3 jeux de 3 000 sources aléatoires (traits longs, points d'arrêt, segments nuls, poids sous et autour du seuil 1e-4) × 5 tissus (grain tourné, anisotropie, calibré, portée étroite, plafond nul) × 6 000 échantillons, rang minimal varié | déplacement, nombre de contributeurs et confiance identiques bit à bit |
| `pull_field equivalence : simulation, carte et compensation independantes du nombre de fils` | séquence aléatoire de 12 000 commandes, lin | simulation et carte identiques à 1 et 8 fils ; simulation identique point par point à la référence |
| `hidden travel grid : identique cellule par cellule a la rasterisation d origine` | 3 séquences aléatoires de 4 000 commandes × 5 largeurs de fil (0, 1, 350, 400, 1 250 µm) × 1 et 8 fils | 0 cellule différente |
| `hidden travel : meme resultat avec un seul fil et avec plusieurs` | scène à 8 ponts acceptés et 7 refus | séquence et bilan identiques |
| Suites existantes | `test_stitch` (512 cas, 1 090 606 assertions) ; CTest Release complet | vert ; **2 749/2 749** |

L'aperçu de marine est rastérisé depuis la séquence simulée : son empreinte contrôle aussi
`simulate_fabric_pull` de bout en bout.

## Arguments d'exactitude

- **`PullField`.** Mêmes cellules (clé et taille inchangées), mêmes sources par cellule en
  indices croissants (tri stable), donc même ordre de sommation. Grandeurs par segment
  calculées avec les expressions d'origine. Rejet anticipé si l'exposant A + B dépasse
  ln(poids / 1e-4) + 1e-6. Comme exp(-A)·exp(-B) ≤ 1, le poids calculé est alors sous 1e-4
  avec une marge relative de 1e-6, très supérieure aux arrondis de `exp` et des produits :
  seules sont écartées les sources que le test d'origine `w < 1e-4` écarterait.
  Point relevé : une cellule de 3σ contient des sources au-delà de 3σ du point, dont le poids
  peut atteindre ~1 %. Une grille plus fine aurait donc changé la sortie et a été écartée.
- **Grille des ponts cachés.** Tout centre à distance ≤ r du segment est à moins de r, en x,
  d'un point du segment situé dans la bande |y − cy| ≤ r. Avec r + 1 µm de marge et une cellule
  de part et d'autre, l'intervalle testé contient toutes les cellules retenues par le test
  d'origine. Le test garde le même t et les mêmes écarts ; le carré de la distance ne décide
  qu'en dehors de ±1e-9 relatif autour du seuil, et `hypot` tranche dans cette zone. Les bandes
  parallèles écrivent des rangées disjointes, en parcourant les points par indice croissant :
  la valeur « dernier point couvrant » de chaque cellule ne dépend pas du découpage.
- **Plans des ponts cachés.** `plan` ne modifie aucun état après la construction de la grille
  quand `allow_same_color_surface` est désactivé ; sinon le chemin séquentiel d'origine est
  conservé. La boucle de décision est inchangée et prend un plan pré-calculé dont les arguments
  sont identiques à ceux de son propre appel.

## Limites

- Une seule machine (Ryzen 7 7700X, RTX 5060 Ti) et un seul compilateur (MSVC). La CI
  `linux-core` compile et teste le cœur sous Linux. Les tests d'équivalence y vérifient
  l'exactitude, pas les temps.
- Le tirage prédit (`apply_predicted_pull`) n'est actif sur aucun motif du banc ; il est couvert
  par les tests unitaires existants (`[predicted_pull]`) et partage le `PullField` vérifié.
- Le Debug complet n'a pas été rejoué dans cette session ; la CI le couvre.
- Aucun essai physique : sans objet, la broderie produite est identique.
