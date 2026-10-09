# Glossaire

## Pour débuter

Les mots que vous croisez dans l'application et dans le
[Guide de prise en main](getting-started.md), sans nom de code.

| Terme | Signification |
|---|---|
| Point | Une piqûre d'aiguille. Un motif est une longue suite de points |
| Saut | Déplacement de l'aiguille **sans couture** d'un endroit à un autre |
| Coupe | Le fil est coupé (entre deux objets éloignés, ou à la fin). Réglée par un **seuil** : un déplacement plus long devient une coupe, plus court un simple saut. Le fichier se termine toujours par une coupe finale. **À tester sur votre machine** |
| Point d'arrêt | Quelques petits points qui bloquent le fil avant et après une coupe, pour qu'il ne se défasse pas |
| Changement de couleur | Arrêt de la machine pour passer au fil suivant |
| Tatami | Remplissage d'une surface par rangées parallèles de points |
| Satin | Zigzag serré qui couvre une bande étroite (lettre, bordure) : brillant, mais seulement adapté aux formes allongées |
| Auto-satin | Satin construit **automatiquement** à partir de la seule forme, sans tracer de rails. Expérimental, non validé sur machine ; il refuse les formes qui ne s'y prêtent pas (disque, forme compacte) |
| Squelette | Ligne médiane d'une forme (son « axe »), calculée par l'auto-satin pour savoir dans quelle direction coudre |
| Traversée | Un trait de satin d'un bord de la forme à l'autre ; le satin est une suite de traversées serrées |
| Guide | Repère que vous posez pour orienter les fils d'un satin ou d'un remplissage dans une direction précise |
| Sous-couche | Points posés **sous** le satin ou le tatami pour le stabiliser et le soutenir ; invisibles une fois la couche visible cousue |
| Tirage (compensation de tirage) | Le fil tire sur le tissu et rétrécit les formes ; on élargit légèrement la forme pour compenser |
| Densité | Écart entre deux rangées ou deux traversées : plus il est petit, plus la couture est serrée |
| Région | Zone de couleur d'une image **segmentée** (la segmentation découpe l'image en régions) |
| Région active | La région actuellement **sélectionnée** : une seule à la fois ; les commandes Fusionner, Supprimer, Recolorer et Vectoriser lui sont appliquées |
| Objet vectoriel | Contour éditable (avec ses nœuds) obtenu à partir d'une région |
| Objet de broderie | Intention de couture attachée à une forme : contour cousu, tatami ou satin |
| Numérisation automatique | Création automatique des objets de broderie pour toutes les régions |
| Projet (.osp) | Votre travail complet et éditable. Le DST, lui, ne contient que les points |
| DST | Format de fichier de broderie lu par beaucoup de machines (Tajima) |
| Cadre | Zone de broderie du tambour (rectangle rouge sur le canevas) |

## Termes techniques

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
| Palette | palette | Ensemble de couleurs/fils | catalogue `thread_palette` (données de démonstration), non relié à l'interface |
| Fil | thread | Fil de broderie | `thread_palette::Thread` (non relié à l'interface) |
| Ordre de couture | sewing order | Ordre des objets | `optimization`, `SewingOrder` |
| Micromètre | micrometer | Unité interne (1/1000 mm) | `Micrometers` |
| Déplacement (interne) | travel | Liaison non cousue d'un remplissage | `FillStitch::travel` |
| Squelette | skeleton | Axe médian d'une forme (auto-satin) | `auto_satin` |
| Traversée | crossing | Segment d'un bord à l'autre d'une colonne satin | `skeleton_satin.hpp` |
| Guide | guide | Repère d'orientation des fils | `AutoSatinGuide`, guides de direction |
| Auto-satin | auto-satin | Satin par squelette et traversées orientées | `generate_skeleton_satin` |
| Coupe finale | final trim | Coupe qui termine la séquence | `finish_sequence` |
| Point d'arrêt | lock stitch | Points de verrouillage autour d'une coupe | `LockStitch` |
