# Vérification croisée des fichiers machine — HP-QA-005

État : **outil de comparaison prêt, compatibilité machine non prouvée**.
Cette vérification utilise le lecteur indépendant [pyembroidery](https://github.com/EmbroidePy/pyembroidery)
(licence MIT, installé séparément, **aucun code tiers recopié** dans OpenStitch).

## Mode d'emploi

Après la compilation d'`openstitch-cli` et la création d'un DST/PES/JEF/EXP :

```sh
python -m pip install pyembroidery
python scripts/interop/compare_reader.py --cli /chemin/vers/openstitch-cli motif.dst motif.pes
python -m unittest discover -s scripts/interop -p 'test_*.py'
```

L'outil décode chaque fichier **deux fois** : `openstitch-cli stats --json`
(lecteur du projet), puis `pyembroidery.read` (lecteur indépendant).
Il compare le nombre de points cousus, de sauts, de coupes et de changements
de couleur, et produit un rapport JSON. Le code de sortie vaut `0` pour
une correspondance de tous les compteurs, `1` s'il y a une divergence ou
un fichier non lu. Les différences sont montrées, **jamais corrigées en silence**.

Attention : les commandes machine STOP/coupe et la normalisation des sauts
peuvent être interprétées différemment selon le lecteur. Une divergence
n'implique donc pas nécessairement une erreur de format : l'examiner à partir
du fichier d'origine et documenter le cas dans le corpus. Une correspondance
des compteurs ne prouve ni l'identité point par point ni l'ouverture sur
machine. Pour clore HP-QA-005, ajouter des fichiers de référence réels et
l'exécution de la vérification dans la CI OpenStitch avec le binaire construit.
