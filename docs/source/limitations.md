# Limitations et état des fonctionnalités

Public : tous. Ce chapitre distingue explicitement l'état de chaque
fonctionnalité, vérifié dans le code.

## Tableau des fonctionnalités

| Fonctionnalité | État | Interface | Module | Tests | Limitations |
|---|---|---|---|---|---|
| Import PNG/JPEG/BMP/TIFF | Implémenté | Fichier | image | oui | — |
| Prétraitement non destructif | Implémenté | Image | image | oui | pas de resize |
| Segmentation CIELAB + régions | Implémenté | Segmentation | segmentation | oui | 4-connexité |
| Édition de régions | Implémenté | Segmentation | segmentation | oui | — |
| Vectorisation | Implémenté | Segmentation | vectorization | oui | — |
| Édition de nœuds | Partiel | canevas | document | — | déplacement seul |
| Point droit/double/triple | Implémenté | Broderie | stitch_generation | oui | — |
| Tatami | Implémenté | Broderie | stitch_generation | oui | pas de sous-couche, pas d'underpath caché |
| Satin | Implémenté | Broderie | stitch_generation | oui | pas d'axe médian ni barreaux |
| Auto-numérisation | Implémenté | Segmentation | autodigitize | (intégration) | heuristique de type |
| Ordre de couture | Implémenté | dock | optimization | oui | 2-opt non implémenté |
| Analyse | Implémenté | Analyse | stitch_analysis | oui | pas de carte de densité |
| Simulation | Implémenté | barre | desktop | — | pas de réglage de vitesse |
| Export/Import DST | Implémenté | Fichier/CLI | formats | oui | limites du format |
| Export SVG diagnostic | Implémenté | CLI | formats | oui | — |
| Format projet `.osp` | Implémenté | Fichier | project_io | oui | pas d'autosave/migration |
| Palette de fils | Non implémenté | — | (thread_palette absent) | — | RGB par objet uniquement |
| Édition manuelle des points | Non implémenté | — | — | — | régénération uniquement |
| Remplissages courbe/radial/spirale/motif | Non implémenté | — | — | — | prévus |
| Profils machine/cadres avancés | Non implémenté | — | — | — | cadre fixe 100×100 mm |
| Compensation directionnelle | Partiel | — | — | — | satin uniquement |
| Tâches asynchrones | Non implémenté | — | — | — | traitements synchrones |

## Dette technique connue

- `README.md` mentionne « 121 tests » alors que le compte réel est 139 (jalon
  antérieur) — corrigé dans la présente documentation.
- Le satin et le tatami restent des versions de base ; la refonte du moteur de
  points (documentée dans `docs/stitch-engine-audit.md`) a traité les fondations
  et le running stitch ; satin/tatami avancés restent à reprendre.
- Aucune validation sur machine à broder réelle.

## Statut de validation

Le logiciel est **validé numériquement** (139 tests) et **visuellement** (SVG de
diagnostic, aperçu). Il n'est **pas** validé sur simulateur tiers ni
physiquement. Ne pas considérer les résultats comme prêts pour la production sans
essais machine.

## Implémentation associée

Voir chaque chapitre thématique et l'annexe *Audit du dépôt*.
