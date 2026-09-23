# Roadmap de parité avec Hatch Embroidery

Public : mainteneur, contributeurs, **et agent Claude en mode piloté** (voir le
skill `.claude/skills/openstitch-roadmap/`). Ce fichier liste **tout ce qui rend
OpenStitch Studio moins bon que Hatch Embroidery (Wilcom)** : fonctionnalités
absentes, ergonomie, performance, complétude, distribution, qualité textile.
C'est la **liste de travail de référence** : quand l'agent est piloté sans
consigne précise (« continue », « fais la suite », `/loop`…), il prend la
prochaine entrée de ce fichier.

- Rédigé le 2026-09-22 à partir du code (pas seulement de la doc) : menus de
  `apps/desktop/main_window.cpp`, modèle `libs/document/`, codecs
  `libs/formats/`, commandes `libs/commands/`, `docs/source/limitations.md`.
- Les fonctionnalités de Hatch citées viennent de la documentation publique de
  **Hatch Embroidery 3** (niveaux Personalizer / Composer / Organizer /
  Digitizer). **Vérifier le comportement exact de Hatch avant d'implémenter**
  (manuel en ligne Wilcom/Hatch, vidéos officielles) : ce fichier dit *quoi*
  combler, pas *comment Hatch le code*.
- Contraintes du projet qui priment sur la parité : **Apache-2.0, aucun code
  GPL/AGPL** (ni lié, ni copié), cœur **sans Qt**, coordonnées en µm entières,
  mutation par `ICommand`, points jamais stockés (`effective_sequence`),
  déterminisme. Une fonctionnalité Hatch qui imposerait de violer une de ces
  règles se fait autrement ou pas du tout.

---

## 0. Mode d'emploi

### 0.1 Format d'une entrée

```
### HP-XXX-NNN — Titre [P0|P1|P2|P3] — Statut
- État OpenStitch : ce qui existe aujourd'hui (vérifié dans le code).
- Hatch : ce que Hatch propose.
- À faire : le travail concret.
- Modules : où ça vit (lib cœur d'abord, UI ensuite).
- Dépend de : autres entrées à faire avant.
- Acceptation : critères vérifiables (tests, comportement observable).
```

### 0.2 Priorités

| Priorité | Sens |
|---|---|
| **P0** | Bloquant : sans ça, un utilisateur de Hatch ne peut pas passer à OpenStitch (il ne peut pas broder sur sa machine, ou le flux de travail de base manque). |
| **P1** | Attendu d'un logiciel de numérisation sérieux ; son absence se remarque en quelques minutes. |
| **P2** | Fonction avancée de Hatch Digitizer ; différenciante mais pas bloquante. |
| **P3** | Confort, niche, ou très coûteux pour un gain faible. |

### 0.3 Statuts

`☐ À faire` · `◐ Partiel` (une partie existe, préciser laquelle) · `▶ En cours` ·
`☑ Fait` (avec date + commit) · `✗ Abandonné` (avec la raison, ex. licence).

Quand une entrée passe à `☑ Fait`, **ne pas la supprimer** : ajouter la date,
le commit et une ligne « Livré : … ». L'historique sert à ne pas refaire.

### 0.4 Ordre de traitement (pour l'agent piloté)

1. Les entrées `▶ En cours` d'abord (terminer avant d'ouvrir autre chose).
2. Puis la **vague** la plus basse non terminée (§1), dans l'ordre de la vague.
3. Dans une vague, respecter `Dépend de`.
4. Ne jamais sauter une entrée P0 pour une P1/P2 « plus amusante ».
5. Une entrée trop grosse pour un lot se découpe en sous-entrées
   `HP-XXX-NNN.a`, `.b`… ajoutées **dans ce fichier** avant de commencer.

---

## 1. Vagues de livraison recommandées

Ordre pensé pour qu'à la fin de chaque vague OpenStitch soit utilisable par un
public plus large. Les identifiants renvoient aux sections suivantes.

### Vague 1 — « Je peux broder sur ma machine et travailler normalement »

1. HP-FILE-001 Nouveau projet · HP-FILE-002 Enregistrer / Enregistrer sous ·
   HP-FILE-003 Fichiers récents · HP-FILE-004 Sauvegarde auto + récupération
2. HP-FMT-001 Couche de normalisation machine
3. HP-FMT-002 Export PES · HP-FMT-003 Import PES · HP-FMT-004 JEF · HP-FMT-005 EXP
4. HP-THR-001 → HP-THR-005 Palettes de fils, fil le plus proche, film couleur
5. HP-OBJ-001 Sélection multiple · HP-OBJ-002 Copier/coller · HP-OBJ-003
   Flèches · HP-OBJ-004 Rotation · HP-OBJ-006 Miroir · HP-OBJ-014 Transformer le
   design entier · HP-OBJ-016 Suppression multiple · HP-OBJ-018 Couleur d'objet
6. HP-VEC-002 Tracé ouvert (ligne pour point de contour)
7. HP-STI-004 Satin de bordure à largeur fixe
8. HP-VIEW-001 Rendu réaliste des points
9. HP-PERF-001 Tâches asynchrones · HP-PERF-002 Régénération incrémentale
10. HP-I18N-001 Infrastructure de traduction · HP-I18N-002 Anglais
11. HP-DIST-001 Installateur Windows signé
12. HP-PROD-001 Fiche de production imprimable
13. HP-PHYS-001 Protocole de validation physique

### Vague 2 — « Je peux faire du texte et des designs courants »

1. HP-TXT-001 → HP-TXT-004, HP-TXT-005, HP-TXT-007, HP-TXT-008, HP-TXT-012 → HP-TXT-014
2. HP-ENG-001 Compensation tatami · HP-ENG-002 Sous-couche auto · HP-ENG-010
   Entrée/sortie auto · HP-ENG-008 Longueurs min/max partout
3. HP-AUTO-001 Qualité de l'auto-numérisation · HP-AUTO-003 Clic-pour-broder
4. HP-EDIT-001 → HP-EDIT-004 Édition des points complète
5. HP-SEQ-002 Ordre par glisser-déposer · HP-SEQ-001 2-opt
6. HP-SIM-001 → HP-SIM-004 Simulateur complet
7. HP-HOOP-001, HP-HOOP-002, HP-HOOP-003, HP-HOOP-005, HP-HOOP-006 Cadres et machines
8. HP-VEC-001, HP-VEC-003 → HP-VEC-006, HP-VEC-009, HP-VEC-010, HP-VEC-015
9. HP-OBJ-005, HP-OBJ-008 → HP-OBJ-012, HP-OBJ-015, HP-OBJ-019
10. Le reste des P1 de FILE, UX, VIEW, PROD, HELP.

### Vague 3 — « Je remplace Hatch Digitizer »

Tous les P1 restants, puis les P2 : types de points avancés (motif, contour,
radial, spirale, dégradé, point de croix, stipple, 3D), appliqué, profils
tissus, multi-cadrage, monogrammes, noms d'équipe, formats secondaires,
bibliothèques.

### Vague 4 — Finitions

Les P3.

---

## 2. Formats de fichiers et interopérabilité (FMT)

Aujourd'hui : **DST** (lecture/écriture, `libs/formats/src/dst.cpp`),
**DXF** (sous-ensemble LINE/LWPOLYLINE/CIRCLE/ARC), **SVG** import (formes +
path complet + transformations), **SVG diagnostic** en export, `.osp` natif.
Hatch lit et écrit une vingtaine de formats machine et plusieurs formats
graphiques. C'est **le premier mur** : la majorité des machines domestiques
(Brother, Babylock, Janome, Husqvarna, Pfaff, Bernina) n'utilisent pas DST.

### HP-FMT-001 — Couche de normalisation machine séparée des codecs [P0] — ☐ À faire
- État OpenStitch : la découpe des grands déplacements, la quantification et
  l'encodage des coupes sont faits *dans* l'encodeur DST (`encode_dst`,
  `DstWriteOptions::trim_jumps`). `roadmap.md` le signale déjà (« Séparer la
  normalisation machine de l'encodeur DST »).
- Hatch : un seul moteur produit la séquence ; chaque format n'est qu'un
  encodeur ; les contraintes (pas max, codage des coupes, arrêts couleur)
  dépendent du format **et** de la machine cible.
- À faire : une étape pure `normalize_for_machine(sequence, MachineConstraints)`
  → séquence machine (découpage des sauts/points > pas max du format,
  quantification à la résolution du format sans dérive, représentation des
  Trim selon le format — commande native ou N sauts —, insertion des Stop /
  ColorChange, origine). Les encodeurs deviennent triviaux et testables un à
  un. `MachineConstraints` : résolution, pas max par enregistrement, longueur
  max d'un point cousu, support natif trim/stop, nombre de couleurs max.
- Modules : `libs/formats` (nouvel en-tête `machine.hpp`), consommé par tous
  les codecs.
- Acceptation : l'export DST reste **octet pour octet identique** sur tout le
  corpus de tests existant ; tests unitaires de la normalisation seule (saut de
  50 mm découpé en N enregistrements, dérive nulle cumulée sur 10 000 points).

### HP-FMT-002 — Export PES (Brother / Babylock / Bernette) [P0] — ☐ À faire
- État OpenStitch : absent.
- Hatch : écrit PES (versions 1 à 6+), avec bloc PEC (aperçu monochrome et
  index de couleurs de la palette Brother).
- À faire : encodeur PES v1 (le plus lu par toutes les machines) + bloc PEC
  (en-tête, table des couleurs Brother 64 indices, commandes, vignette 48×38) ;
  puis option v6 (couleurs RGB réelles, noms de fils). Mapping couleur → index
  PEC par fil le plus proche (HP-THR-003). Écrire le codec **d'après la
  documentation publique du format**, jamais en copiant libembroidery (zlib,
  acceptable en licence mais à citer) ni pyembroidery (MIT) sans attribution ;
  **aucun code GPL** (Ink/Stitch, Embroidermodder 1 sont GPL).
- Modules : `libs/formats` (`pes.hpp`), `THIRD_PARTY_LICENSES.md` si une
  documentation/un tableau sous licence est repris.
- Dépend de : HP-FMT-001, HP-THR-001.
- Acceptation : fichiers ouverts sans erreur par au moins deux visualiseurs
  tiers (ex. pyembroidery en script de test hors build, Embrilliance Express
  gratuit, visionneuse Brother) ; aller-retour export→import PES identique au
  pas de 0,1 mm ; test de déterminisme (2 exports = mêmes octets) ; essai sur
  une vraie machine Brother (HP-PHYS-001).

### HP-FMT-003 — Import PES [P0] — ☐ À faire
- État OpenStitch : absent. Seul DST s'importe comme séquence de points.
- Hatch : ouvre PES et récupère les couleurs.
- À faire : décodeur PES/PEC tolérant (même politique que `decode_dst` : ne
  plante jamais, erreur structurée), couleurs réelles récupérées et assignées
  au film couleur.
- Modules : `libs/formats`, `apps/desktop` (menu Fichier ▸ Importer).
- Acceptation : corpus de PES réels (versions variées) importés, fuzzing
  (HP-QA-003) sans crash.

### HP-FMT-004 — JEF / JEF+ (Janome, Elna) lecture + écriture [P0] — ☐ À faire
- État OpenStitch : absent.
- Hatch : oui, avec choix du cadre Janome dans l'en-tête.
- À faire : codec JEF (table de couleurs Janome, code de cadre dans l'en-tête
  — dépend du profil de cadre HP-HOOP-001).
- Modules : `libs/formats`.
- Dépend de : HP-FMT-001, HP-THR-001.
- Acceptation : idem HP-FMT-002.

### HP-FMT-005 — EXP (Melco / Bernina) lecture + écriture [P0] — ☐ À faire
- État OpenStitch : absent.
- Hatch : oui. Format simple, très répandu chez Bernina et en industriel.
- À faire : codec EXP (+ fichier compagnon de couleurs `.inf` optionnel).
- Modules : `libs/formats`.
- Dépend de : HP-FMT-001.

### HP-FMT-006 — VP3 / VIP / HUS (Husqvarna Viking, Pfaff) [P1] — ☐ À faire
- État OpenStitch : absent.
- Hatch : oui.
- À faire : VP3 d'abord (le plus actuel, couleurs et noms de fils inclus),
  HUS/VIP en lecture puis écriture (compression propre au format : vérifier
  qu'une implémentation non GPL est documentée).
- Modules : `libs/formats`.
- Dépend de : HP-FMT-001, HP-THR-001.

### HP-FMT-007 — Formats secondaires (XXX, SEW, PCS/PCM/PCQ, SHV, CSD, DSB/DSZ, U01, TAP, KSM, T01, ZSK) [P2] — ☐ À faire
- État OpenStitch : absents.
- Hatch : la plupart en lecture et écriture.
- À faire : un codec par format, dans l'ordre de popularité réelle (sondage
  utilisateurs ; XXX/Singer et U01/Barudan probablement d'abord). Chaque codec
  = entrée séparée `HP-FMT-007.x` quand on le commence.
- Modules : `libs/formats`.
- Dépend de : HP-FMT-001.

### HP-FMT-008 — ART (Bernina) / EMB (Wilcom) [P3] — ☐ À faire
- État OpenStitch : absents.
- Hatch : EMB est son format natif ; ART est lu.
- À faire : ces formats sont propriétaires, chiffrés/compressés et non
  documentés publiquement. N'implémenter qu'en **lecture**, et seulement si
  une documentation d'interopérabilité légitime existe. Sinon marquer
  `✗ Abandonné (format fermé)`.

### HP-FMT-009 — Couleurs à côté du DST (fichier compagnon, en-tête étendu) [P1] — ☐ À faire
- État OpenStitch : le DST n'emporte que des arrêts couleur ; les couleurs
  réelles sont perdues à l'export.
- Hatch : peut écrire l'ordre des couleurs dans des fichiers d'accompagnement
  et sur la fiche de production.
- À faire : option d'export d'un fichier compagnon (`.inf` Melco, `.thr`,
  ou CSV/TXT lisible) listant les fils dans l'ordre ; relire ce compagnon à
  l'import d'un DST s'il est présent à côté.
- Modules : `libs/formats`.
- Dépend de : HP-THR-001.

### HP-FMT-010 — Reconnaissance d'un fichier de points en objets (« stitch to object ») [P2] — ☐ À faire
- État OpenStitch : un DST importé reste une séquence brute non régénérable ;
  impossible de le redimensionner proprement ou d'en changer la densité.
- Hatch : reconnaît les objets d'un fichier de points (satin, tatami, contour)
  pour les rendre de nouveau éditables et redimensionnables avec recalcul.
- À faire : segmenter la séquence par couleur puis par continuité ;
  classifier les blocs (zigzag régulier → satin, rangées parallèles → tatami,
  suite de points alignés → contour) ; reconstruire rails/contours ; créer des
  `EmbroideryObject` dont les paramètres imitent l'original ; garder le brut en
  repli pour les blocs non reconnus.
- Modules : nouvelle lib `stitch_recognition` (cœur, sans Qt).
- Acceptation : un design OpenStitch exporté en DST puis réimporté est
  reconnu avec ≥ 90 % des points dans des objets du bon type (corpus de test).

### HP-FMT-011 — Export image (PNG/JPG) du rendu brodé [P1] — ☐ À faire
- État OpenStitch : pas d'export image ; seul le SVG diagnostic existe (trait
  noir, pas de couleurs de fil).
- Hatch : export d'une image réaliste pour montrer au client.
- À faire : rendu hors écran du rendu réaliste (HP-VIEW-001) à une résolution
  choisie, fond transparent ou couleur tissu.
- Modules : `apps/desktop` (le rendu Qt), éventuellement un rendu cœur
  simplifié dans `formats` pour la CLI.
- Dépend de : HP-VIEW-001.

### HP-FMT-012 — Export SVG « présentation » (couleurs de fil, épaisseur) [P2] — ☐ À faire
- État OpenStitch : `to_diagnostic_svg` seulement (noir + sauts orange).
- À faire : SVG en couleurs de fil réelles, largeur de trait = épaisseur du fil,
  passes masquables ; utile au web et à la découpe laser des appliqués.
- Modules : `libs/formats`.

### HP-FMT-013 — Import graphique élargi (PDF, EPS, AI, EMF/WMF, CDR) [P2] — ☐ À faire
- État OpenStitch : SVG et DXF seulement pour le vectoriel.
- Hatch : importe les principaux formats vectoriels et les convertit en objets.
- À faire : EMF/WMF (Windows, API native ou parseur maison), PDF/AI (AI récent
  = PDF) via une lib **non GPL** (PDFium, BSD — pas Poppler, pas MuPDF qui est
  AGPL), EPS via conversion (Ghostscript est AGPL → **interdit** en dur ; au
  mieux, appel optionnel d'un outil externe installé par l'utilisateur).
  CDR : librevenge/libcdr sont MPL — vérifier la compatibilité avant.
- Modules : `libs/formats`, `THIRD_PARTY_LICENSES.md`.

### HP-FMT-014 — Import SVG plus complet [P1] — ◐ Partiel
- État OpenStitch : formes, `<path>` complet, `<g>`, transformations.
- À faire : `<text>` (converti en contours via HP-TXT-003), styles CSS et
  attribut `style`, `fill-rule`, `<use>`/`<defs>`/`<symbol>`, `clipPath`,
  unités (`mm`, `in`, `pt`, `viewBox`) pour une taille physique exacte,
  couleurs de remplissage/contour reprises comme couleur de fil et comme choix
  de type (contour SVG → point de contour, remplissage → tatami).
- Modules : `libs/formats/svg_import`.
- Acceptation : un SVG Inkscape de 50 mm s'importe à 50 mm ; les couleurs
  d'origine sont conservées ; tests par cas.

### HP-FMT-015 — DXF plus complet [P2] — ◐ Partiel
- État OpenStitch : LINE, LWPOLYLINE (bulge), CIRCLE, ARC ; unités supposées mm.
- À faire : SPLINE (→ Bézier), POLYLINE/VERTEX historiques, ELLIPSE, INSERT de
  blocs, lecture de `$INSUNITS`, calques DXF → objets séparés.
- Modules : `libs/formats/dxf`.

### HP-FMT-016 — Formats d'image supplémentaires et glisser-déposer [P1] — ☐ À faire
- État OpenStitch : PNG/JPEG/BMP/TIFF/SVG via dialogue seulement.
- À faire : GIF, WebP ; **glisser-déposer** d'un fichier (image, SVG, DXF,
  DST, PES, OSP) sur la fenêtre ; coller une image depuis le presse-papiers.
