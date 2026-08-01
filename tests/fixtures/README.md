# Fixtures d'intégration

## `tentabrode.png`

Image complexe fournie par l'utilisateur comme motif de référence fonctionnel.
Elle sert aux tests d'intégration de segmentation, auto-numérisation et satin :
spirale longue, largeurs variables, trous, anneaux, branches fines, concavités
et zones étroites.

- SHA-256 : `07BB7151FB3B2D7E12323DA0100E1272C5F67983E7D76FA9AB8E3EC4D21F7F7D`
- Provenance : fichier utilisateur `tentabrode.png`, ajouté le 2026-08-01 avec
  autorisation explicite pour les tests du projet.

Les tests ne doivent pas dépendre de pixels de rendu UI. Ils peuvent vérifier
des invariants reproductibles sur l'image source : absence de crash, résultats
déterministes, topologie des zones, géométries finies, absence de croisements
globaux et respect des trous.
