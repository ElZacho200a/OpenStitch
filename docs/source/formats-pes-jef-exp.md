# Formats PES, JEF et EXP

Public : utilisateur avancé, développeur, mainteneur. État : **Implémenté, non validé
sur machine réelle** (HP-FMT-002 à 005). Les codecs sont testés en aller-retour et sur des
octets calculés à la main, mais **aucun fichier produit ici n'a été ouvert par une
machine Brother, Janome ou Melco ni par un logiciel tiers** : voir *Limites*.

## Où ça vit

- `libs/formats/src/pes.cpp`, `jef.cpp`, `exp.cpp` : un codec par format, qui ne fait
  que la sérialisation propre au format. Toute la normalisation (découpage des
  déplacements, quantification sans dérive, représentation des coupes) vient de
  `normalize_for_machine` (HP-FMT-001, voir [Format DST](dst-format.md)).
- `libs/formats/src/codec_common.*` : application des options machine, boîte
  englobante, vérification des limites. `machine_palettes.*` : tables de fils.
- Inscription au registre : une entrée par format dans `format_registry.cpp`
  (`pes`, `jef`, `exp` ; DST inchangé). `FormatInfo` gagne `encode_ex` / `decode_ex`
  (avec options et couleurs) et `carries_colors`.
- `formats::check_export_limits` : analyse pré-export propre au format (voir plus bas).
- `project_io::export_machine_file(projet, format, chemin, options)` et
  `import_machine_file` restent génériques ; l'export fournit les couleurs des blocs
  (`stitch_analysis::color_blocks`) aux formats qui en portent, l'import les relit.

## Options machine (`MachineExportOptions`)

| Option | Valeurs | Effet |
|---|---|---|
| `trims` | `Native`, `Drop` | coupe codée à la façon du format, ou remplacée par un saut |
| `stops` | `Native`, `AsColorChange`, `Drop` | arrêt machine propre au format, traité comme un changement de couleur, ou ignoré |
| `color_changes` | `Emit`, `Drop` | changements de couleur écrits ou supprimés (motif monochrome) |
| `block_colors` | RGB par bloc | couleurs réelles (PES, JEF) ; vide = palette par défaut déterministe |
| `design_name` | texte | nom du motif (PES : 16 caractères ASCII) |
| `jef_timestamp` | `AAAAMMJJhhmmss` | horodatage JEF, **fixe par défaut** (sortie octet-exacte) |

Dans la CLI : `osp2dst projet.osp sortie.pes [--no-trims] [--no-color-changes]
[--stops native|as-color-change|drop]` (le format vient de l'extension). Dans le bureau :
**Fichier ▸ Exporter une broderie machine…** ouvre un dialogue (format, coupes, arrêts,
changements de couleur, résumé recalculé), puis le choix du fichier avec l'extension du format.

## EXP (Melco / Bernina)

Flux sans en-tête ni marqueur de fin, paires d'octets signés (dx, dy) en 0,1 mm,
**axe Y vers le haut** (comme le DST). Contrôle : `0x80` suivi d'un code : `0x04` saut
(dx dy), `0x80` coupe (`07 00`), `0x01` changement de couleur ou arrêt (`00 00`).
Delta max ±127 : les grands déplacements sont découpés en sauts. Pas de couleur dans
le fichier (le fichier compagnon `.inf` n'est pas écrit, voir HP-FMT-009). Un arrêt
devient un changement de couleur.

## JEF (Janome)

En-tête de `0x74` octets : décalage des points, `0x14`, horodatage (14 caractères),
nombre de couleurs, nombre de paires d'octets du flux, code de cadre (50x50 = 1,
126x110 = 3, 140x200 = 2, 200x200 = 4, repli 0), étendues du motif (demi-largeur,
demi-hauteur), puis quatre groupes de marges par cadre (110x110, 50x50, 140x200, cadre
personnalisé ; `-1` si le motif dépasse). Table de fils : un indice de la palette
Janome (78 entrées) par bloc, `0` = arrêt machine, puis un mot `0x0D` par entrée.
Flux : `dx dy` signés ; `80 01 dx dy` changement/arrêt ; `80 02 dx dy` saut ; trois
sauts nuls = coupe ; `80 10` fin. **Axe Y vers le haut**, delta max ±127. Le motif est
écrit **centré** sur l'origine machine (premier déplacement = positionnement, retiré
à la relecture). Deux fils différents consécutifs ne reçoivent jamais le même indice.