- Modules : `libs/image` (OpenCV lit déjà WebP selon la config vcpkg),
  `apps/desktop`.

### HP-FMT-017 — Envoi vers la machine / clé USB [P2] — ☐ À faire
- État OpenStitch : export fichier uniquement.
- Hatch : « Envoyer vers USB », et transfert sans fil (Embroidery Connect).
- À faire : action « Envoyer vers une clé USB » (détection des lecteurs
  amovibles, dossier attendu par la marque, format selon le profil machine) ;
  plus tard, protocoles réseau documentés des machines Wi-Fi.
- Modules : `apps/desktop`.
- Dépend de : HP-HOOP-002.

### HP-FMT-018 — Conversion par lots [P2] — ☐ À faire
- Hatch Organizer : convertit un dossier entier vers un autre format.
- À faire : sous-commande CLI `convert` (in → out, format déduit de
  l'extension) puis dialogue desktop.
- Modules : `apps/cli`, `apps/desktop`.

### HP-FMT-019 — Miniatures dans l'Explorateur Windows [P3] — ☐ À faire
- Hatch : installe un fournisseur de miniatures pour ses formats.
- À faire : `IThumbnailProvider` pour `.osp`, `.dst`, `.pes`… (DLL COM
  installée par l'installateur).
- Modules : nouveau `apps/shell_thumbnails`, `packaging/`.

### HP-FMT-020 — `.osp` plus riche (vignette, métadonnées) [P2] — ☐ À faire
- État OpenStitch : JSON + ZIP sans vignette ni métadonnées.
- À faire : vignette PNG dans l'archive (sert à HP-FMT-019, fichiers récents,
  bibliothèque), métadonnées (auteur, client, notes, date, tissu, cadre).
- Modules : `libs/project_io`, `libs/document`.

### HP-FMT-021 — Migrations de schéma `.osp` systématiques [P1] — ◐ Partiel
- État OpenStitch : lecture tolérante `value(clé, défaut)` ; `stitch-feature-gap-audit.md`
  note l'absence de vrai mécanisme de migration.
- À faire : table de migrations versionnées `vN → vN+1` testées une à une ;
  corpus d'anciens `.osp` figés dans `tests/fixtures/osp/` relus à chaque
  build ; refus clair d'un fichier d'une version *future*.
- Modules : `libs/project_io`.

---

## 3. Fils, couleurs et palettes (THR)

Aujourd'hui : un **RGB par objet**, rien d'autre (`palettes-and-threads.md` :
« Prévu / partiel », lib `thread_palette` absente). Hatch est construit autour
des nuanciers de fabricants : c'est ce que l'utilisateur achète en magasin.

### HP-THR-001 — Bibliothèque `thread_palette` [P0] — ☐ À faire
- À faire : lib cœur sans Qt : `Thread { brand, range, code, name, rgb,
  weight }`, `ThreadChart { name, threads }`, chargement depuis des fichiers de
  données (JSON/CSV) embarqués, recherche par code/nom.
- Modules : nouvelle `libs/thread_palette` (`openstitch::thread_palette`),
  tests `tests/unit/thread_palette/`.
- Acceptation : ≥ 1 nuancier chargé et interrogé en test ; déterminisme de
  l'ordre.

### HP-THR-002 — Nuanciers des fabricants [P0] — ☐ À faire
- Hatch : Madeira (Polyneon, Rayon, Classic), Isacord, Robison-Anton,
  Sulky, Gunold, Marathon, Floriani, Coats, Brother, Janome, Pantone approx…
- À faire : fichiers de données par fabricant. **Point licence** : les
  associations code ↔ couleur sont des faits, mais une table recopiée d'un
  produit tiers peut être protégée ; sourcer depuis les cartes publiées par
  les fabricants et documenter la source dans `THIRD_PARTY_LICENSES.md`.
- Modules : `libs/thread_palette/data/`.
- Dépend de : HP-THR-001.

### HP-THR-003 — Fil le plus proche (distance perceptuelle) [P0] — ☐ À faire
- Hatch : « Match » vers le nuancier choisi.
- À faire : conversion sRGB → CIELAB (déjà faite dans `segmentation`, à
  factoriser dans `core` ou `thread_palette`), distance **CIEDE2000**, top-N
  candidats, filtre par gamme possédée (HP-THR-007).
- Modules : `libs/thread_palette`.
- Acceptation : tests contre les valeurs de référence publiées de CIEDE2000
  (jeu de Sharma).

### HP-THR-004 — Fil assigné à chaque objet + remplacer une couleur partout [P0] — ☐ À faire
- État OpenStitch : `EmbroideryObject::rgb` seul ; pas de sélecteur de couleur
  d'objet de broderie dans l'inspecteur (seulement la recoloration de région).
- À faire : `EmbroideryObject` porte une référence de fil optionnelle
  (`ThreadRef`) en plus du RGB d'affichage ; commande `SetObjectThreadCommand`
  (un ou plusieurs objets) ; « remplacer ce fil par… » sur tout le design ;
  sérialisation `.osp` + migration.
- Modules : `libs/document`, `libs/commands`, `libs/project_io`,
  `apps/desktop/properties_panel`.
- Dépend de : HP-THR-001 ; skill `openstitch-stitch-param` pour le champ persistant.

### HP-THR-005 — Film couleur / barre des couleurs du design [P0] — ☐ À faire
- Hatch : barre « Palette de couleurs » (fils du design) + « Color film »
  (liste des blocs de couleur dans l'ordre de couture, vignettes, glisser pour
  réordonner, clic pour sélectionner les objets de cette couleur).
- À faire : dock listant les fils utilisés dans l'ordre de couture (pastille,
  marque, code, nombre de points, longueur), sélection des objets d'un fil,
  masquage d'un fil, réordonnancement par glisser (commande annulable qui
  réordonne les objets en respectant les verrous).
- Modules : `apps/desktop` (nouveau `color_film_panel`), `libs/optimization`
  pour le réordonnancement contraint.
- Dépend de : HP-THR-004.

### HP-THR-006 — Coloris multiples (colorways) [P1] — ☐ À faire
- Hatch : plusieurs jeux de couleurs pour un même design, bascule instantanée.
- À faire : `Project::colorways` (liste de tables fil-logique → fil réel),
  coloris actif, export dans le coloris actif, fiche de production par coloris.
- Modules : `libs/document`, `libs/project_io`, `apps/desktop`.
- Dépend de : HP-THR-004.

### HP-THR-007 — Mes fils (stock possédé) et nuancier personnel [P1] — ☐ À faire
- À faire : l'utilisateur coche les bobines qu'il possède ; le « plus proche »
  peut se limiter à son stock ; nuancier personnel éditable ; import/export
  CSV et formats courants de nuanciers (`.thr`, `.rgb`, `.edr`, `.gpl`) si
  documentés.
- Modules : `libs/thread_palette`, préférences `apps/desktop`.

### HP-THR-008 — Couleur / texture du tissu de fond [P1] — ☐ À faire
- Hatch : couleur de fond réglable (et produits en arrière-plan, HP-VIEW-004).
- À faire : couleur de tissu par projet (persistée), utilisée par le rendu et
  par l'analyse (fil trop proche du tissu = avertissement).
- Modules : `libs/document`, `apps/desktop/canvas_view`.

### HP-THR-009 — Poids / type de fil (40, 60 wt, métallique) [P2] — ☐ À faire
- À faire : le poids du fil influence l'épaisseur de rendu et la densité
  recommandée (fil 60 → densité plus serrée) ; métallique → longueur mini
  plus grande, avertissement de vitesse.
- Modules : `libs/thread_palette`, `libs/stitch_generation` (défauts),
  `apps/desktop`.

### HP-THR-010 — Aiguilles des machines multi-têtes [P2] — ☐ À faire
- À faire : table fil ↔ numéro d'aiguille, ordre d'aiguilles exporté (DST
  n'a que des arrêts ; certaines machines lisent une séquence d'aiguilles),
  réutilisation d'une aiguille déjà enfilée au lieu d'un nouvel arrêt.
- Modules : `libs/document`, `libs/formats`.
- Dépend de : HP-HOOP-002.

### HP-THR-011 — Réduire / fusionner les couleurs d'un design [P1] — ☐ À faire
- État OpenStitch : la quantification existe au niveau **image** seulement.
- À faire : au niveau objets : fusionner deux fils, « réduire à N couleurs »
  (regroupement CIELAB des fils des objets), avec aperçu et une seule commande
  annulable.
- Modules : `libs/thread_palette`, `libs/commands`.

### HP-THR-012 — Pipette (couleur depuis l'image ou un objet) [P1] — ☐ À faire
- À faire : outil pipette : clic sur l'image → fil le plus proche proposé ;
  clic sur un objet → reprendre son fil.
- Modules : `apps/desktop`.
- Dépend de : HP-THR-003.

---

## 4. Lettrage et texte (TXT)

