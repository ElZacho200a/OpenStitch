# Fiche de production

Public : utilisateur, développeur. État : **Implémenté** (HP-PROD-001 ; durée estimée : première
version de HP-PROD-002).

## But

Une page A4 portrait à imprimer ou à joindre à un devis / un ordre de broderie : tout ce
qu'un brodeur doit savoir avant de lancer la machine, sans ouvrir OpenStitch.

## Contenu

1. **En-tête** : nom du projet et date.
2. **Aperçu du motif** : rendu réaliste (`libs/stitch_render`, mêmes réglages que
   *Affichage ▸ Rendu réaliste*) ; sur un très grand motif, où le fil texturé serait
   illisible, des lignes colorées. L'image porte un **texte alternatif** complet.
3. **Caractéristiques** : dimensions réelles en mm (largeur × hauteur), taille du cadre et
   indication « le motif tient dans le cadre / dépasse du cadre », nombre de points, de
   sauts, de coupes, de changements de couleur, de couleurs, longueur de fil estimée, temps
   de broderie estimé.
4. **Blocs de couleur dans l'ordre de couture** : n°, pastille **et code hexadécimal** (la
   couleur n'est jamais portée par la seule teinte), référence de fil, points, sauts,
   longueur de fil, objets concernés.
5. **Avertissements** : résultats de `stitch_analysis::analyze` (gravité en toutes lettres,
   objet nommé, piste de correction ; les problèmes au-delà du plafond par catégorie sont
   comptés).
6. **Notes** : texte libre saisi dans le dialogue.

## Hypothèses du temps estimé

```
minutes = points / vitesse + changements de fil × 30 s + coupes × 2 s
```

La vitesse par défaut est de **700 points/min** (plage usuelle 600–800), réglable dans le
dialogue (100–2000) ou par `--speed` en ligne de commande ; les 30 s et 2 s sont les valeurs
de `ProductionOptions`. Les hypothèses sont **rappelées sur la fiche**. C'est un ordre de
grandeur : la vitesse réelle dépend de la machine, du fil et du motif. La **longueur de fil**
est le fil du dessus (somme des segments cousus de chaque bloc) : ni canette, ni fil de saut.

## Référence de fil

Le bloc affiche « marque gamme code - nom » quand son `ColorBlock::thread_key` est connu
(nuancier de `libs/thread_palette`). Aujourd'hui les objets du document ne portent que leur
couleur RVB : le champ reste vide (« — ») et la fiche n'invente jamais de référence. Il se
remplira sans changement de format dès que les blocs porteront leur clé de fil.

## Architecture

- `libs/stitch_analysis/…/production_sheet.hpp` : `make_production_sheet(project, sequence,
  options)` produit un `ProductionSheet` pur (aucun Qt, aucune horloge : la date est fournie)
  à partir de la séquence **effective** et de `color_blocks` (la table de blocs unique).
  `production_to_json` (JSON stable, schéma 1) et `production_to_html` (page autonome écrite
  dans le sous-ensemble de HTML/CSS de `QTextDocument`) vivent dans la même bibliothèque.
- `apps/desktop/production_dialog.*`, `main_window_production.cpp` : dialogue **Fichier ▸
  Fiche de production…**. Il édite nom, date, vitesse, notes, affiche le document HTML,
  l'**imprime** (`QPrinter`, avec aperçu `QPrintPreviewDialog`) ou l'**exporte en PDF**
  (`QPdfWriter`, A4 portrait, marges 15 mm). Le document imprimé est celui de la bibliothèque :
  aucun calcul dans l'interface.
- `openstitch-cli production` : voir [ligne de commande](cli.md).

## Limites

- Le PDF n'est produit que par le bureau (Qt) ; le CLI écrit du HTML autonome (aperçu SVG
  intégré), imprimable depuis un navigateur.
- Pas de champ « client » dédié : à écrire dans les notes.
- Un design importé (DST) n'a ni couleur ni référence de fil : tous les blocs sont noirs.
- Gabarit 1:1 (HP-PROD-003), consommation par couleur et canette (HP-PROD-005) : non faits.

## Tests

`tests/unit/stitch_analysis/test_production_sheet.cpp` (totaux, blocs, temps, cadre,
référence de fil, JSON/HTML déterministes et échappés, séquence vide) ;
`tests/unit/desktop/test_production_dialog.cpp` (champs → fiche, PDF écrit).
