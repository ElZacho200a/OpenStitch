# Campagne de validation sur machine — HP-PHYS-001

**État : protocole prêt, aucune validation textile revendiquée.** Les résultats
ne doivent être marqués « mesurés » que lorsque le motif a été brodé et mesuré.

## Préparer la campagne

1. Noter la machine et son firmware, le fil, l'aiguille, l'entoilage et les
   paramètres exacts (densité, compensation, sous-couche, vitesse) du projet.
2. Construire des projets `.osp` dédiés pour chaque famille : satin de largeur
   1, 2, 4, 6, 8 et 10 mm ; tatami d'espacement 0,3/0,4/0,5 mm avec ou sans
   sous-couche de bord ; lettrage de hauteur 3, 5, 8, 12 et 20 mm.
3. Faire les mêmes essais sur trois tissus identifiés : coton stable, jersey
   extensible et éponge. Les 51 cas sont des points de **départ**, pas des
   paramètres « recommandés » ni des tests de performance.
4. Exporter les DST et conserver pour chaque fichier l'empreinte SHA-256,
   le commit OpenStitch et le `.osp` source. Photographier à l'échelle avec
   une règle millimétrée et une référence d'éclairage constante.
5. Mesurer la largeur ou la hauteur réellement cousue, relever les défauts
   (fronces, espaces, accumulation, cassures, déplacement, bord instable),
   photographier recto et verso, inscrire les observations dans le CSV.

## Registre reproductible

Depuis la racine du dépôt :

```sh
python scripts/validation/physical_matrix.py new essais.csv
python scripts/validation/physical_matrix.py check essais.csv
python -m unittest discover -s scripts/validation -p 'test_*.py'
```

`new` refuse d'écraser un fichier existant. `check` vérifie la présence des
51 cas, les colonnes, la provenance complète et les mesures valides.
**Un CSV vide de mesures est valide mais vaut 0/51 réalisé : il ne constitue
aucune preuve de validation.** Chaque ligne remplie doit donner machine,
fil, aiguille, entoilage, commit, empreinte du DST, mesure et photo.

## Critère pour clore HP-PHYS-001

La validation sur machine réclame réellement des mesures avec défauts
quantifiés et photos recto-verso, puis une décision explicitement justifiée
pour chaque réglage par défaut. Tant que ces données n'existent pas,
HP-PHYS-001 demeure **partiel**. Éviter de conclure à une qualité textile
à partir des seules statistiques de points ou d'un rendu sur écran.