Aujourd'hui : **aucun objet texte**. Le lettrage est une des premières raisons
d'acheter Hatch (même le niveau Personalizer), avec des dizaines de polices
prénumérisées et la conversion des polices TrueType.

### HP-TXT-001 — Objet texte dans le modèle [P0] — ☐ À faire
- À faire : `TextObject` (ou alternative de `StitchParams` / nouveau type
  d'objet) : chaîne UTF-8, police, hauteur de capitale en mm, espacements,
  base (HP-TXT-005), alignement, type de point par défaut ; les lettres
  restent **régénérées** depuis ces paramètres (ADR-014), jamais figées.
  Commandes : créer, éditer le texte, éditer les paramètres. `.osp` + migration.
- Modules : `libs/document`, `libs/commands`, `libs/project_io`.
- Acceptation : créer/éditer/annuler un texte ; aller-retour `.osp`.

### HP-TXT-002 — Format et bibliothèque de polices de broderie natives [P1] — ☐ À faire
- Hatch : polices **prénumérisées** (satin propre à chaque lettre, ordre et
  connecteurs optimisés), bien meilleures que la conversion TrueType aux
  petites tailles.
- À faire : format ouvert de police de broderie (par lettre : objets satin/
  tatami/contour paramétriques, métriques, crénage) ; outil pour en créer
  (numériser chaque glyphe dans OpenStitch et l'enregistrer) ; un premier jeu
  de polices sous licence libre (OFL) numérisées par la communauté.
- Modules : `libs/lettering` (nouvelle lib cœur), `docs/`.
- Dépend de : HP-TXT-001.

### HP-TXT-003 — Conversion de polices TrueType/OpenType [P0] — ☐ À faire
- Hatch : convertit n'importe quelle police installée.
- À faire : lecture des contours de glyphes via **FreeType** (licence FTL,
  compatible — **pas l'option GPLv2**) et mise en forme via **HarfBuzz** (MIT)
  pour le crénage/ligatures ; contours → `geometry::Path` en µm ; énumération
  des polices système (côté desktop seulement, cœur agnostique).
- Modules : `libs/lettering` (FreeType/HarfBuzz encapsulés, jamais exposés),
  `vcpkg.json`, `THIRD_PARTY_LICENSES.md`.
- Dépend de : HP-TXT-001.

### HP-TXT-004 — Lettrage satin automatique par lettre [P0] — ☐ À faire
- Hatch : une lettre TrueType devient des colonnes satin qui suivent les
  traits, pas un tatami.
- À faire : réutiliser `auto_satin` / `satin_planning` sur chaque glyphe
  (formes fines idéales) avec repli tatami pour les empattements larges et
  repli contour pour les très petites tailles ; ordre des sections et
  connecteurs internes à la lettre.
- Modules : `libs/lettering`, `libs/auto_satin`.
- Dépend de : HP-TXT-003. Lire `docs/source/satin.md` avant.
- Acceptation : un alphabet complet (A–Z, a–z, 0–9, accents FR) dans une
  police sans empattement donne 100 % de lettres cousues sans zone oubliée ni
  débordement (métriques des tests satin), SVG dorés.

### HP-TXT-005 — Bases de texte (ligne, arc, cercle, chemin) [P1] — ☐ À faire
- Hatch : ligne droite, arc concave/convexe, cercle complet (texte en haut et
  en bas), ligne verticale, texte sur un chemin quelconque.
- À faire : placement de chaque glyphe par abscisse curviligne sur la base,
  rotation selon la tangente ; poignées sur le canevas pour le rayon/l'angle.
- Modules : `libs/lettering`, `apps/desktop`.

### HP-TXT-006 — Enveloppes / déformations de texte [P2] — ☐ À faire
- Hatch : arche, vague, bombé, trapèze, déformation libre.
- À faire : transformations non linéaires appliquées aux contours avant
  génération.
- Modules : `libs/lettering`, `libs/geometry`.

### HP-TXT-007 — Espacement, crénage, alignement, interligne [P1] — ☐ À faire
- À faire : espacement des lettres / des mots, interligne, alignement
  gauche/centre/droite/justifié, texte multilignes, crénage automatique
  (police) + manuel par paire.
- Modules : `libs/lettering`, inspecteur.

### HP-TXT-008 — Édition individuelle d'une lettre [P1] — ☐ À faire
- Hatch : sélectionner une lettre d'un texte pour la déplacer, tourner,
  redimensionner, recolorer sans casser l'objet texte.
- À faire : décalages par lettre stockés dans l'objet texte (deltas, comme
  les overrides de points), poignées dédiées.
- Modules : `libs/lettering`, `libs/commands`, `apps/desktop`.

### HP-TXT-009 — Avertissement taille minimale [P1] — ☐ À faire
- À faire : règle d'analyse « texte trop petit » (hauteur < ~5 mm en satin,
  trait < 1 mm), suggestion de police adaptée aux petites tailles.
- Modules : `libs/stitch_analysis`.
- Dépend de : HP-TXT-001.

### HP-TXT-010 — Monogrammes [P2] — ☐ À faire
- Hatch : gabarits de monogrammes (2–3 lettres, grande centrale), ornements et
  bordures.
- À faire : gabarits paramétriques + bibliothèque d'ornements (HP-LIB-002).
- Modules : `libs/lettering`, `apps/desktop`.

### HP-TXT-011 — Noms d'équipe / publipostage [P2] — ☐ À faire
- Hatch : liste de noms → un design par nom (ou tous dans un fichier),
  export en lot.
- À faire : champ variable dans un texte, liste importée (CSV), export en lot
  d'un fichier machine par ligne.
- Modules : `libs/lettering`, `apps/cli`, `apps/desktop`.

### HP-TXT-012 — Unicode complet et langues [P1] — ☐ À faire
- À faire : accents, ligatures, écritures non latines (cyrillique, grec),
  RTL (arabe, hébreu) via HarfBuzz ; gestion des glyphes absents (repli et
  avertissement, jamais un carré silencieux).
- Modules : `libs/lettering`.

### HP-TXT-013 — Réglages adaptés aux petites lettres [P1] — ☐ À faire
- À faire : sous la hauteur X : pas de sous-couche ou sous-couche centrale
  seulement, densité ajustée, compensation réduite, satin → contour sous le
  seuil de largeur ; règles documentées et testées.
- Modules : `libs/lettering`, `libs/stitch_generation`.

### HP-TXT-014 — Connecteurs entre lettres [P1] — ☐ À faire
- À faire : ordre de gauche à droite, fin d'une lettre près du début de la
  suivante, trajet caché si possible sinon saut + coupe selon le seuil,
  réglage « couper entre les lettres » / « entre les mots ».
- Modules : `libs/lettering`, `libs/stitch_generation` (routage).

### HP-TXT-015 — Import de polices de broderie tierces [P3] — ☐ À faire
- À faire : formats de polices des autres logiciels, s'ils sont documentés
  (BX Embrilliance est propriétaire → probablement `✗`).

---

## 5. Types de points et remplissages (STI)

Aujourd'hui : **point droit simple/double/triple**, **tatami** (scanline,
sous-couches, underpath caché), **satin** à rails (barreaux, sous-couches,
compensation, finitions, locks, routage multi-colonnes), **remplissage
directionnel** (guides, ruptures, aspect fait main). Hatch en propose beaucoup
plus.

### HP-STI-001 — Point arrière, point tige, point « bean » [P1] — ◐ Partiel
- État OpenStitch : simple / aller-retour / triple (`RunningStitchParams::repeats`).
- Hatch : Backstitch, Stemstitch, Bean (triple), Single/Triple run.
- À faire : point arrière (chaque point revient d'une fraction en arrière),
  point tige (points décalés latéralement, aspect torsadé) comme variantes de
  `RunningStitchParams` (enum de style).
- Modules : `libs/stitch_generation/running_stitch`, `libs/document`.

### HP-STI-002 — Motif le long d'un chemin (motif run) [P1] — ☐ À faire
- Hatch : bibliothèque de motifs (feuilles, cœurs, vagues…) répétés le long
  d'un tracé.
- À faire : motif = petite séquence de points normalisée ; répétition par
  abscisse curviligne, orientation tangente, taille/espacement réglables ;
  bibliothèque de base.
- Modules : `libs/stitch_generation` (`motif_run`), `libs/document`.

### HP-STI-003 — Point feston (blanket / E-stitch) et zigzag simple [P1] — ☐ À faire
- Hatch : Blanket, E-stitch (appliqué), zigzag.
- À faire : générateurs paramétrés (largeur, espacement, côté) le long d'un
  chemin.
- Modules : `libs/stitch_generation`.

### HP-STI-004 — Satin de bordure à largeur fixe le long d'un chemin [P0] — ☐ À faire
- État OpenStitch : le satin exige deux rails ou un contour fermé découpé ;
  pas de « trait satin » de largeur constante sur un tracé ouvert.
- Hatch : Satin Outline / Column A à largeur fixe : l'outil le plus utilisé
  pour les contours, les bordures d'écussons, les tiges.
- À faire : `SatinParams` généré depuis un **chemin central + largeur**
  (rails = offsets ±w/2 du chemin, barreaux perpendiculaires), largeur
  éventuellement variable par nœud ; coins vifs gérés (HP-ENG-005).
- Modules : `libs/stitch_generation`, `libs/geometry` (offset de polyligne
  ouverte), `libs/document`, `apps/desktop` (créer depuis un tracé ouvert).
- Dépend de : HP-VEC-002.
- Acceptation : un cercle et un tracé en S donnent un satin régulier sans
  croisement (métriques de `test_satin_pairing_metrics.cpp`).

### HP-STI-005 — Motifs de tatami (phases, programmes) [P1] — ◐ Partiel
- État OpenStitch : `stagger` (répétition de phase) seulement.
- Hatch : nombreux motifs de tatami (décalages, « Program Split » : motifs
  dessinés par les pénétrations).
- À faire : table de décalages par rangée (motif de phase quelconque),
  bibliothèque de motifs, motif « programme » défini par une petite grille.
- Modules : `libs/stitch_generation/tatami`.

### HP-STI-006 — Remplissage à motif (motif fill) [P1] — ☐ À faire
- Hatch : pavage de la zone par un motif 2D répété (étoiles, écailles…).
- À faire : pavage de motifs découpés au contour, chemins reliés par le
  routage existant.
- Modules : `libs/stitch_generation`.
- Dépend de : HP-STI-002 (format de motif).

### HP-STI-007 — Remplissage concentrique / contour (echo) [P1] — ☐ À faire
- Hatch : Contour fill (lignes qui suivent le bord).
- À faire : offsets successifs de Clipper2 (déjà encapsulé dans `geometry`),
  liaison en spirale des anneaux, trous gérés.
- Modules : `libs/stitch_generation`, `libs/geometry`.

### HP-STI-008 — Remplissage radial [P2] — ☐ À faire
- À faire : rayons depuis un centre déplaçable, densité constante à la
  périphérie (points raccourcis vers le centre).
- Modules : `libs/stitch_generation`.

### HP-STI-009 — Remplissage en spirale [P2] — ☐ À faire
- À faire : spirale continue depuis un centre, sans coupe.
- Modules : `libs/stitch_generation`.

### HP-STI-010 — Remplissage en dégradé (densité variable) [P1] — ☐ À faire
- Hatch : Gradient fill : densité qui varie dans la forme (transparence).
- À faire : profil de densité le long d'un axe (points de contrôle), tatami à
  espacement variable ; option satin à densité variable.
- Modules : `libs/stitch_generation`.

### HP-STI-011 — Mélange de couleurs (color blending) [P2] — ☐ À faire
- Hatch : deux couches de dégradés inverses qui se mélangent.
- À faire : couple d'objets liés à dégradés complémentaires générés ensemble.
- Dépend de : HP-STI-010.

### HP-STI-012 — Point de croix [P2] — ☐ À faire
- Hatch : outil point de croix sur grille (Hatch intègre un module Cross Stitch).
- À faire : grille, cellules → croix (ordre des jambes cohérent), import d'une
  image pixélisée en grille, fil par cellule.
- Modules : `libs/stitch_generation`, `apps/desktop`.

### HP-STI-013 — Stipple / méandre (quilting) [P2] — ☐ À faire
- À faire : chemin aléatoire (graine fixe) qui remplit une zone sans se croiser
  (courbe de remplissage type Hilbert ou TSP-art), espacement réglable.
- Modules : `libs/stitch_generation`.

### HP-STI-014 — Remplissage croisé (crosshatch) [P2] — ☐ À faire
- À faire : deux (ou trois) tatamis très espacés à angles différents, un seul
  objet.
- Modules : `libs/stitch_generation`.

### HP-STI-015 — Satin en relief / broderie 3D sur mousse (puff) [P2] — ☐ À faire
- Hatch : Raised satin, réglages pour mousse 3D (densité élevée, pas de
  sous-couche, bouts fermés, arrêt pour poser la mousse).
- À faire : préréglage satin « 3D » + Stop machine inséré avant l'objet +
  terminaisons qui coupent la mousse.
- Modules : `libs/stitch_generation`, `libs/document`.

### HP-STI-016 — Trapunto [P3] — ☐ À faire
- Hatch : zones surélevées par contours à l'intérieur d'un remplissage.

### HP-STI-017 — PhotoStitch (photo en lignes) [P2] — ☐ À faire
- Hatch : PhotoFlash / Color PhotoStitch : une photo devient des lignes de
  points de densité variable.
- À faire : trame par lignes dont la densité suit la luminance (mono), puis
  version couleur par couches.
- Modules : `libs/autodigitize` ou nouveau `libs/photostitch`.

### HP-STI-018 — Satin et tatami « tournants » [P1] — ◐ Partiel
- État OpenStitch : satin à barreaux (angle variable) ✓ ; remplissage
  directionnel (champ de directions) ✓ ; **pas** de satin automatique sur les
  formes rondes/larges (cercle plein refusé par l'auto-satin).
- Hatch : Turning satin / Complex turning sur formes larges et arrondies.
- À faire : proposer automatiquement le remplissage directionnel pour les
  formes refusées par le satin au lieu d'un tatami à angle fixe ; valider sur
  cercle, disque, pétale.
- Modules : `libs/autodigitize`, `libs/stitch_generation`.

### HP-STI-019 — Broderie à main levée (freehand) [P2] — ☐ À faire
- Hatch : dessiner directement un point de contour, triple, satin à la souris
  ou au stylet (pression → largeur).
- À faire : outil qui crée directement un objet de broderie depuis un tracé
  lissé (réutilise `freeform_path`), pression tablette → largeur de satin.
- Modules : `apps/desktop`, `libs/stitch_generation`.

### HP-STI-020 — Point de chaînette [P3] — ☐ À faire
- Seulement pour machines chenille/chaînette ; faible priorité.

### HP-STI-021 — Bordures prédéfinies [P2] — ☐ À faire
- Hatch : bibliothèque de bordures (satin, motifs, feston) autour d'une forme.
- Dépend de : HP-STI-002, HP-STI-004, HP-LIB-002.

### HP-STI-022 — Bord irrégulier (fourrure, herbe) [P2] — ☐ À faire
- Hatch : « Jagged edge » : un côté du remplissage à longueurs aléatoires.
- À faire : option tatami/satin avec longueur de pénétration aléatoire (graine
  fixe) côté A, B ou les deux.
- Modules : `libs/stitch_generation`.

### HP-STI-023 — Satin à motifs (satin spécial) [P3] — ☐ À faire
- À faire : satin dont les pénétrations dessinent un motif (pas uniquement
  split).

### HP-STI-024 — Satin complexe avec trous [P2] — ☐ À faire
- État OpenStitch : l'auto-satin refuse les anneaux larges / formes à trous
  complexes (anneau fin géré en 4 sections).
- Hatch : Column C / Complex fill contournant les trous.
- Modules : `libs/auto_satin`, `libs/satin_planning`. Lire `satin.md`.

---

## 6. Qualité du moteur et réglages textiles (ENG)

Hatch applique automatiquement des règles de métier (« stitch processing ») que
l'utilisateur ne voit pas. OpenStitch a les briques mais peu d'automatismes.

### HP-ENG-001 — Compensation d'étirement du tatami [P0] — ☐ À faire
- État OpenStitch : compensation **satin seulement** (`limitations.md`,
  « Compensation directionnelle : Partiel ») ; le tatami a un `inset`, qui fait
  l'inverse (rentre le bord).
- Hatch : pull compensation sur tous les remplissages, dans la direction du fil.
- À faire : élargir la zone **dans l'axe des rangées** (offset anisotrope :
  les extrémités de rangées dépassent de `pull` mm), réglable par objet ;
  même chose pour le directionnel.
- Modules : `libs/stitch_generation/tatami`, `directional_fill`, `libs/document`.
- Acceptation : test géométrique (la rangée dépasse du contour de exactement
  `pull` ± 0,1 mm) ; validation physique HP-PHYS-001.

### HP-ENG-002 — Sous-couche automatique selon la forme [P0] — ☐ À faire
- État OpenStitch : sous-couches désactivées par défaut (tatami) ou booléennes
  (satin) ; l'utilisateur doit savoir quoi cocher.
- Hatch : sous-couche choisie automatiquement selon la largeur/le type
  (satin fin : centre ; moyen : bord ; large : bord + zigzag ; tatami : bord +
  parallèle).
- À faire : mode « Auto » (défaut pour les nouveaux objets) qui choisit selon
  des seuils documentés et testés ; les anciens projets gardent leurs réglages.
- Modules : `libs/stitch_generation`, `libs/document` (enum Auto/Manuel).

### HP-ENG-003 — Espacement satin automatique selon la largeur [P1] — ☐ À faire
- Hatch : Auto spacing : colonnes étroites plus lâches, larges plus serrées.
- Modules : `libs/stitch_generation/satin`.

### HP-ENG-004 — Découpe automatique des satins trop larges [P1] — ◐ Partiel
- État OpenStitch : split stitch disponible mais désactivé par défaut ;
  avertissement > 9 mm.
- Hatch : Auto split automatique au-delà de la longueur max.
- À faire : split automatique par défaut au-delà de `max_stitch_length`.

### HP-ENG-005 — Coins satin intelligents [P1] — ◐ Partiel
- État OpenStitch : `ShortStitchMode` dans les virages ; `satin.md` note que
  le coude C0 franc (sans congé) n'est pas traité (« mitre/congé hors
  périmètre »).
