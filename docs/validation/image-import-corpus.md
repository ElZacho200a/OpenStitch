# Corpus d'import image — HP-QA-006

`scripts/qa_images/image_corpus.py` génère sept petits PNG de test **sans
droits tiers** et sans dépendance Python : RVB 8 bits, RVBA, niveaux de
gris 16 bits, palette indexée, et trois entrées invalides. Les images
valides doivent produire une largeur de 2 px et une hauteur de 1 px dans
`openstitch-cli info --json` ; les invalides doivent être rejetées.

```sh
python -m unittest discover -s scripts/qa_images -p 'test_*.py'
python scripts/qa_images/image_corpus.py --cli build/linux-core/apps/cli/openstitch-cli
```

Deux tests CTest optionnels sont enregistrés si Python 3 est disponible :
la vérification du générateur et le passage au **vrai exécutable CLI**.
Ce test ne valide pas l'import de toutes les images utilisateur ni les
images géantes ; HP-QA-006 reste partiel jusqu'à constitution d'un corpus
réel, avec licences vérifiées et régressions documentées.
