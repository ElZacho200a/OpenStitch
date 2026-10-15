---
name: openstitch-digitize
description: Numériser une image en broderie avec OpenStitch (image → objets → points → DST) de façon à obtenir un résultat brodable et fidèle, et diagnostiquer une numérisation décevante. À utiliser quand l'utilisateur fournit une image (illustration, logo, photo, dessin au trait) à broder, demande « numérise ça », « pourquoi c'est moche / lourd / plein de coupes », compare une numérisation automatique à une version retouchée à la main (.osp), ou quand on règle `digitize`, `--dark-lines`, `--satin-bands`, `--skip-background`, `--max-colors`, `--min-region-mm2`.
---

# Bien numériser une image en broderie

La numérisation automatique donne un **point de départ**, pas un résultat fini. La méthode
ci-dessous vient d'un cas réel : une illustration plate de navire (1448 × 1086 px, 300 mm), où
une version retouchée à la main par l'utilisateur était nettement meilleure que la sortie
automatique. Chaque règle porte la mesure qui la justifie. Documentation de référence :
`docs/source/auto-numerisation.md` (à lire par `search_docs`, voir le skill `openstitch-docs`).

## 1. Regarder l'image avant de lancer quoi que ce soit

| Question | Pourquoi |
|---|---|
| **Plate (aplats, contours nets) ou photo ?** | Plate : mode `shapes` (défaut). Dessin au trait : `contours`. Photo : `stipple`, ou la réduire à 6–10 couleurs avant (une photo en `shapes` donne des milliers de petites régions). |
| **Y a-t-il un fond ?** | Un fond (papier, crème, blanc) ne se brode pas. Voir §3. |
| **Taille réelle voulue (mm) ?** | `--dpi` fixe l'échelle : mm/px = 25,4 / dpi. 1448 px pour 300 mm → `--dpi 122.6`. Sans `--dpi`, 96 est supposé et la pièce sort 1,3 fois trop grande. Les détails fins se jugent en mm, pas en pixels. |
| **Combien de vraies couleurs de fil ?** | `--max-colors` (défaut 8). Compter les teintes distinctes visibles ; mettre 1 à 2 de plus que ce compte, jamais 64 : deux quasi-doublons = un changement de fil de plus pour rien. |
| **Contours noirs fins (mâts, haubans, bords) ?** | `--dark-lines`, voir §4. |

## 2. Lancer, mesurer, regarder

Binaire : `openstitch-cli` (voir `openstitch-build-test` pour le build). Sorties dans le
**scratchpad**, jamais à la racine du dépôt.

```bash
openstitch-cli digitize img.png sortie.dst --dpi 122.6 --max-colors 12 --output-svg diag.svg
openstitch-cli stats sortie.dst        # points, sauts, coupes, changements de fil, dimensions
python3 -I .claude/skills/openstitch-digitize/scripts/render_dst.py sortie.dst rendu.png --source img.png
```

Lire **dans cet ordre** :

1. **Dimensions** : si elles dépassent la taille voulue, le fond a été brodé (§3).
2. **Messages de `digitize`** : fond détecté ou non, `Surface non couverte`, nombre d'objets.
3. **`stats`** : points, coupes, changements de fil. Repères observés sur une image de ~300 mm
   à 12 couleurs : 80 000 à 120 000 points, 1 400 à 1 700 coupes. Au-delà de 3 000 coupes, un
   nombre excessif d'objets séparés est en cause (voir §6).
4. **Le rendu**, en regardant la lisibilité des mâts, des contours et des petites zones, pas
   seulement les chiffres.

Le rendu du script est **approximatif** (une couleur par bloc, tatami en lignes pointillées) ;
ce n'est pas le rendu réaliste de l'application. Ne jamais le présenter comme tel, et le dire.
Si une version retouchée existe (`.osp`), lui passer `--palette` (couleurs des objets par ordre
de couture) car sa boîte englobante ne correspond plus à l'image.

## 3. Le fond : la première cause d'échec

Détection automatique (`segmentation::background_candidate`, même règle CLI et bureau) :

- fond **clair** (L\* > 90) qui touche au moins 3 bords ; **ou**
- fond **coloré** (crème, beige, aplat) qui occupe au moins 85 % du pourtour et touche les 4 bords.

Un ciel ou une mer plein cadre n'est **jamais** ignoré (ils ne remplissent pas tout le pourtour).
Le CLI imprime `Région candidate au fond` et `Ignorer la plus grande région : oui/non`. Si ce
n'est pas le bon choix : `--skip-background yes|no`. Cas vécu : fond beige non détecté
(L\* 86) → 299 × 224 mm et +22 % de points, alors que le motif fait 290 × 200 mm.

## 4. Choisir les options