- Hatch : Smart corners : coins en onglet, en chevauchement ou en capuchon.
- À faire : détection d'angle vif sur le chemin/les rails, trois stratégies
  (onglet, chevauchement, coupe), réglables.
- Modules : `libs/stitch_generation/satin`. Lire `satin.md`.

### HP-ENG-006 — Points courts du tatami près des bords [P2] — ☐ À faire
- À faire : éviter les pénétrations trop rapprochées en bord de zone
  (fusion/redistribution comme en satin).

### HP-ENG-007 — Densité en unités usuelles [P2] — ☐ À faire
- À faire : afficher la densité aussi en « lignes/cm » ou « points/pouce »
  selon les préférences ; même valeur stockée en µm.
- Modules : `apps/desktop`.

### HP-ENG-008 — Longueurs min/max appliquées à tous les générateurs [P0] — ◐ Partiel
- État OpenStitch : filtre des points trop courts dans les finitions (Lot F),
  `max_stitch_length` satin ; l'analyse signale > 7 mm.
- À faire : garantie globale (post-génération) : aucun point cousu > max
  machine (découpage), aucun < min (fusion), pour **tous** les types ;
  invariant testé sur tout le corpus.
- Modules : `libs/stitch_generation/finish`.

### HP-ENG-009 — Satin sur formes rondes et larges [P1] — ☐ À faire
- Voir HP-STI-018 ; ici côté qualité : ne plus jamais tomber sur un tatami
  à angle fixe pour un disque de 15 mm si un directionnel/satin tournant fait
  mieux (mesure : régularité de la direction des fils).

### HP-ENG-010 — Entrée et sortie automatiques au plus proche [P0] — ◐ Partiel
- État OpenStitch : points d'entrée/sortie réglables (satin, tatami) ;
  routage multi-colonnes satin ; ordre des objets par centres (pas par
  extrémités).
- Hatch : Auto start & end / Closest join : chaque objet commence au point le
  plus proche de la fin du précédent et finit près du début du suivant.
- À faire : après l'ordre, calcul des points d'entrée/sortie de chaque objet
  en fonction des voisins (tatami, directionnel, contour fermé : entrée sur le
  bord le plus proche) ; coût d'ordre basé sur ces points, pas les centres.
- Modules : `libs/optimization`, `libs/stitch_generation`.
- Acceptation : longueur totale des sauts réduite sur le corpus « marine » et
  `tentabrode` (mesurée, rapportée dans le commit).

### HP-ENG-011 — Ne pas coudre sous les objets qui recouvrent [P1] — ☐ À faire
- Hatch : Remove overlaps : la partie d'un remplissage cachée par un objet
  cousu au-dessus n'est pas cousue (moins de points, moins d'épaisseur).
- À faire : soustraction booléenne (Clipper2) des objets suivants avec une
  marge de recouvrement ; option par objet ; recalcul automatique.
- Modules : `libs/geometry`, `libs/stitch_generation`.

### HP-ENG-012 — Qualité générale du tatami [P1] — ◐ Partiel
- À faire : audit visuel/physique du tatami (bords, rangées orphelines,
  sauts restants) contre un résultat Hatch sur les mêmes formes ; corriger
  chaque défaut par la méthode `openstitch-bugfix`.

### HP-ENG-013 — Styles / préréglages de points [P1] — ☐ À faire
- Hatch : styles réutilisables (jeu de paramètres nommé) et valeurs par
  défaut par type.
- À faire : bibliothèque de préréglages (« satin lettrage fin », « tatami
  serviette éponge »…), appliquer à la sélection, définir comme défaut.
- Modules : `libs/document`, `apps/desktop`.

### HP-ENG-014 — Verrous d'entrée/sortie réglables par objet [P2] — ◐ Partiel
- État OpenStitch : réglage global (`SequenceFinishing`) + satin propre.
- À faire : surcharge par objet (tous types).

### HP-ENG-015 — Déplacements cousus le long des bords [P1] — ◐ Partiel
- État OpenStitch : underpath caché (tatami, satin, directionnel).
- Hatch : Travel on edges : déplacements cousus le long du contour vers le
  prochain départ au lieu d'un saut.
- À faire : trajet le long du bord (sous la future couche) quand aucun
  underpath intérieur n'est valide.

### HP-ENG-016 — Redimensionnement d'un fichier de points importé [P2] — ☐ À faire
- État OpenStitch : les objets se régénèrent (avantage) mais un DST/PES
  importé ne peut pas être redimensionné en gardant la densité.
- À faire : mise à l'échelle avec recalcul (ajout/suppression de pénétrations
  dans les blocs reconnus), sinon avertissement au-delà de ±10 %.
- Dépend de : HP-FMT-010.

---

## 7. Numérisation automatique et assistée (AUTO)

Aujourd'hui : auto-numérisation complète (segmentation CIELAB → régions →
vecteurs → satin/tatami), classification **expérimentale**, segmentation IA
SAM via un worker **WSL**. ~18 s sur l'image de référence.

### HP-AUTO-001 — Qualité de l'auto-numérisation comparable à Hatch [P0] — ◐ Partiel
- À faire : jeu de référence (10–20 images variées : logo, dessin au trait,
  mascotte, texte, photo simple) numérisé dans Hatch et dans OpenStitch ;
  métriques (points, coupes, sauts, zones oubliées, débordements, satin vs
  tatami) ; boucle d'amélioration mesurée ; retirer l'étiquette
  « Expérimental » quand les métriques sont atteintes.
- Modules : `libs/autodigitize`, `tests/fixtures/`, `docs/source/auto-numerisation.md`.
- Dépend de : HP-PHYS-002 (même jeu).

### HP-AUTO-002 — Numérisation instantanée en un clic [P1] — ◐ Partiel
- État OpenStitch : « Numérisation automatique » existe mais demande une
  segmentation préalable et des réglages.
- Hatch : Instant Auto-Digitize : image → broderie sans question.
- À faire : action unique qui enchaîne les étapes avec des défauts choisis
  automatiquement (nombre de couleurs estimé, fond détecté), en tâche de fond
  avec progression (HP-PERF-001).

### HP-AUTO-003 — Clic-pour-broder / baguette magique [P0] — ☐ À faire
- Hatch : Click-to-Stitch : un clic sur une zone de couleur de l'image crée
  l'objet (remplissage, satin ou contour).
- À faire : outil : clic → région par croissance (tolérance réglable) → vecteur
  → objet du type choisi (ou auto) en une commande annulable ; variante
  « contour seulement ».
- Modules : `libs/segmentation` (croissance locale), `apps/desktop`.

### HP-AUTO-004 — Vectorisation par courbes [P1] — ☐ À faire
- État OpenStitch : la vectorisation produit des polygones (Douglas-Peucker),
  pas de courbes de Bézier ; les formes gardent des facettes et beaucoup de
  nœuds à éditer.
