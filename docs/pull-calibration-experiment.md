# Compensation de tirage : protocole de calibration experimentale

Prototype HP-ENG-001.a. **Ne modifie pas le moteur de broderie et ne
pretend pas produire de coefficients verifies sur machine.**

1. `python scripts/validation/pull_calibration.py new essais.csv`
2. Broder les echantillons de meme dimension nominale sur chaque support.
3. Completer pour chaque ligne mesuree : machine, stabilizer, thread, needle,
   applied_pull_mm (par extremite) et actual_mm (dimension mesuree dans l'axe
   de la tension du fil). Les lignes non encore brodees restent vides.
4. `python scripts/validation/pull_calibration.py fit essais.csv calibrage.json`
5. Examiner les mesures brutes, dispersion entre repetitions, corrections
   bornees (`clamped`) et eventuelles deformations transversales avant usage.

L'estimation preliminaire est :
`pull_recommande = pull_applique + (nominal - mesure) / 2`.

Cette formule suppose une reponse lineaire et symetrique. Elle ne traite
pas le push, les jonctions de couleurs ni la fatigue textile. Ne pas la
confondre avec un modele physique. Le JSON marque toujours
`validated: false`. L'interpolation est lineaire seulement ENTRE mesures,
constante hors plage.

Pour les tests :
`python -m unittest discover -s scripts/validation -p test_pull_calibration.py`

**Suite** : pieces-test DST automatiques, validation physique,
integration aux profils du projet `.osp`, commandes et UI desktop.
