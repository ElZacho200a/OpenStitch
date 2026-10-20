# Paramètres de numérisation et retouche manuelle

*Référence chargée à la demande par le skill `openstitch-digitize` (§ « Choisir les options »).*

## Choisir les options

| Besoin | Option | Réserve |
|---|---|---|
| Traits noirs fins lisibles (mâts, haubans, contours) | `--dark-lines` (`--dark-lines-width 1.6`, `--dark-lines-lightness 25`) ; dans l'application : case « Traits sombres cousus par-dessus » de la boîte *Numérisation automatique* | +40 à +60 % de coupes mesurées (1 181 → 1 675 ; 941 → 1 499) : chaque trait est un tracé séparé, cousu en dernier. Point droit seulement. |
| **Îlots collés à de grands aplats** (enclaves) | `--island-cleanup MM2` (essayer 20 ; `--island-max-delta-e` pour limiter l'écart de couleur) ; dans l'application : *Segmentation → Nettoyer les petits îlots…* | Fusionne dans la voisine de **couleur** la plus proche. Mesuré : 344 → 173 objets, 887 → 681 coupes à 20 mm². Les traits fins sombres (haubans) sont aussi des îlots : **associer à `--dark-lines`**. |
| Filets clairs entre zones jointives (interstices) | `--fill-inset 0` (défaut 0,2) | +16 % de points (91 755 -> 106 764), coupes inchangées ; `0.1` = compromis (+5 %). Mesure : journal-essais.md, essai 2 |
| Petites zones parasites | `--min-region-mm2` (défaut 3), `--min-feature-mm` (défaut 0 = désactivé ; une valeur retire les détails plus étroits que cette largeur, à essayer par petits pas) | Trop haut : on perd des détails voulus. |
| Bandes étroites en satin (rayons de nuages, écume, rubans) | `--satin-bands` | **Désactivé par défaut volontairement** : la décision par génération d'essai est un point à faire relire au regard du brevet US8851001B2 (`docs/revue-brevets-optimisation-trajets-2026-10.md`). Ne pas l'activer par défaut sans cette relecture ; l'activer sur demande explicite de l'utilisateur pour son propre projet est possible. |
| Moins de coupes avant export | `--hidden-travel` (et niveaux) | Gain surtout en longueur de saut, peu en nombre de coupes sur des régions juxtaposées. |
| Dessin au trait, logo | `--mode contours --detail 0.5` (+ `--single-path`) | — |
| Photo, rendu des tons | `--mode stipple --stipple-points N` | — |

Règle d'or : **un réglage à la fois**, et mesurer avant/après avec `stats` (même `--dpi`, même
`--max-colors`).


## Ce qu'une retouche manuelle apporte (cas du navire)

Comparaison de la sortie automatique à une version retouchée par l'utilisateur (344 objets
contre ~500 en automatique) :

1. **Fond supprimé** (voir « Le fond » dans SKILL.md).
2. **Bandes étroites en satin** : ~80 % des objets étaient en auto-satin ; le tatami n'était
   gardé que pour les grandes surfaces.
3. **Régions fusionnées** : moins d'objets, plus grands ; les îlots et morceaux de même couleur
   regroupés (`--island-cleanup` automatise cette passe).
4. **Ordre des couleurs réfléchi** : la grande base sombre (bleu marine) d'abord, puis les
   couleurs claires par-dessus, les traits quasi noirs en dernier. Une base sombre sous les
   surfaces claires absorbe les interstices entre régions voisines (`fill_overlap` vaut 0 par
   défaut : sans base, un interstice apparaît entre deux couleurs voisines).
5. **Tatami étirés** pour recouvrir les écarts.

Le coût de cette version : environ deux fois plus de coupes (3 000 contre 1 460) à cause des
objets satin séparés. Meilleur visuellement, pas moins de coupes : le dire.