- À faire : ajustement de courbes cubiques (algorithme de Schneider /
  type Potrace — **Potrace est GPL : réimplémenter d'après la publication**,
  ne pas l'utiliser), détection des coins.
- Modules : `libs/vectorization`.
- Acceptation : nombre de nœuds divisé par ≥ 3 à erreur de contour égale.

### HP-AUTO-005 — Détection des contours noirs (outlines) [P1] — ☐ À faire
- À faire : les traits noirs fins d'un dessin (contours de coloriage)
  deviennent des objets contour/satin **par-dessus** les remplissages, au lieu
  de régions noires tatamisées.
- Modules : `libs/autodigitize`, `libs/auto_satin`.

### HP-AUTO-006 — Suppression du fond [P1] — ◐ Partiel
- État OpenStitch : option « ignorer le fond » à la numérisation.
- À faire : détection automatique du fond (couleur dominante au bord),
  transparence PNG respectée, choix « ne pas broder cette couleur » par couleur.

### HP-AUTO-007 — Fusion automatique des couleurs proches [P1] — ☐ À faire
- Voir HP-THR-011, déclenché à la fin de l'auto-numérisation.

### HP-AUTO-008 — Nettoyage des micro-détails [P2] — ◐ Partiel
- État OpenStitch : taille min de région.
- À faire : absorption des petites régions dans leur voisine dominante,
  lissage des bords en escalier.

### HP-AUTO-009 — Choix du type satin / tatami fiable [P0] — ◐ Partiel
- État OpenStitch : moteur topologique par défaut, refus → tatami ; étiqueté
  expérimental.
- À faire : fait partie de HP-AUTO-001 ; métrique spécifique : taux de bandes
  fines cousues en satin, zéro satin > largeur max.

### HP-AUTO-010 — Suppression des recouvrements après numérisation [P1] — ☐ À faire
- Voir HP-ENG-011, appliqué automatiquement aux objets auto-numérisés.

### HP-AUTO-011 — Segmentation IA sans WSL [P1] — ☐ À faire
- État OpenStitch : SAM passe par un worker Python dans WSL
  (`sam_worker_client`, `wsl_path_converter`) : installation hors de portée
  d'un utilisateur ordinaire.
- À faire : exécution native Windows (ONNX Runtime, MIT) du modèle, téléchargé
  à la demande ; plus de dépendance WSL/Python pour l'utilisateur final.
- Modules : `libs/ai_segmentation`, `apps/desktop`, `packaging/`.

### HP-AUTO-012 — Numériser automatiquement un graphisme vectoriel [P1] — ☐ À faire
- Hatch : Convert graphics to embroidery : SVG/AI importé → objets de broderie
  avec types choisis automatiquement.
- À faire : après import SVG, appliquer la classification satin/tatami/contour
  aux formes vectorielles (sans passer par l'image).
- Modules : `libs/autodigitize`.

---

## 8. Dessin et édition vectorielle (VEC)

Aujourd'hui : rectangle, ellipse, polygone, polygone régulier, Bézier, forme
libre, colonne satin manuelle, ligne de coupe satin ; édition de nœuds
(déplacer, type de nœud, tangentes, suppression) ; dupliquer ; décaler.

### HP-VEC-001 — Insertion de nœuds sur un objet vectoriel [P1] — ◐ Partiel
- État OpenStitch : `MoveNodeCommand`, `SetNodeHandleCommand`,
  `SetNodeTypeCommand`, `RemoveNodeCommand` ; insertion seulement sur les
  **rails satin** (`InsertSatinRailNodeCommand`), pas sur un vecteur.
- À faire : double-clic sur un segment = nouveau nœud (sans déformer la
  courbe : subdivision de De Casteljau) ; `InsertNodeCommand`.
- Modules : `libs/geometry`, `libs/commands`, `apps/desktop`.

### HP-VEC-002 — Tracé ouvert (ligne, polyligne, courbe ouverte) [P0] — ☐ À faire
- État OpenStitch : les outils de dessin ferment toujours la forme ; pas
  moyen de tracer une simple ligne pour un point de contour ou une tige.
- Hatch : outils de ligne ouverte (droite et courbe) pour contours et satins.
- À faire : outils « ligne » (clics, Entrée pour terminer **sans** fermer) et
  « courbe ouverte » ; `VectorObject` doit porter des chemins ouverts
  (vérifier `geometry::Path::closed`) ; point de contour et satin de bordure
  (HP-STI-004) acceptent un chemin ouvert.
- Modules : `libs/document`, `libs/geometry`, `apps/desktop`.

### HP-VEC-003 — Opérations booléennes entre formes [P1] — ☐ À faire
- Hatch : Weld (souder), Trim, Intersect, Exclude.
- À faire : union / différence / intersection / exclusion sur la sélection via
  Clipper2 (déjà encapsulé : `geometry/boolean.hpp`) ; une commande annulable ;
  les objets de broderie liés sont régénérés.
- Modules : `libs/commands`, `apps/desktop`.
- Dépend de : HP-OBJ-001.

### HP-VEC-004 — Couteau : couper une forme en deux [P1] — ◐ Partiel
- État OpenStitch : ligne de coupe **satin** uniquement (`DrawSatinCutLine`).
- À faire : couteau général sur toute forme vectorielle (`geometry/cut.hpp`
  existe), les deux morceaux gardent le type de broderie.

### HP-VEC-005 — Supprimer / remplir les trous [P1] — ☐ À faire
- Hatch : Remove holes / Fill holes.
- À faire : commandes « supprimer tous les trous », « supprimer ce trou »,
  « combler les trous < X mm² ».

### HP-VEC-006 — Créer un contour autour d'objets [P1] — ◐ Partiel
- État OpenStitch : « Décaler… » (offset d'une forme).
- Hatch : Outlines & Offsets : contour (point de contour ou satin) autour d'un
  ou plusieurs objets, avec écart, sur les trous ou non.
- À faire : action unique sur la sélection qui crée l'objet de contour
  (running, triple ou satin de bordure) avec l'écart choisi.
- Dépend de : HP-STI-004, HP-OBJ-001.

### HP-VEC-007 — Lisser / simplifier un tracé à la demande [P2] — ☐ À faire
- À faire : commande « lisser » (réduction des nœuds + ajustement de courbes)
  sur un objet existant.
- Dépend de : HP-AUTO-004.

### HP-VEC-008 — Convertir un polygone en courbes [P2] — ☐ À faire
- Dépend de : HP-AUTO-004.

### HP-VEC-009 — Magnétisme (grille, nœuds, guides) [P1] — ☐ À faire
- État OpenStitch : grille affichée, aucun accrochage.
- À faire : accrochage à la grille, aux nœuds et aux guides, activable
  (touche + bouton), tolérance en pixels écran.
- Modules : `apps/desktop/canvas_view`.

### HP-VEC-010 — Outil de mesure [P1] — ☐ À faire
- Hatch : mesurer une distance/un angle sur le canevas.
- À faire : outil règle : glisser → distance en mm, angle, affiché et copié.

### HP-VEC-011 — Guides tirés des règles [P2] — ☐ À faire
- À faire : glisser depuis une règle crée un guide horizontal/vertical
  (persisté dans le projet, masquable).

### HP-VEC-012 — Contraintes de tracé [P2] — ☐ À faire
- À faire : Maj = angles multiples de 15°/45° pendant le tracé polygone/ligne/Bézier.

### HP-VEC-013 — Séparer / combiner des morceaux [P1] — ☐ À faire
- Hatch : Break apart / Combine.
- À faire : un `VectorObject` à plusieurs `PathSet` se sépare en objets ; à
  l'inverse plusieurs objets se combinent en un seul.
- Dépend de : HP-OBJ-001.

### HP-VEC-014 — Formes prédéfinies supplémentaires [P2] — ☐ À faire
- À faire : étoile, cœur, flèche, rectangle arrondi, arc, spirale — outils
  paramétriques (poignées) ou bibliothèque (HP-LIB-002).

---

## 9. Manipulation d'objets (OBJ)

Aujourd'hui : **un seul objet sélectionné à la fois** ; translation d'un vecteur
à la souris ; redimensionnement par poignées de coin (`ScaleVectorObjectCommand`) ;
dupliquer / décaler (vecteurs) ; supprimer ; monter/descendre dans l'ordre ;
verrouiller pour l'optimisation. C'est le **deuxième mur** après les formats :
tout utilisateur de Hatch fait Ctrl+C/Ctrl+V, sélection rectangle, rotation.

### HP-OBJ-001 — Sélection multiple [P0] — ☐ À faire
- À faire : Ctrl/Maj+clic, rectangle de sélection (glisser dans le vide),
  Ctrl+A, sélection depuis la liste Document (multi-lignes), sélection de
  tous les objets d'une couleur ; boîte englobante commune avec poignées ;
  inspecteur multi-objets (HP-OBJ-019). Modèle de sélection sorti de
  `MainWindow` vers une classe dédiée testable.
- Modules : `apps/desktop` (nouvelle `selection_model`), tests QTest.
- Acceptation : QTest : sélection rectangle, ajout/retrait Ctrl+clic,
  synchronisation liste ↔ canevas.

### HP-OBJ-002 — Couper / copier / coller [P0] — ☐ À faire
- À faire : Ctrl+X/C/V sur objets vectoriels **et** leurs objets de broderie
  (paramètres, fil, overrides) ; nouveaux identifiants à la collation ; collage
  décalé ou à la même position (Ctrl+Maj+V) ; entre deux projets / deux
  instances (presse-papiers système, MIME dédié contenant du JSON `.osp`) ;
  une seule commande annulable par collage.
- Modules : `libs/project_io` (sérialisation d'un sous-ensemble),
  `libs/commands`, `apps/desktop`.
- Dépend de : HP-OBJ-001.

### HP-OBJ-003 — Déplacement au clavier [P0] — ☐ À faire
- À faire : flèches = 0,1 mm (ou pas de grille), Maj+flèches = 1 mm ; une
  commande fusionnée par rafale (undo en un coup).
- Modules : `apps/desktop`, `libs/commands` (fusion de commandes consécutives).

### HP-OBJ-004 — Rotation libre d'objets [P0] — ☐ À faire
- État OpenStitch : rotation de l'**image** par 90° ; orientation des fils
  du tatami ; aucune rotation d'objet.
- À faire : poignées de rotation sur la boîte de sélection (second clic ou
  poignée dédiée), angle numérique, Maj = pas de 15°, rotation autour du
  centre ou d'un pivot déplaçable ; les paramètres dépendants tournent aussi
  (angle tatami, guides directionnels, rails/barreaux satin, point d'entrée).
  `RotateObjectsCommand`.
- Modules : `libs/geometry`, `libs/commands`, `apps/desktop`.
- Acceptation : tatami à 30° tourné de 45° → fils à 75° ; rotation 360°
  = géométrie identique (au µm près, arrondi documenté).

### HP-OBJ-005 — Redimensionnement complet [P1] — ◐ Partiel
- État OpenStitch : poignées de coin (coin opposé fixe).
- À faire : poignées de côté (une dimension), Maj = proportionnel, Alt =
  depuis le centre, saisie numérique en mm et en %, redimensionnement d'une
  sélection multiple ; les paramètres dépendants suivent (guides, rails).
- Dépend de : HP-OBJ-001.

### HP-OBJ-006 — Miroir horizontal / vertical [P0] — ☐ À faire
- À faire : `MirrorObjectsCommand` sur la sélection (axe = centre de la
  sélection), paramètres dépendants miroités (angles, rails échangés pour
  garder l'orientation, guides).
- Modules : `libs/geometry`, `libs/commands`, `apps/desktop`.

### HP-OBJ-007 — Inclinaison (skew) [P2] — ☐ À faire

### HP-OBJ-008 — Aligner et répartir [P1] — ☐ À faire
- À faire : aligner gauche/centre/droite/haut/milieu/bas, sur la sélection,
  sur le premier objet ou sur le cadre ; répartir horizontalement/verticalement.
- Dépend de : HP-OBJ-001.

### HP-OBJ-009 — Grouper / dégrouper [P1] — ☐ À faire
- État OpenStitch : groupes **affichés** pour les sections d'un même plan
  satin dans le panneau Document, pas de groupe utilisateur.
- À faire : groupe persistant (sélection et transformation comme un seul
  objet, ordre de couture interne conservé), imbrication, entrer dans un
  groupe (double-clic).
- Modules : `libs/document`, `libs/commands`, `libs/project_io`, `apps/desktop`.

### HP-OBJ-010 — Masquer / verrouiller par objet depuis la liste [P1] — ◐ Partiel
- État OpenStitch : `visible` existe dans le modèle ; verrou = « ne pas bouger
  à l'optimisation » seulement.
- À faire : icônes œil/cadenas dans la liste ; verrou d'**édition**
  (non sélectionnable au canevas) distinct du verrou d'ordre.

### HP-OBJ-011 — Réordonner par glisser-déposer dans la liste [P1] — ☐ À faire
- État OpenStitch : boutons monter/descendre.
- À faire : glisser-déposer (un ou plusieurs objets) dans Document/Ordre ;
  « premier plan / arrière-plan / avant / arrière » (Ctrl+Pg↑/↓ …).
- Voir aussi HP-SEQ-002.

### HP-OBJ-012 — Dupliquer les objets de broderie [P1] — ◐ Partiel
- État OpenStitch : « Dupliquer » sur un vecteur.
- À faire : Ctrl+D sur la sélection (vecteur + broderie + fil), décalage
  paramétrable ; « dupliquer avec décalage répété » (le deuxième Ctrl+D
  reprend le même décalage).

### HP-OBJ-013 — Réseaux et dispositions (array, couronne, kaléidoscope, miroir-fusion) [P2] — ☐ À faire
- Hatch : Create layouts : réseau linéaire/circulaire, couronne (wreath),
  kaléidoscope, miroir-fusion.
- À faire : générateurs de copies transformées en une commande ; option
  « liées » (modifier l'original met à jour les copies) plus tard.

### HP-OBJ-014 — Transformer le design entier [P0] — ☐ À faire
- À faire : « centrer dans le cadre », « redimensionner le design » (toute la
  broderie, recalcul des points grâce à la régénération), rotation 90°/miroir
  du design complet — indispensable pour faire tenir un design dans le cadre
  de sa machine.
- Dépend de : HP-OBJ-004, HP-OBJ-005, HP-OBJ-006.

### HP-OBJ-015 — Barre de propriétés X / Y / L / H / angle [P1] — ☐ À faire
- Hatch : champs numériques de position/taille/rotation de la sélection.
- À faire : champs en mm dans la barre contextuelle ou l'inspecteur, cadenas
  de proportions ; chaque validation = une commande.

### HP-OBJ-016 — Suppression multiple [P0] — ◐ Partiel
- État OpenStitch : supprimer un objet, supprimer la broderie en gardant la
  forme ; Suppr = supprimer la **région** sélectionnée (conflit de sens).
- À faire : Suppr supprime la sélection courante quelle qu'elle soit (objets,
  régions, points en mode édition), une seule commande.
- Dépend de : HP-OBJ-001.

### HP-OBJ-017 — Renommer les objets [P2] — ☐ À faire
- À faire : F2 / double-clic dans la liste.

### HP-OBJ-018 — Changer la couleur d'un objet de broderie [P0] — ☐ À faire
- État OpenStitch : pas de sélecteur de couleur dans l'inspecteur pour un
  objet de broderie (seule la recoloration de région existe).
- À faire : pastille dans l'inspecteur → sélecteur (nuancier + RGB libre),
  application à la sélection multiple.
- Dépend de : HP-THR-004 pour la version « fil » (une version RGB peut venir avant).

### HP-OBJ-019 — Inspecteur multi-objets [P1] — ☐ À faire
- À faire : la sélection de plusieurs objets du même type montre les
  paramètres communs (valeurs différentes = champ « — ») ; une modification
  s'applique à tous en une commande.
- Dépend de : HP-OBJ-001.

### HP-OBJ-020 — Copier / coller les propriétés [P2] — ☐ À faire
- À faire : « pinceau » : prendre les paramètres d'un objet et les appliquer
  à d'autres (même type).

### HP-OBJ-021 — Panneau Historique [P2] — ☐ À faire
- À faire : liste des commandes de l'`UndoStack` avec libellés ; clic =
  revenir à cet état.

### HP-OBJ-022 — Convertir entre types sans perte [P1] — ◐ Partiel
- État OpenStitch : changement de type par clic droit (contour/tatami/satin/
  directionnel) ; le satin est reconstruit depuis le contour.
- À faire : conversions manquantes : satin → contour (ligne centrale), satin →
  forme (contour extérieur), contour → satin de bordure, tatami → contour
  (bord), conserver fil et points d'entrée.

---

## 10. Édition des points (EDIT)

Aujourd'hui : Lot 8.2 : mode d'édition des points, **déplacement** d'un point,
indicateurs Clean/Dirty/ManuallyEdited, abandon des retouches. Le cœur sait
aussi Stitch↔Jump et coupe (`SetStitchPointTypeCommand`, `SetStitchTrimCommand`)
mais **sans interface** (`stitch-editing.md`).

### HP-EDIT-001 — Interface Stitch↔Jump et coupe (Lot 8.3) [P1] — ☐ À faire
- À faire : menu contextuel / raccourcis en mode édition des points ; voir
  `docs/lot8-manual-editing-design.md`.
- Modules : `apps/desktop`.

### HP-EDIT-002 — Insérer et supprimer des points [P1] — ☐ À faire
- Hatch : insérer un point entre deux, supprimer des points.
- À faire : étendre `StitchOverride` (insertions/suppressions en deltas,
  compatibles ADR-014) + commandes + UI.
- Modules : `libs/document`, `libs/stitch_generation/overrides`, `libs/commands`.

### HP-EDIT-003 — Sélection et déplacement de plusieurs points [P1] — ☐ À faire
- À faire : rectangle de sélection de points, déplacement groupé, suppression groupée.

### HP-EDIT-004 — Édition des points d'un fichier machine importé [P1] — ☐ À faire
- État OpenStitch : une séquence importée est « la vérité », non éditable
  point par point.
- À faire : même mode d'édition sur la séquence importée (objet pseudo
  « séquence brute » qui porte des overrides).

### HP-EDIT-005 — Insérer arrêt, changement de couleur, coupe manuels [P2] — ☐ À faire

### HP-EDIT-006 — Poignées d'entrée/sortie sur le canevas [P1] — ◐ Partiel
- État OpenStitch : `entry_point` / `exit_point` dans les paramètres ; pas de
  poignée générale pour tous les types.
- Hatch : Reshape affiche des marqueurs d'entrée/sortie déplaçables.
- À faire : marqueurs déplaçables pour tout objet sélectionné (tatami,
  directionnel, satin, contour fermé).

### HP-EDIT-007 — Plusieurs lignes d'angle dans un remplissage [P2] — ◐ Partiel
- État OpenStitch : le directionnel couvre ce besoin ; le tatami n'a qu'un angle.
- Hatch : Stitch angles : ajouter plusieurs lignes d'angle à un objet.
- À faire : ergonomie « ajouter une ligne d'angle » sur un tatami qui le
  convertit en directionnel de façon transparente.

---

## 11. Ordre de couture et séquence (SEQ)

Aujourd'hui : stratégies document / couleur / proximité / couleur+proximité /
couches ; objets verrouillables ; coût estimé ; routage satin multi-colonnes.

### HP-SEQ-001 — Amélioration 2-opt / Or-opt [P1] — ☐ À faire
- État OpenStitch : `limitations.md` : « 2-opt non implémenté ».
- À faire : amélioration locale déterministe après la stratégie choisie, sur
  les points d'entrée/sortie réels (HP-ENG-010).
- Modules : `libs/optimization`.

### HP-SEQ-002 — Ordre par glisser-déposer [P1] — ☐ À faire
- Voir HP-OBJ-011 ; dans le film couleur (HP-THR-005) aussi.

### HP-SEQ-003 — Séquencer par couleur depuis le film couleur [P0] — ☐ À faire
- Voir HP-THR-005.

### HP-SEQ-004 — Fusion de plusieurs objets en un seul parcours (branching) [P2] — ☐ À faire
- Hatch : Branching : plusieurs objets de même couleur cousus d'un seul trajet
  (déplacements cachés sous les objets), sans coupe.
- Modules : `libs/stitch_generation` (routage), `libs/optimization`.

### HP-SEQ-005 — Connecteurs réglables par objet [P2] — ☐ À faire
- À faire : pour chaque liaison sortante : auto / saut / saut+coupe / cousu.

### HP-SEQ-006 — Minimiser les coupes [P1] — ☐ À faire
- À faire : préférer un déplacement cousu caché sous un objet cousu ensuite
  (même couleur ou couverture garantie) plutôt qu'une coupe ; métrique :
  nombre de coupes sur le corpus.

### HP-SEQ-007 — Machines sans coupe automatique [P2] — ☐ À faire
- À faire : profil « pas de coupe » : les coupes deviennent des arrêts
  (l'utilisateur coupe à la main) avec points d'arrêt renforcés.
- Dépend de : HP-HOOP-002.

### HP-SEQ-008 — Point de départ / d'arrivée du design [P1] — ☐ À faire
- Hatch : départ/fin au centre, en haut à gauche…
- À faire : réglage de l'origine machine et du point de retour final.

---

## 12. Visualisation et rendu (VIEW)

Aujourd'hui : points tracés en traits fins (≈ 0,15 mm) dans la couleur du fil,
sauts en pointillés, pastilles de pénétration masquées au-delà de 4 000 points,
calques image/régions/vecteurs/broderie, filtres d'affichage, thème clair/sombre.

### HP-VIEW-001 — Rendu réaliste des points [P0] — ☐ À faire
- Hatch : TrueView : chaque point dessiné comme un fil (épaisseur réelle
  ~0,4 mm, dégradé d'ombrage, reflet), bascule rapide TrueView/points.
- À faire : rendu « fil » : segment épais aux bouts arrondis, dégradé
  perpendiculaire (clair au centre), légère ombre ; rendu rapide (cache
  d'images par objet, GPU via `QOpenGLWidget`/`QRhi` si nécessaire) ;
  bascule T.
- Modules : `apps/desktop/canvas_view` (rendu uniquement, aucune logique).
- Acceptation : 100 000 points rendus en < 100 ms après modification d'un
  objet (mesure dans `tests/bench/bench_main_window.cpp`).

### HP-VIEW-002 — Rendu 3D [P2] — ☐ À faire
- À faire : mode relief (éclairage directionnel, épaisseur cumulée) pour la
  présentation.

### HP-VIEW-003 — Affichage par passe (sous-couche, déplacements, verrous) [P1] — ☐ À faire
- État OpenStitch : `StitchPass` existe dans chaque commande ; `limitations.md` :
  « affichage/toggle par passe dans l'UI à venir ».
- À faire : cases à cocher Sous-couche / Couche supérieure / Déplacements /
  Verrous / Manuels, couleurs distinctes optionnelles ; afficher les
  pénétrations, les connecteurs.

### HP-VIEW-004 — Fond tissu et mise en situation sur vêtement [P2] — ☐ À faire
- Hatch : afficher le design sur un produit (t-shirt, casquette, serviette).
- À faire : couleur de tissu (HP-THR-008), texture, images de produits avec
  zone brodable, cadre montré dessus.

### HP-VIEW-005 — Carte de densité [P1] — ☐ À faire
- État OpenStitch : `limitations.md` : « pas de carte de densité ».
- À faire : cœur : grille de densité (pénétrations/mm², épaisseur cumulée)
  calculée par `stitch_analysis` ; UI : surimpression colorée (échelle
  séquentielle) + règle d'analyse « zone trop dense ».
- Modules : `libs/stitch_analysis`, `apps/desktop`.

### HP-VIEW-006 — Vue d'ensemble (mini-carte) [P2] — ☐ À faire

### HP-VIEW-007 — Zoom rectangle et zoom 1:1 réel [P1] — ☐ À faire
- À faire : zoom sur un rectangle glissé ; zoom 100 % = taille réelle à
  l'écran (DPI physique, calibrable) ; zoom sur la sélection.

### HP-VIEW-008 — Vue scindée dessin / broderie [P3] — ☐ À faire

### HP-VIEW-009 — Fluidité sur gros designs [P1] — ☐ À faire
- État OpenStitch : pastilles masquées au-delà de 4 000 points ; la scène est
  reconstruite par `refreshImage()` à chaque modification (voir aussi
  HP-PERF-004).
- À faire : rendu incrémental (seul l'objet modifié est redessiné),
  niveau de détail selon le zoom ; cible 200 000 points à 60 i/s au zoom/pan.

### HP-VIEW-010 — Grille et guides configurables [P1] — ☐ À faire
- À faire : pas de grille réglable, grille majeure/mineure, couleur, masquage.

### HP-VIEW-011 — Opacité de l'image de fond [P1] — ☐ À faire
- À faire : curseur d'opacité / estompage de l'image pendant la numérisation.

### HP-VIEW-012 — Aperçu en direct pendant le réglage [P1] — ☐ À faire
- À faire : les spinbox de l'inspecteur mettent à jour l'aperçu pendant le
  glisser/la saisie (commande validée à la fin), sans latence perceptible
  pour un objet isolé.
- Dépend de : HP-PERF-002.

### HP-VIEW-013 — Info-bulle de survol d'un objet [P2] — ☐ À faire
- À faire : type, fil, nombre de points, dimensions.

---

## 13. Simulation (SIM)

Aujourd'hui : lecture/pause, curseur, compteur, repère d'aiguille (~60 pas/s).

### HP-SIM-001 — Réglage de vitesse [P1] — ☐ À faire
- État OpenStitch : `simulation.md` : pas de réglage de vitesse.

### HP-SIM-002 — Navigation par objet / couleur / point [P1] — ☐ À faire
- Hatch : Stitch Player : sauter à l'objet ou à la couleur suivant(e) /
  précédent(e), pas à pas avant/arrière.

### HP-SIM-003 — Objet courant mis en évidence [P1] — ☐ À faire
- À faire : l'objet en cours de couture est surligné dans la liste et le
  film couleur ; la couleur de fil courante est affichée.

### HP-SIM-004 — Estimation du temps affichée pendant la simulation [P1] — ☐ À faire
- Dépend de : HP-PROD-002.

### HP-SIM-005 — Export vidéo / GIF de la simulation [P3] — ☐ À faire

### HP-SIM-006 — Visualiser sauts et coupes pendant la simulation [P2] — ☐ À faire
- À faire : pause optionnelle aux changements de couleur (comme la machine),
  marqueurs de coupe.

---

## 14. Production, analyse et impression (PROD)

Aujourd'hui : statistiques (points, sauts, coupes, couleurs, dimensions,
fil), analyse par règles (points courts/longs, sauts longs, trop de points,
hors cadre, déplacement sans coupe), résumé avant export DST.

### HP-PROD-001 — Fiche de production imprimable [P0] — ☐ À faire
- Hatch : Production worksheet : aperçu du design, dimensions, nombre de
  points, temps estimé, cadre, **liste des fils dans l'ordre** (marque, code,
  nom, pastille), notes, client.
- À faire : cœur : structure de données de la fiche ; desktop : aperçu avant
  impression + impression + export PDF (`QPdfWriter`, Qt est LGPL, OK côté
  desktop seulement) ; CLI : export texte/JSON.
- Modules : `libs/stitch_analysis` (données), `apps/desktop`, `apps/cli`.
- Dépend de : HP-THR-004 (liste de fils), HP-PROD-002.

### HP-PROD-002 — Estimation de la durée de couture [P1] — ☐ À faire
- À faire : points / vitesse machine (profil) + temps par changement de fil,
  par coupe, par saut ; affichée dans les statistiques, le résumé d'export,
  la fiche.
- Modules : `libs/stitch_analysis`.
- Dépend de : HP-HOOP-002 (vitesse).

### HP-PROD-003 — Gabarit de placement imprimé à l'échelle 1:1 [P1] — ☐ À faire
- Hatch : impression du design à taille réelle avec croix de centrage et axes.
- À faire : impression 1:1 vérifiée (règle imprimée de 10 cm), sur plusieurs
  pages si besoin.

### HP-PROD-004 — Aperçu avant impression général [P1] — ☐ À faire

### HP-PROD-005 — Consommation de fil par couleur et canette [P1] — ◐ Partiel
- État OpenStitch : longueur de fil totale.
- À faire : par couleur, estimation fil de canette, en mètres.

### HP-PROD-006 — Devis (prix par 1 000 points) [P3] — ☐ À faire

### HP-PROD-007 — Règles d'analyse supplémentaires [P1] — ◐ Partiel
- À faire : zone trop dense / recouvrements empilés (HP-VIEW-005), détails
  < 1 mm, texte trop petit (HP-TXT-009), satin trop large, objet sans point
  d'arrêt, fil trop proche de la couleur du tissu, dépassement de la zone
  cousable de la machine, nombre de couleurs > aiguilles de la machine.
- Modules : `libs/stitch_analysis`.

### HP-PROD-008 — Corrections automatiques depuis l'analyse [P2] — ☐ À faire
- À faire : bouton « Corriger » par problème quand une correction sûre existe
  (ajouter une coupe, découper un point long, supprimer un point court),
  appliqué par commande annulable.

---

## 15. Cadres et machines (HOOP)

Aujourd'hui : un cadre **rectangulaire** de taille libre (10–500 mm), persisté.

### HP-HOOP-001 — Bibliothèque de cadres par marque [P1] — ☐ À faire
- Hatch : liste des cadres de chaque marque (Brother, Babylock, Janome,
  Bernina, Husqvarna, Pfaff, Singer, Tajima…) avec forme (rectangle, rond,
  ovale, arrondi), zone cousable, gabarit.
- À faire : données de cadres (JSON), formes non rectangulaires dans
  `Canvas`, test « hors cadre » sur la forme réelle.
- Modules : `libs/document`, données, `apps/desktop`.

### HP-HOOP-002 — Profil machine [P1] — ☐ À faire
- À faire : `MachineProfile` : marque/modèle, format préféré, cadres
  disponibles, champ max, longueur max de point, coupe auto oui/non, nombre
  d'aiguilles, vitesse ; choisi dans les préférences et par projet ;
  alimente HP-FMT-001.
- Modules : `libs/document` ou nouvelle `libs/machine`, `apps/desktop`.

### HP-HOOP-003 — Vérifications à l'export selon la machine [P1] — ☐ À faire
- À faire : le résumé d'export avertit : format incompatible, design plus
  grand que le plus grand cadre, trop de couleurs pour les aiguilles…
- Dépend de : HP-HOOP-002.

### HP-HOOP-004 — Multi-cadrage (design découpé en plusieurs cadrages) [P2] — ☐ À faire
- Hatch Digitizer : Multi-hooping : placer plusieurs positions de cadre sur
  un grand design, découper les objets, points de repère d'alignement, un
  fichier par cadrage.
- Modules : `libs/stitch_generation` (découpe), `libs/formats`, `apps/desktop`.

### HP-HOOP-005 — Rotation du cadre / design en paysage [P1] — ☐ À faire

### HP-HOOP-006 — Centrer automatiquement dans le cadre [P1] — ☐ À faire
- Voir HP-OBJ-014.

---

## 16. Tissus (FAB)

### HP-FAB-001 — Profils de tissu (Auto Fabric) [P1] — ☐ À faire
- Hatch : Auto Fabric : choisir le tissu (coton, jersey, polaire, éponge,
  denim, soie, cuir, casquette) ajuste densité, sous-couche, compensation de
  tous les objets automatiquement.
- À faire : `FabricProfile` (facteurs appliqués aux défauts « Auto »), choix
  par projet, régénération ; valeurs issues de la validation physique
  (HP-PHYS-001), documentées avec leur source.
- Modules : `libs/document`, `libs/stitch_generation`.
- Dépend de : HP-ENG-002.

### HP-FAB-002 — Stabilisateur recommandé [P2] — ☐ À faire
- À faire : recommandation d'entoilage selon tissu + densité, affichée sur la
  fiche de production.

### HP-FAB-003 — Couche d'écrasement pour éponge (knockdown) [P2] — ☐ À faire
- À faire : tatami très lâche automatique sous le design (couleur du tissu)
  pour les tissus à poils.

---

## 17. Appliqué et techniques spéciales (SPEC)

Aujourd'hui : rien (aucune occurrence « appliqué » dans le code métier).

### HP-SPEC-001 — Appliqué [P1] — ☐ À faire
- Hatch : objet appliqué : ligne de placement → arrêt → point de maintien
  (tack-down, zigzag ou E-stitch) → arrêt → couverture satin ; appliqué
  depuis une forme ; export du contour de découpe (SVG/DXF pour plotter,
  cutter Brother ScanNCut).
- À faire : nouveau type d'objet (alternative de `StitchParams`) générant les
  passes avec `Stop` machine ; export du contour de découpe via `formats`.
- Modules : `libs/document`, `libs/stitch_generation`, `libs/formats`, UI.
- Dépend de : HP-STI-003, HP-STI-004.

### HP-SPEC-002 — Appliqué partiel / multiple [P2] — ☐ À faire
- À faire : plusieurs tissus d'appliqué ordonnés, côtés recouverts par un
  autre objet sans couverture satin.

### HP-SPEC-003 — Dentelle autoportante (FSL) [P3] — ☐ À faire
- À faire : sous-couche en treillis, contrôle de connexité des fils.

### HP-SPEC-004 — Écussons / patchs [P2] — ☐ À faire
- À faire : bordure satin épaisse type merrow, gabarits de formes d'écusson.
- Dépend de : HP-STI-004.

### HP-SPEC-005 — Projets « dans le cadre » (ITH) [P3] — ☐ À faire

### HP-SPEC-006 — Casquettes [P2] — ☐ À faire
- À faire : ordre imposé (centre → extérieur, bas → haut) et compensation
  adaptée pour cadre casquette.

---

## 18. Bibliothèques (LIB)

### HP-LIB-001 — Designs d'exemple [P2] — ☐ À faire
- Hatch : des centaines de designs inclus.
- À faire : quelques dizaines de designs `.osp` sous licence libre, livrés
  avec l'application ; ouverture depuis l'écran d'accueil.

### HP-LIB-002 — Bibliothèque de formes, motifs, bordures, ornements [P2] — ☐ À faire
- À faire : panneau de ressources glissables sur le canevas.

### HP-LIB-003 — Organiseur de fichiers (catalogue) [P2] — ☐ À faire
- Hatch Organizer : navigation dans les dossiers avec vignettes, recherche,
  étiquettes, conversion, impression de catalogue.
- Dépend de : HP-FMT-020, HP-FMT-018.

---

## 19. Gestion de fichiers et de projets (FILE)

Aujourd'hui : ouvrir image/SVG, enregistrer `.osp` (**le chemin est redemandé
à chaque Ctrl+S**, `MainWindow::saveProject`), ouvrir `.osp`, import/export
DST et DXF, indicateur « modifié » et garde à la fermeture.

### HP-FILE-001 — Nouveau projet (Ctrl+N) [P0] — ☑ Fait (2026-09-23)
- État OpenStitch : aucune action « Nouveau » ; il faut relancer ou ouvrir
  autre chose.
- À faire : action Nouveau (avec garde des modifications non enregistrées),
  réinitialisation propre de tous les modes d'édition (cf. la sortie propre du
  mode d'édition des points déjà faite au chargement).
- Livré : action « Fichier ▸ Nouveau projet » (Ctrl+N, `action_newProject`) +
  bouton de barre principale, avec garde des modifications non enregistrées
  (`MainWindow::confirmDiscardChanges`, Enregistrer / Ne pas enregistrer /
  Annuler — garde désormais partagée avec la fermeture de la fenêtre).
  Réinitialisation centralisée dans `MainWindow::resetDocumentState()`, appelée
  par TOUS les chemins de remplacement de document (Nouveau, ouvrir image,
  ouvrir SVG, ouvrir projet, importer DST) : pile d'annulation, séquence en
  cache, états d'édition, sélections, mode fusion, les cinq modes d'édition
  exclusifs et leurs cibles, les tracés en cours (polygone/main levée/colonne
  satin/Bézier/guides de direction) et leurs aperçus de scène, la simulation.
  Chacun de ces chemins n'en réinitialisait auparavant qu'une partie (mode
  satin, sélection de broderie et tracé en cours survivaient au changement de
  document). `applyLoadedProject` rafraîchit aussi explicitement les panneaux
  quand le document n'a pas d'image (`refreshImage()` sort tôt dans ce cas) —
  sans quoi le panneau Document gardait les objets du document précédent.
  Tests : `newProjectActionIsInFileMenuWithStandardShortcut`,
  `newProjectResetsDocumentEditModesAndPanels`,
  `newProjectOnModifiedDocumentCancelsOrDiscardsAsChosen`
  (`tests/unit/desktop/test_main_window.cpp`).
- Reste (hors entrée) : le chemin « Enregistrer » de la garde rouvre toujours un
  sélecteur de fichier tant que HP-FILE-002 n'est pas fait ; un document sans
  image ne régénère toujours pas de points (`refreshImage` sort tôt), limite
  indépendante de cette entrée.

### HP-FILE-002 — Enregistrer / Enregistrer sous [P0] — ☐ À faire
- État OpenStitch : « Enregistrer le projet… » ouvre toujours un dialogue.
- À faire : chemin courant mémorisé ; Ctrl+S enregistre directement,
  Ctrl+Maj+S = Enregistrer sous ; nom du fichier dans le titre de la fenêtre ;
  écriture atomique (fichier temporaire + renommage) pour ne jamais corrompre
  un `.osp` en cas de crash pendant l'écriture.
- Modules : `apps/desktop`, `libs/project_io` (écriture atomique).

### HP-FILE-003 — Fichiers récents [P0] — ☐ À faire
- À faire : sous-menu Fichier ▸ Récents (10), liste sur l'écran d'accueil
  (avec vignettes quand HP-FMT-020 existe), suppression des entrées disparues.

### HP-FILE-004 — Sauvegarde automatique et récupération après plantage [P0] — ☐ À faire
- État OpenStitch : `limitations.md` : « pas d'autosave ».
- À faire : autosave périodique (hors thread UI si lourd) dans le dossier
  applicatif ; au démarrage suivant un plantage, proposer la récupération.

### HP-FILE-005 — Association de fichiers et glisser-déposer [P1] — ☐ À faire
- À faire : double-clic sur un `.osp` (et, en option, `.dst`/`.pes`) ouvre
  OpenStitch (installateur) ; ouverture par argument de ligne de commande ;
  instance unique optionnelle ; glisser-déposer (HP-FMT-016).

### HP-FILE-006 — Plusieurs documents ouverts (onglets) [P2] — ☐ À faire
- À faire : `Project` + `UndoStack` par onglet ; copier/coller entre onglets.

### HP-FILE-007 — Insérer un design dans le projet courant [P1] — ☐ À faire
- Hatch : Insert design : fusionner un autre fichier dans le design ouvert.
- À faire : insérer un `.osp` (objets éditables) ou un fichier machine
  (séquence brute positionnable) ; une commande annulable.

### HP-FILE-008 — Modèles de projet [P2] — ☐ À faire
- À faire : projet de départ avec cadre, tissu, machine, préréglages.

### HP-FILE-009 — Copies de sauvegarde versionnées [P2] — ☐ À faire
- À faire : `.osp.bak` à chaque enregistrement (N versions).

### HP-FILE-010 — Relier / remplacer l'image source [P3] — ☐ À faire

### HP-FILE-011 — Propriétés du document [P2] — ☐ À faire
- Voir HP-FMT-020 (métadonnées) ; dialogue d'édition.

---

## 20. Ergonomie générale (UX)

Aujourd'hui : docks, thème clair/sombre, densité, inspecteur, panneau Document,
workflow, état d'accueil, barre contextuelle, raccourcis de base. Beaucoup
d'actions passent par des **dialogues modaux** (« Créer un tatami… » demande
les paramètres avant de créer) et par des **modes** distincts (édition satin,
édition rails, guides, points, guides directionnels).

### HP-UX-001 — Outil de sélection unique et direct [P1] — ☐ À faire
- Hatch : un outil Sélection et un outil Remodeler (Reshape) couvrent
  l'essentiel ; pas de multiples modes à activer par menu.
- À faire : réduire le nombre de modes : sélectionner un objet montre ses
  poignées de transformation ; double-clic (ou touche) passe en remodelage
  (nœuds, rails, guides, entrée/sortie selon le type) ; Échap remonte d'un
  niveau. Revoir `docs/ui-redesign-*.md`.
- Modules : `apps/desktop`.

### HP-UX-002 — Création directe sans dialogue [P1] — ☐ À faire
- À faire : « Créer un tatami » crée immédiatement avec les défauts (ou le
  dernier réglage) ; les paramètres se règlent ensuite dans l'inspecteur.

### HP-UX-003 — Inspecteur homogène pour tous les types [P1] — ◐ Partiel
- À faire : mêmes sections pour tous (Général : nom, fil, visible ; Points ;
  Sous-couche ; Compensation ; Entrée/sortie ; Avancé), unités affichées,
  info-bulle sur chaque champ, bouton « réinitialiser au défaut » par champ,
  valeurs par défaut documentées.
- Modules : `apps/desktop/properties_panel`.

### HP-UX-004 — Palette d'outils organisée par tâches [P1] — ☐ À faire
- Hatch : onglets / boîtes à outils : Numériser, Éditer, Lettrage, Disposer,
  Couleurs…
- À faire : regroupement logique des outils, libellés et icônes cohérents.

### HP-UX-005 — Menu contextuel complet [P1] — ◐ Partiel
- À faire : couper/copier/coller/dupliquer, ordre (avant/arrière), couleur/fil,
  grouper, aligner, transformer, convertir, propriétés — cohérent avec la
  barre de menus.

### HP-UX-006 — Progression et annulation des opérations longues [P0] — ☐ À faire
- Voir HP-PERF-001 : barre de progression, bouton Annuler, UI jamais figée.

### HP-UX-007 — Préférences générales [P1] — ☐ À faire
- État OpenStitch : seules les préférences IA ont un dialogue ; thème et
  densité dans le menu.
- À faire : dialogue Préférences : unités, grille, magnétisme, défauts de
  points, machine/cadre par défaut, dossier de travail, autosave, langue.

### HP-UX-008 — Unités impériales (pouces) [P1] — ☐ À faire
- À faire : affichage et saisie en pouces (règles, inspecteur, dimensions),
  stockage toujours en µm. Indispensable pour le marché nord-américain.

### HP-UX-009 — Conventions de navigation standard [P1] — ◐ Partiel
- État OpenStitch : molette = zoom ancré, outil Main ; le glisser en Sélection
  déplace la vue (non standard : il devrait faire un rectangle de sélection).
- À faire : Espace+glisser et clic molette = déplacer la vue ; glisser dans le
  vide = rectangle de sélection (HP-OBJ-001) ; Ctrl+molette / molette
  configurable ; pavé tactile (pinch-zoom).

### HP-UX-010 — Barre d'état informative [P1] — ◐ Partiel
- À faire : taille de la sélection, nombre de points de la sélection et du
  design, couleur courante, zoom, unité.

### HP-UX-011 — Raccourcis et barres personnalisables [P2] — ☐ À faire

### HP-UX-012 — Assistant de démarrage image → broderie [P1] — ◐ Partiel
- État OpenStitch : bandeau workflow (6 étapes) informatif.
- Hatch : assistants pas à pas pour les débutants.
- À faire : assistant guidé (image → taille → couleurs → fond → numériser →
  vérifier → exporter pour ma machine).

### HP-UX-013 — Messages d'erreur actionnables [P2] — ◐ Partiel
- À faire : chaque refus dit quoi faire (« satin refusé : forme trop large,
  essayez Remplissage directionnel [bouton] »).

### HP-UX-014 — Retour visuel du survol et des curseurs [P2] — ☐ À faire
- À faire : surbrillance au survol, curseurs spécifiques par poignée.

### HP-UX-015 — Icônes haute densité et cohérence visuelle [P2] — ◐ Partiel
- À faire : jeu d'icônes complet (tous les outils/actions), rendu net en 150–200 %.

### HP-UX-016 — Libellés compréhensibles par un non-technicien [P1] — ☐ À faire
- À faire : relire tous les libellés (« Déboguer : afficher toutes les
  données… », « Remodelage satin (avancé) », « trajet caché ») ; masquer les
  outils de débogage derrière un mode développeur.

---

## 21. Internationalisation et accessibilité (I18N)

Aujourd'hui : textes en **français** dans `tr()`, aucun `QTranslator`, aucun
fichier `.ts`. Hatch est disponible en une dizaine de langues.

### HP-I18N-001 — Infrastructure de traduction [P0] — ☐ À faire
- À faire : `qt_add_translations` (CMake), fichiers `.ts`, chargement du
  `QTranslator` selon la langue système / la préférence ; décider la langue
  source (garder le français source est possible ; l'anglais source est
  l'usage courant — décision à documenter en ADR) ; messages du cœur
  (`Finding::message`, erreurs `Result`) traduisibles : identifiants +
  paramètres côté cœur, texte côté desktop.
- Modules : `apps/desktop`, `libs/stitch_analysis`, `libs/core/error`.

### HP-I18N-002 — Anglais complet [P0] — ☐ À faire
- Dépend de : HP-I18N-001.
- Acceptation : aucune chaîne française visible en anglais (test qui charge la
  traduction et vérifie l'absence de chaînes non traduites).

### HP-I18N-003 — Autres langues (ES, DE, IT, PT, NL, JA) [P1] — ☐ À faire
- À faire : plateforme de traduction communautaire (Weblate libre).

### HP-I18N-004 — Accessibilité (clavier, lecteur d'écran, contraste) [P2] — ◐ Partiel
- À faire : noms accessibles sur tous les contrôles, navigation clavier
  complète des panneaux, audit WCAG des thèmes.

### HP-I18N-005 — Séparateur décimal et formats locaux [P2] — ☐ À faire

### HP-I18N-006 — Documentation utilisateur traduite [P2] — ☐ À faire
- Dépend de : HP-HELP-001.

---

## 22. Performance et réactivité (PERF)

Aujourd'hui : **tout est synchrone** (`CLAUDE.md` : « No async task
infrastructure exists yet »), auto-numérisation ~18 s sur l'image de référence
(UI figée pendant ce temps), régénération de toute la séquence à chaque
modification, scène reconstruite à chaque rafraîchissement.

### HP-PERF-001 — Tâches en arrière-plan avec progression et annulation [P0] — ☐ À faire
- À faire : infrastructure de tâches (thread de travail, instantané immuable
  du `Project` en entrée, résultat appliqué par commande sur le thread UI),
  progression et jeton d'annulation passés aux fonctions du cœur (sans Qt :
  callback / `std::stop_token`) ; migrer segmentation, auto-numérisation,
  génération, IA.
- Modules : `libs/core` (progress/cancel), `apps/desktop`.
- Acceptation : l'UI reste réactive (QTest : un événement traité pendant une
  auto-numérisation) ; annuler ne modifie pas le document ; déterminisme
  inchangé (même résultat en tâche de fond qu'en synchrone).

### HP-PERF-002 — Régénération incrémentale par objet [P0] — ☐ À faire
- À faire : cache de la séquence générée par objet, clé = empreinte (paramètres
  + géométrie source + contexte de routage) ; modifier un objet ne régénère
  que lui et ses liaisons ; `effective_sequence` assemble les tranches.
  Respecter la garde structurelle `check_no_raw_sequence_bypass.cmake`.
- Modules : `libs/stitch_generation`.
- Acceptation : séquence octet pour octet identique avec et sans cache
  (test d'équivalence comme `test_skeleton_equivalence.cpp`) ; modification
  d'un objet sur `tentabrode` < 50 ms.

### HP-PERF-003 — Auto-numérisation plus rapide [P1] — ◐ Partiel
- État OpenStitch : 94,6 s → ~18 s (`docs/performance-audit.md`).
- À faire : profiler de nouveau, paralléliser par région (résultat réassemblé
  dans un ordre déterministe) ; cible < 5 s sur `tentabrode.png`.

### HP-PERF-004 — Rendu de scène incrémental [P1] — ☐ À faire
- Voir HP-VIEW-009 : ne plus reconstruire toute la scène dans `refreshImage()`.

### HP-PERF-005 — Parallélisation des générateurs [P1] — ☐ À faire
- À faire : génération des objets indépendants en parallèle, assemblage
  ordonné (déterminisme strict).

### HP-PERF-006 — Démarrage rapide [P2] — ☐ À faire
- À faire : mesurer le démarrage, charger paresseusement l'IA et les nuanciers.

### HP-PERF-007 — Mémoire sur gros projets [P2] — ☐ À faire
- À faire : mesurer un projet de 500 000 points et 2 000 objets ; plafonds.

### HP-PERF-008 — Suivi des performances dans le temps [P2] — ◐ Partiel
- État OpenStitch : `openstitch-bench` hors CTest.
- À faire : exécution périodique en CI (non bloquante), historique des mesures.

---

## 23. Robustesse et qualité logicielle (QA)

### HP-QA-001 — Découper `main_window.cpp` [P1] — ☐ À faire
- État OpenStitch : `apps/desktop/main_window.cpp` ≈ 305 Ko, un seul fichier
  pour menus, outils, modes d'édition, rendu, simulation. Chaque nouvelle
  fonctionnalité UI de cette liste l'alourdit et ralentit tout le monde.
- À faire : extraire par responsabilité (contrôleur de sélection, outils de
  dessin, modes d'édition satin/points/directionnel, rendu des points,
  simulation, fichiers) — comme a commencé `main_window_directional.cpp` ;
  aucun changement de comportement, tests QTest verts à chaque étape.
- Faire **tôt** : avant HP-OBJ-001 idéalement.

### HP-QA-002 — Journal et rapport de plantage [P1] — ☐ À faire
- À faire : journal fichier (spdlog déjà présent) dans le dossier applicatif,
  minidump en cas de crash (Windows `MiniDumpWriteDump`), « envoyer le rapport »
  (lien vers un ticket prérempli, jamais d'envoi silencieux).

### HP-QA-003 — Fuzzing des décodeurs [P1] — ☐ À faire
- À faire : cibles libFuzzer (clang-cl ou job Linux) pour DST, PES, JEF, EXP,
  SVG, DXF, `.osp` ; corpus minimal commité.

### HP-QA-004 — Tests bout en bout desktop [P2] — ◐ Partiel
- À faire : scénarios complets en QTest offscreen (image → numériser → éditer
  → exporter PES) pour chaque vague livrée.

### HP-QA-005 — Vérification croisée avec un lecteur tiers [P1] — ☐ À faire
- État OpenStitch : `limitations.md` : « Validé sur simulateur : non ».
- À faire : script de test (hors build, optionnel) qui relit nos exports avec
  pyembroidery (MIT) et compare les points ; exécuté en CI.

### HP-QA-006 — Crash sur fichiers réels [P1] — ☐ À faire
- À faire : corpus de fichiers utilisateurs réels (DST/PES/SVG/images
  étranges : 16 bits, CMJN, énormes, transparents) ouverts en test.

---

## 24. Distribution et plateformes (DIST)

Aujourd'hui : un script Inno Setup (`packaging/windows/installer.iss`),
build Windows MSVC, pas de binaire public.

### HP-DIST-001 — Installateur Windows signé et publié [P0] — ◐ Partiel
- À faire : installateur produit par la CI (Release), Qt déployé
  (`windeployqt`), runtime MSVC, signature de code (sinon SmartScreen bloque
  l'utilisateur), associations de fichiers (HP-FILE-005), désinstallation
  propre ; release GitHub avec notes.
- Modules : `packaging/`, `.github/workflows/`.

### HP-DIST-002 — Mise à jour automatique [P2] — ☐ À faire
- À faire : vérification de version au démarrage (opt-in), lien de
  téléchargement.

### HP-DIST-003 — macOS [P2] — ☐ À faire
- Hatch est Windows seulement : un portage macOS serait un **avantage** net.

### HP-DIST-004 — Linux desktop [P2] — ☐ À faire
- État OpenStitch : cœur + CLI compilent sous Linux ; pas de desktop.
- À faire : build desktop Linux, AppImage/Flatpak.

### HP-DIST-005 — Site web, téléchargement, communauté [P1] — ☐ À faire
- À faire : page de présentation, captures, téléchargements, forum/Discussions,
  guide de contribution.

### HP-DIST-006 — IA installable sans compétences techniques [P1] — ☐ À faire
- Voir HP-AUTO-011.

### HP-DIST-007 — Nom définitif du produit [P1] — ☐ À faire
- État OpenStitch : « nom temporaire » (ADR-001) ; à trancher avant la
  première publication publique (vérifier les marques existantes).

---

## 25. Aide et documentation utilisateur (HELP)

Aujourd'hui : documentation technique riche (PDF) surtout pour développeurs ;
un guide utilisateur ; aide = liste des raccourcis + À propos.

### HP-HELP-001 — Manuel utilisateur en ligne et aide contextuelle (F1) [P1] — ☐ À faire
- À faire : manuel orienté tâches (« broder un logo », « faire du texte »,
  « exporter pour une Brother »), accessible par F1 sur l'outil/le panneau
  courant.

### HP-HELP-002 — Tutoriels et projets guidés [P1] — ☐ À faire
- À faire : 5 à 10 tutoriels pas à pas avec fichiers d'exemple (HP-LIB-001).

### HP-HELP-003 — Premier lancement guidé [P2] — ☐ À faire
- À faire : choix de la machine, du cadre, des unités, de la langue au
  premier démarrage.

### HP-HELP-004 — Infobulles illustrées des paramètres [P2] — ☐ À faire
- À faire : petit schéma pour densité, compensation, sous-couches, split.

---

## 26. Validation physique (PHYS)

Aujourd'hui : **aucune validation sur machine réelle** (`limitations.md`).
C'est la plus grande différence invisible : les défauts de Hatch ont été
corrigés par des années de broderies réelles.

### HP-PHYS-001 — Protocole de validation sur machine [P0] — ☐ À faire
- À faire : grille d'essai (densités, compensations, sous-couches, longueurs,
  satin de largeurs croissantes, lettres de 3 à 20 mm) sur 3–4 tissus ;
  protocole écrit (machine, fil, aiguille, entoilage) ; photos et mesures
  archivées ; valeurs par défaut ajustées d'après les résultats ; chaque
  réglage par défaut documenté avec sa source (essai n°).
- Modules : `docs/validation/`, `tests/fixtures/validation/`.
- Note : demande l'intervention humaine de l'utilisateur (machine réelle) —
  l'agent prépare les fichiers d'essai et le protocole, puis attend les
  retours.

### HP-PHYS-002 — Comparatif Hatch vs OpenStitch sur un jeu commun [P1] — ☐ À faire
- À faire : mêmes images numérisées dans les deux logiciels, brodées ;
  tableau comparatif (temps de numérisation, points, coupes, rendu).

### HP-PHYS-003 — Programme de testeurs [P1] — ☐ À faire
- À faire : version bêta distribuée à des brodeurs (associations, fablabs),
  formulaire de retour, suivi des tickets.

---

## 27. Atouts d'OpenStitch à préserver (ne pas régresser)

Ce qu'OpenStitch fait déjà mieux ou différemment de Hatch — toute entrée
ci-dessus doit les respecter :

- **Libre et gratuit** (Apache-2.0) ; cœur portable, CLI scriptable (Hatch
  n'a pas de CLI).
- **Déterminisme** : même projet → mêmes octets DST ; tests d'aller-retour.
- **Paramètres toujours modifiables** : les points sont régénérés, les
  retouches manuelles sont des deltas (ADR-014).
- **Remplissage directionnel** avec guides, lignes de rupture et aspect fait
  main (« peinture à l'aiguille ») — rare, même chez les logiciels payants.
- **Satin topologique** (réseaux Y/T/croix, guides liés, jonctions ancrées)
  documenté en profondeur (`satin.md`).
- **Segmentation IA** (SAM) intégrée.
- Import SVG/DXF pour les utilisateurs de CAO (Fusion 360).

---

## 28. Journal des mises à jour de ce fichier

| Date | Qui | Changement |
|---|---|---|
| 2026-09-22 | Claude (session d'audit) | Création : inventaire complet vérifié dans le code. |
| 2026-09-23 | Claude (session pilotée) | HP-FILE-001 ☑ : action « Nouveau projet » (Ctrl+N) avec garde des modifications non enregistrées et réinitialisation centralisée de tout l'état d'édition de la fenêtre. |
