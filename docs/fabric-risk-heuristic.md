# Carte de risque textile — prototype heuristique V1

Ce module donne un **indice relatif de 0 à 100** de risque potentiel de tirage
d'un textile sous une broderie. **Il ne calcule pas une tension en newtons,
une déformation en millimètres ni la formation de plis.**
La méthode n'est **pas** calibrée sur machine réelle.

## Utilisation

Interface Windows : menu **Analyse → Risque de tirage du tissu…**, choisir
un profil indicatif (coton tissé, jersey ou feutrine). Le rapport texte et
l'export PNG sont disponibles dans le dialogue.

CLI :

```bash
openstitch-cli project fabric-risk motif.osp --fabric jersey \
    --cell-mm 1 --heatmap risque.png --output risque.json
```

Choix : `--fabric woven|jersey|felt` (défaut : `woven`).
La commande lit la **séquence effective** et ne modifie jamais le `.osp`.
Schéma de rapport : `openstitch.fabric-risk/1`.
Les cartes utilisent le même repère modèle que les analyses de densité,
avec des cellules de 1 mm par défaut ; les cellules sans fil sont grises.

## Hypothèses et calcul

1. Construire la liste des segments cousus par `build_thread_model`,
   qui utilise `stitch_generation::effective_sequence`.
2. Accumuler sur une grille la longueur du fil et son tenseur d'orientation :
   `[sum(dl ux²), sum(dl ux uy), sum(dl uy²)]`, par segments échantillonnés
   tous les 0,2 mm.
3. Normaliser la longueur totale par la longueur d'un remplissage de référence
   (rangées espacées de 0,4 mm).
4. Pour la longueur normalisée `L` et la cohérence d'orientation `C` :
   `score = clamp(100 * s * (1 - exp(-0.42 L)) * (0.7 + 0.3 C), 0, 100)`.
   `C = hypot(xx - yy, 2 xy) / longueur_totale`, bornée dans [0, 1].
   Les profils `s` sont **arbitraires** : coton tissé 1,00 ;
   jersey 1,45 ; feutrine 0,75.
5. Calculer le maximum, le percentile 99 des cellules contenant du fil,
   et la surface où le score vaut au moins 70.

Cette combinaison distingue certaines orientations et les accumulations
de fil, mais **ce score n'est pas une grandeur physique**.
Un score 80 n'indique pas 80 % de probabilité de plis.
Il n'intègre pas la tension fil/canette, la rigidité ou l'anisotropie
mesurées du tissu, le stabilisateur, le serrage du cadre, la précontrainte,
ni la séquence de libération après décerclage.

## Validation et étapes suivantes

Les tests unitaires vérifient la reproductibilité PNG/JSON, la monotonie
des superpositions, les profils et le refus des paramètres incorrects.
Une action de bureau est testée pour sa présence dans le menu.

Pour parler de **carte de contraintes** ou de **simulation** :
mesurer des coupons brodés (maillage/repères optiques avant/après),
caractériser des tissus et stabilisateurs, calibrer les forces effectives,
puis développer un solveur mécanique 2D et valider son erreur sur un corpus
indépendant. En l'absence de ces étapes, l'interface doit toujours
afficher la mention « estimation non calibrée ».

La fonctionnalité est indépendante de **HP-FAB-001** (Auto Fabric) :
les profils ci-dessus ne règlent ni densité, ni sous-couche, ni compensation,
et ne sont pas persistés dans le projet.
