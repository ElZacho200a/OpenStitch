# Glossaire

Terme français, terme anglais utilisé dans le code, définition, et type/classe
associé lorsqu'il existe.

| Français | Anglais (code) | Définition | Type / fichier |
|---|---|---|---|
| Point | stitch | Pénétration d'aiguille cousant le fil | `CommandType::Stitch` |
| Saut | jump | Déplacement de l'aiguille sans couture | `CommandType::Jump` |
| Coupe-fil | trim | Coupe du fil | `CommandType::Trim` |
| Changement de couleur | color change | Arrêt pour changer de fil | `CommandType::ColorChange` |
| Arrêt | stop | Arrêt machine | `CommandType::Stop` |
| Fin | end | Fin du motif | `CommandType::End` |
| Point droit / courant | running stitch | Points le long d'un chemin | `run_stitch` |
| Point triple | bean stitch | Chaque segment cousu 3× | `RepeatMode::BeanStitch` |
| Point arrière | backstitch | Progression avec recouvrement | `RepeatMode::Backstitch` |
| Remplissage | fill | Couture d'une surface | `fill_tatami` |
| Tatami | tatami | Remplissage par rangées parallèles | `TatamiParams` |
| Colonne satin | satin column | Zigzag entre deux rails | `SatinParams`, `fill_satin` |
| Rail | rail | Bord d'une colonne satin | `SatinParams::rail_a/b` |
| Sous-couche | underlay | Couche cousue avant la couche visible | `center_underlay` |
| Compensation de tirage | pull compensation | Élargissement pour compenser la traction | `pull_compensation` |
| Densité | density / row_spacing | Espacement des fils/rangées | `density`, `row_spacing` |
| Longueur de point | stitch length | Longueur d'un point | `stitch_length` |
| Angle | angle | Orientation du remplissage | `Angle` |
| Contour | contour / outer | Bord d'une région | `PathSet::outer` |
| Trou | hole | Contour intérieur | `PathSet::holes` |
| Chemin | path | Polyligne/courbe | `geometry::Path` |
| Région | region | Zone de couleur connexe | `segmentation::Region` |
| Objet vectoriel | vector object | Contours éditables | `document::VectorObject` |
| Objet de broderie | embroidery object | Intention de couture | `document::EmbroideryObject` |
| Séquence de points | stitch sequence | Commandes machine | `stitch::StitchSequence` |
| Cadre / tambour | hoop / canvas | Zone de broderie | `document::Canvas` |
| Palette | palette | Ensemble de couleurs/fils | *(prévu)* |
| Fil | thread | Fil de broderie | *(prévu)* |
| Ordre de couture | sewing order | Ordre des objets | `optimization`, `SewingOrder` |
| Micromètre | micrometer | Unité interne (1/1000 mm) | `Micrometers` |
| Déplacement (interne) | travel | Liaison non cousue d'un remplissage | `FillStitch::travel` |