## PES (Brother / Babylock / Bernette)

Écriture : PES version 1 « complète » : `#PES0001`, décalage du bloc PEC, section PES
(objets `CEmbOne` / `CSewSeg`, sections de points cousus ou de sauts avec l'indice de
fil, journal des couleurs), puis bloc PEC :

- en-tête de 512 octets : `LA:` + nom sur 16, taille des vignettes (6 octets x 38),
  nombre de couleurs - 1, **indices de la palette Brother à 64 entrées** (fil le plus
  proche, CIEDE2000, via `thread_palette`) ;
- bloc de points : `00 00`, longueur 24 bits, `31 FF F0`, largeur, hauteur,
  `0x01E0`, `0x01B0`, puis flux : forme courte (1 octet, 7 bits, |d| <= 63) ou longue
  (2 octets, 12 bits, drapeau saut `0x10`, coupe `0x20`), `FE B0 xx` changement de
  couleur (`xx` alterne 02/01), `FF 00` fin ;
- vignettes 48x38 (une d'ensemble + une par bloc de couleur), 1 bit par pixel, bit de
  poids faible d'abord.

**Axe Y vers le bas** dans le fichier (converti). Un arrêt est un changement de couleur
vers le **même** indice de fil ; à la lecture, un changement vers le même indice que
le précédent est donc relu comme arrêt. Lecture : PES de toute version (le décalage
du bloc PEC est lu dans l'en-tête ; la section PES n'est pas interprétée, les couleurs
viennent de la table PEC). Pas d'écriture PES v6 (couleurs RGB, noms de fils).

## Aller-retour et déterminisme

Sortie octet-exacte d'une exécution à l'autre. Aller-retour **exact** (types et positions
au pas de 0,1 mm) pour les motifs dont les déplacements tiennent dans un enregistrement ;
sinon les sauts intermédiaires du découpage réapparaissent à la relecture et un second
tour redonne les **mêmes octets** (idempotence, testée). La relecture place le point de
départ du motif à l'origine (comme `decode_dst`).

## Analyse pré-export

`formats::check_export_limits(sequence, format, options)` retourne des `ExportIssue`
(Erreur / Avertissement / Information) : plus de 255 blocs de couleur en PES, étendue
au-delà de 3276,7 mm en PES (coordonnées 16 bits) -- ces deux cas sont **bloquants** et
l'encodeur renvoie la même erreur claire ; déplacements qui seront découpés ; cadres
connus (Janome 200 x 200, Brother 360 x 200) ; couleurs perdues en EXP/DST. Le dialogue
d'export les affiche et désactive « Choisir le fichier » en cas d'erreur.

## Sources consultées et licences

Aucun code GPL consulté ni repris (ni Ink/Stitch, ni Embroidermodder). Les structures
des formats ont été recoupées avec **pyembroidery** (licence MIT) : lecture des
modules `PesWriter`, `PecWriter`, `PecReader`, `PecGraphics`, `JefWriter`, `JefReader`,
`ExpWriter`, `ExpReader`, pour *comprendre* le format ; le code est entièrement réécrit
en C++. Les **tables de 64 fils PEC et de 78 fils JEF** (valeurs RGB factuelles) en
sont transcrites : attribution dans `THIRD_PARTY_LICENSES.md`. Les valeurs de nos
tests sont synthétiques, aucun fichier propriétaire.

## Limites

- **Rien n'est validé sur machine réelle ni avec un visualiseur tiers** (critères
  d'acceptation des HP-FMT-002/004/005, HP-PHYS-001) : conventions d'axe Y (PES bas,
  JEF/EXP haut), centrage, octets de la section PES, ordre des bits des vignettes et
  sens exact des codes sont issus de la documentation publique, non vérifiés ici.
- PES v6, noms/marques de fils, fichier `.inf` EXP, profil de cadre (HP-HOOP-001) : absents.
- Le code de cadre JEF est déduit de la taille du motif, pas choisi par l'utilisateur.
- Pas de corpus de fichiers réels ; fuzzing = octets tronqués/altérés/aléatoires
  (aucun plantage), pas encore HP-QA-003.