| Besoin | Option | Réserve |
|---|---|---|
| Traits noirs fins lisibles (mâts, haubans, contours) | `--dark-lines` (`--dark-lines-width 1.6`, `--dark-lines-lightness 25`) | +40 à +60 % de coupes mesurées (1 181 → 1 675 ; 941 → 1 499) : chaque trait est un tracé séparé, cousu en dernier. Point droit seulement. |
| Petites zones parasites | `--min-region-mm2` (défaut 3), `--min-feature-mm` (défaut 0 = désactivé ; une valeur retire les détails plus étroits que cette largeur, à essayer par petits pas) | Trop haut : on perd des détails voulus. |
| Bandes étroites en satin (rayons de nuages, écume, rubans) | `--satin-bands` | **Désactivé par défaut volontairement** : la décision par génération d'essai est un point à faire relire au regard du brevet US8851001B2 (`docs/revue-brevets-optimisation-trajets-2026-10.md`). Ne pas l'activer par défaut sans cette relecture ; l'activer sur demande explicite de l'utilisateur pour son propre projet est possible. |
| Moins de coupes avant export | `--hidden-travel` (et niveaux) | Gain surtout en longueur de saut, peu en nombre de coupes sur des régions juxtaposées. |
| Dessin au trait, logo | `--mode contours --detail 0.5` (+ `--single-path`) | — |
| Photo, rendu des tons | `--mode stipple --stipple-points N` | — |

Règle d'or : **un réglage à la fois**, et mesurer avant/après avec `stats` (même `--dpi`, même
`--max-colors`).

## 4 bis. Ce qu'une retouche manuelle apporte (cas du navire)

Comparaison de la sortie automatique à une version retouchée par l'utilisateur (344 objets
contre ~500 en automatique) :

1. **Fond supprimé** (voir §3).
2. **Bandes étroites en satin** : ~80 % des objets étaient en auto-satin ; le tatami n'était
   gardé que pour les grandes surfaces.
3. **Régions fusionnées** : moins d'objets, plus grands ; les îlots et morceaux de même couleur
   regroupés.
4. **Ordre des couleurs réfléchi** : la grande base sombre (bleu marine) d'abord, puis les
   couleurs claires par-dessus, les traits quasi noirs en dernier. Une base sombre sous les
   surfaces claires absorbe les interstices entre régions voisines (`fill_overlap` vaut 0 par
   défaut : sans base, un interstice apparaît entre deux couleurs voisines).
5. **Tatami étirés** pour recouvrir les écarts.

Le coût de cette version : environ deux fois plus de coupes (3 000 contre 1 460) à cause des
objets satin séparés. Meilleur visuellement, pas moins de coupes : le dire.

## 5. Dans l'application

La boîte *Numérisation automatique* reprend les mêmes réglages (fond, couleurs, satin des
bandes, chevauchement). Ensuite, les retouches se font par commandes annulables : fusion de
zones, étirement de tatami, ordre des couleurs. « Optimiser les trajets » se lance à la fin,
juste avant l'export, jamais avant : il ne modifie ni l'ordre des couleurs ni la couche visible.

## 6. Diagnostic rapide

| Symptôme | Cause probable | Action |
|---|---|---|
| Pièce trop grande / bordure brodée | fond non ignoré ou `--dpi` oublié | §1 et §3 |
| Mâts et contours fusionnés en blocs sombres | traits fins quantifiés comme des régions | `--dark-lines` |
| Zones de ciel/nuage en lignes pointillées, aspect de grille | tatami sur bandes étroites | satin sur ces bandes (§4) |
| Plus de 3 000 coupes | beaucoup d'objets séparés (satin, traits, îlots) | monter `--min-region-mm2`, baisser `--detail`, fusionner à la main |
| Interstices entre couleurs voisines | `fill_overlap` = 0 | base sombre sous les couleurs claires (§4 bis), ou chevauchement dans la boîte de dialogue |
| Beaucoup de changements de fil | `--max-colors` trop haut, quasi-doublons | le baisser |
| Petits points parasites | îlots | `--min-region-mm2`, `--min-feature-mm` |

## 7. Ce qu'il ne faut pas faire

- Présenter le rendu du script comme le rendu final de l'application.
- Annoncer « plus propre » sans avoir comparé `stats` avant/après au même `--dpi`.
- Activer `--satin-bands` par défaut dans le code sans la relecture brevet.
- Contourner la règle d'or des modules : toute logique de numérisation va dans `libs/autodigitize`
  ou `libs/segmentation` (Qt-free), pas dans `apps/desktop`.
- Écrire des fichiers de travail dans le dépôt (les `.dst`, `.png` d'essai vont au scratchpad).

## 8. Ce skill s'enrichit

Quand une nouvelle image révèle un échec ou une règle (seuil, option, ordre), l'ajouter ici avec
**la mesure qui la justifie** (chiffre avant/après, image de référence), dans la section concernée,
et l'écrire aussi dans `docs/source/auto-numerisation.md`. Ne pas y mettre d'hypothèse non vérifiée.
