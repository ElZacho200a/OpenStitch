# Diagnostic rapide d'une numérisation décevante

*Référence chargée à la demande par le skill `openstitch-digitize`.*


| Symptôme | Cause probable | Action |
|---|---|---|
| Pièce trop grande / bordure brodée | fond non ignoré ou `--dpi` oublié | SKILL.md (« Regarder l'image », « Le fond ») |
| Mâts et contours fusionnés en blocs sombres | traits fins quantifiés comme des régions | `--dark-lines` |
| Zones de ciel/nuage en lignes pointillées, aspect de grille | tatami sur bandes étroites | satin sur ces bandes (parametres.md) |
| Plus de 3 000 coupes | beaucoup d'objets séparés (satin, traits, îlots) | `--island-cleanup`, monter `--min-region-mm2`, baisser `--detail`, fusionner à la main |
| Interstices entre couleurs voisines | `fill_overlap` = 0 | base sombre sous les couleurs claires (parametres.md, « retouche manuelle »), ou chevauchement dans la boîte de dialogue |
| Beaucoup de changements de fil | `--max-colors` trop haut, quasi-doublons | le baisser |
| Petits points parasites | îlots | `--min-region-mm2`, `--min-feature-mm` |

