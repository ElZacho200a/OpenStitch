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

## État d'avancement (revu le 2026-10-18 : tableau recalculé ; statuts hors vague 1 non revérifiés)

> **Source de vérité des statuts : ce fichier.** `specs/implementation-roadmap.md`
> décrit l'*ordre de livraison* (vagues, scopes S1…S15, plans) et `docs/source/roadmap.md`
> la vue d'ensemble ; ils renvoient ici pour tout statut. Une entrée ne passe à ☑
> que si ses critères d'acceptation sont remplis **sur `main`** (code + tests), pas
> sur une branche ou une PR ouverte.

Audit du 2026-10-06 : chaque entrée a été confrontée au code de `main` (`40bf4c0`).
Revue partielle du 2026-10-13 (`main` = `4a8088b`) : vague 1 — objets, cadres, performance, rendu. Tableau recalculé depuis les titres des entrées (+4 le 2026-10-13, issues des veilles `docs/veille-innovations-2026-10.md` et `docs/veille-reduction-coupes-2026-10.md` : HP-ENG-001.a, HP-SEQ-006.a et .c en P1, HP-SEQ-006.b en P2). HP-SEQ-006, .a, .b et .c passent à ◐ le même jour (cœur livré en PR, non fusionné). Les autres sections n'ont pas été revérifiées et peuvent être en retard sur le code.

| Priorité | ☑ Fait | ◐ Partiel | ☐ À faire | Total |
|---|---|---|---|---|
| P0 | 12 | 26 | 6 | 44 |
| P1 | 10 | 62 | 69 | 141 |
| P2 | 3 | 31 | 62 | 96 |
| P3 | 0 | 3 | 12 | 15 |
| **Total** | **25** | **122** | **149** | **296** |

Ajoutés le 2026-10-18 (audit UX/UI, `docs/ux/`) : **+29 entrées ☐** (12 P1, 14 P2, 3 P3 ; HP-UX-040 retiré le jour même) :
HP-UX-017 à HP-UX-044, HP-PERF-009 et HP-QA-008 (§20, §22, §23). Aucun statut existant n'a été
modifié ; HP-UX-001/002/003/004/009/012/016 sont seulement précisés. Quatre entrées passent ◐ dans la foulée (HP-UX-017, 018, 021, 022 : voir le journal §28) ; puis HP-UX-019, 020 et 023.

**Fait (25)** : FILE-001/002/003/004 (nouveau projet, enregistrer, récents, autosave),
THR-001/003/004 (bibliothèque de fils, fil le plus proche, fil assigné à chaque objet),
FMT-001 (normalisation machine), TXT-001 et TXT-009 (lettrage), STI-004 (satin de bordure),
PERF-005 (génération parallèle), OBJ-003 (déplacement au clavier), VEC-004 (couteau), OBJ-014 (design entier), PERF-003 (auto-numérisation rapide).
Ajoutés le 2026-10-18 (cœur + bureau + tests) : VEC-007 (lisser), VEC-008 (courbes), VEC-010 (règle), VEC-014 (formes prédéfinies), FMT-009 (compagnon de couleurs), FMT-011 (export d'image), PROD-005 (consommation de fil), THR-007 (Mes fils), TXT-007 (crénage manuel).

**Fusionné le 2026-10-13** : #25 (fondu, fusion de zones, filtres vectoriels, cadre), #26 (performance), #27 (objets : OBJ-002/004/006/012/014/018). **En PR** : second lot (OBJ-004 poignée, OBJ-018 pastille, VEC-002, VEC-003, VEC-005).
S3 sélection/presse-papiers (PR #3, documentation seulement, **aucun code**).

**Livré hors entrée dédiée** : mode Contours / Line Art (PR #4, 2026-10-05) — fait avancer
AUTO-001/002/005/008/009, VEC-002, STI-004 (brique), sans en clore aucune.

**P0 encore ouverts, par thème** (38 entrées ; détail dans les sections) :
formats machine (FMT-001…005) · fils (THR-002/005) · lettrage (TXT-001/003/004) ·
moteur (STI-004, ENG-001/002/008/010) · auto-numérisation (AUTO-001/003/009) ·
manipulation d'objets (VEC-002, OBJ-001/002/003/004/006/014/016/018, SEQ-003) ·
visualisation et production (VIEW-001, PROD-001) · transverse (UX-006, PERF-001/002,
I18N-001/002) · distribution et validation (DIST-001, PHYS-001).

**Preuves humaines requises** pour clore certaines entrées (HP-PHYS-001, HP-AUTO-001,
HP-DIST-001 …) : machine réelle, scans Hatch, certificat de signature.

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

### Chantier transverse — Ergonomie (audit du 2026-10-18)

Les lots A à E de §20 (HP-UX-017…044, HP-PERF-009, HP-QA-008) ne forment pas une vague : ils se
**glissent dans les vagues 1 à 3** selon les fichiers touchés. Ordre recommandé (voir
`docs/ux/strategie-interface.md` §4) : HP-QA-008 → lot A en parallèle du lot B → lot C
(après HP-PERF-001 et, pour les fichiers communs, HP-QA-001) → lot D → lot E. En mode piloté,
un lot A ou B est un bon choix quand la vague en cours est bloquée par une preuve humaine.

---

## 2. Formats de fichiers et interopérabilité (FMT)

Aujourd'hui : **DST** (lecture/écriture, `libs/formats/src/dst.cpp`),
**DXF** (sous-ensemble LINE/LWPOLYLINE/CIRCLE/ARC), **SVG** import (formes +
path complet + transformations), **SVG diagnostic** en export, `.osp` natif.
Hatch lit et écrit une vingtaine de formats machine et plusieurs formats
graphiques. C'est **le premier mur** : la majorité des machines domestiques
(Brother, Babylock, Janome, Husqvarna, Pfaff, Bernina) n'utilisent pas DST.

### HP-FMT-001 — Couche de normalisation machine séparée des codecs [P0] — ☑ Fait (2026-10-06)
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
- Livré : `libs/formats/include/openstitch/formats/machine.hpp`/`machine.cpp`
  (`MachineConstraints`, `MachineRecord`, `normalize_for_machine`),
  `format_registry.hpp`/`.cpp` (AI-02, une entrée `"dst"` pour l'instant ;
  S2b/S2c s'y inscriront sans toucher au reste). `dst.cpp` migré sur cette
  couche, signatures publiques `encode_dst`/`decode_dst`/`write_dst_file`/
  `read_dst_file` inchangées. Bloc de couleur partagé `stitch::ColorBlock`
  (AD-02 ter). Composition générique projet ↔ fichier machine
  (`project_io::export_machine_file`/`import_machine_file`, AI-03b), table de
  blocs de couleur du design (`stitch_analysis::color_blocks`, AI-03a), nature
  « design importé » (`document::ImportedDesign`, AD-04 — remplace
  l'exception desktop `MainWindow::sequenceImported_`, mutation par
  `commands::SetImportedDesignCommand`, Tier 0). **Octet-pour-octet vérifié
  explicitement** : sortie de `encode_dst` sur deux séquences synthétiques
  (carré simple, séquence complexe trim/jump long/colorchange/stop) comparée
  byte à byte à la sortie de l'ancien `dst.cpp` (avant migration, capturée sur
  la même révision de ces séquences) — identique dans les deux cas ; filet de
  régression permanent dans `tests/unit/formats/test_dst_bytes_regression.cpp`
  et `tests/unit/project_io/test_machine_file.cpp`.
- Reste (hors entrée) : codecs PES (HP-FMT-002/003) et JEF/EXP
  (HP-FMT-004/005) — s'inscrivent dans le registre sans modifier cette couche ;
  aiguillage par nature de `commands::ICommand` pour éditer un design importé
  (S3, AD-05, hors P0).

### HP-FMT-002 — Export PES (Brother / Babylock / Bernette) [P0] — ◐ Partiel
- **Attend (humain)** : ouverture dans deux visualiseurs tiers et essai sur une machine réelle ; aucun travail de code restant identifié au 2026-10-13.
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

- Livré (2026-10-10) : PES v1 complet + bloc PEC (palette Brother 64, vignettes) ; v6 non fait ; options machine (coupes, arrêts, couleurs), dialogue
  d'export et import du bureau, CLI (`osp2dst`/`stats`/`dst2svg` par extension), analyse
  pré-export (`check_export_limits`), tests Catch2 (octets calculés à la main, aller-retour,
  déterminisme, fuzz). **Reste** : validation par visualiseurs tiers et machine réelle
  (non faite, aucun fichier de référence réel). Voir `docs/source/formats-pes-jef-exp.md`.
### HP-FMT-003 — Import PES [P0] — ◐ Partiel
- **Attend (humain)** : ouverture dans deux visualiseurs tiers et essai sur une machine réelle ; aucun travail de code restant identifié au 2026-10-13.
- État OpenStitch : absent. Seul DST s'importe comme séquence de points.
- Hatch : ouvre PES et récupère les couleurs.
- À faire : décodeur PES/PEC tolérant (même politique que `decode_dst` : ne
  plante jamais, erreur structurée), couleurs réelles récupérées et assignées
  au film couleur.
- Modules : `libs/formats`, `apps/desktop` (menu Fichier ▸ Importer).
- Acceptation : corpus de PES réels (versions variées) importés, fuzzing
  (HP-QA-003) sans crash.

- Livré (2026-10-10) : décodeur PES/PEC tolérant, couleurs PEC relues ; options machine (coupes, arrêts, couleurs), dialogue
  d'export et import du bureau, CLI (`osp2dst`/`stats`/`dst2svg` par extension), analyse
  pré-export (`check_export_limits`), tests Catch2 (octets calculés à la main, aller-retour,
  déterminisme, fuzz). **Reste** : validation par visualiseurs tiers et machine réelle
  (non faite, aucun fichier de référence réel). Voir `docs/source/formats-pes-jef-exp.md`.
### HP-FMT-004 — JEF / JEF+ (Janome, Elna) lecture + écriture [P0] — ◐ Partiel
- **Attend (humain)** : ouverture dans deux visualiseurs tiers et essai sur une machine réelle ; aucun travail de code restant identifié au 2026-10-13.
- État OpenStitch : absent.
- Hatch : oui, avec choix du cadre Janome dans l'en-tête.
- À faire : codec JEF (table de couleurs Janome, code de cadre dans l'en-tête
  — dépend du profil de cadre HP-HOOP-001).
- Modules : `libs/formats`.
- Dépend de : HP-FMT-001, HP-THR-001.
- Acceptation : idem HP-FMT-002.

- Livré (2026-10-10) : codec JEF (palette 78, code de cadre déduit de la taille) ; options machine (coupes, arrêts, couleurs), dialogue
  d'export et import du bureau, CLI (`osp2dst`/`stats`/`dst2svg` par extension), analyse
  pré-export (`check_export_limits`), tests Catch2 (octets calculés à la main, aller-retour,
  déterminisme, fuzz). **Reste** : validation par visualiseurs tiers et machine réelle
  (non faite, aucun fichier de référence réel). Voir `docs/source/formats-pes-jef-exp.md`.
### HP-FMT-005 — EXP (Melco / Bernina) lecture + écriture [P0] — ◐ Partiel (2026-10-18, `492754e`)
- **Attend (humain)** : ouverture dans deux visualiseurs tiers et essai sur une machine réelle ; aucun travail de code restant identifié au 2026-10-13.
- État OpenStitch : absent.
- Hatch : oui. Format simple, très répandu chez Bernina et en industriel.
- À faire : codec EXP (+ fichier compagnon de couleurs `.inf` optionnel).
- Modules : `libs/formats`.
- Dépend de : HP-FMT-001.

- Livré (2026-10-10) : codec EXP ; `.inf` non fait ; options machine (coupes, arrêts, couleurs), dialogue
  d'export et import du bureau, CLI (`osp2dst`/`stats`/`dst2svg` par extension), analyse
  pré-export (`check_export_limits`), tests Catch2 (octets calculés à la main, aller-retour,
  déterminisme, fuzz). **Reste** : validation par visualiseurs tiers et machine réelle
  (non faite, aucun fichier de référence réel). Voir `docs/source/formats-pes-jef-exp.md`.
- Livré (2026-10-18) : le `.inf` d'EXP est écrit et relu par le module compagnon de HP-FMT-009 (`265e4b8`).
- Livré (câblage bureau, 2026-10-18) : `.inf` d'EXP écrit et relu, branché au dialogue d'export, à l'import et au CLI.
- Reste : validation par un lecteur tiers et sur machine réelle (humain).

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

### HP-FMT-009 — Couleurs à côté du DST (fichier compagnon, en-tête étendu) [P1] — ☑ Fait (2026-10-18, `492754e`)
- État OpenStitch : le DST n'emporte que des arrêts couleur ; les couleurs
  réelles sont perdues à l'export.
- Hatch : peut écrire l'ordre des couleurs dans des fichiers d'accompagnement
  et sur la fiche de production.
- À faire : option d'export d'un fichier compagnon (`.inf` Melco, `.thr`,
  ou CSV/TXT lisible) listant les fils dans l'ordre ; relire ce compagnon à
  l'import d'un DST s'il est présent à côté.
- Modules : `libs/formats`.
- Dépend de : HP-THR-001.
- Livré (2026-10-18) : `libs/formats/color_companion.*` : fichier compagnon `.inf` (binaire) ou `.csv` (lisible) des fils par bloc de couleur, écriture déterministe, relecture à détection automatique (`read_color_companion_for` cherche `.inf` puis `.csv` à côté du motif), refus propre des contenus invalides ; `test_color_companion` (`265e4b8`).
- Livré (câblage bureau, 2026-10-18) : option « écrire aussi le fichier de couleurs » (`.csv` par défaut ou `.inf`) du dialogue d'export machine et de `osp2dst --color-file`, relue à l'import d'un DST/EXP (un compagnon invalide n'empêche pas l'import) ; tests.
- Reste : validation du `.inf` par un lecteur tiers (humain).

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

### HP-FMT-011 — Export image (PNG/JPG) du rendu brodé [P1] — ☑ Fait (2026-10-18, `033692a`)
- État OpenStitch : pas d'export image ; seul le SVG diagnostic existe (trait
  noir, pas de couleurs de fil).
- Hatch : export d'une image réaliste pour montrer au client.
- À faire : rendu hors écran du rendu réaliste (HP-VIEW-001) à une résolution
  choisie, fond transparent ou couleur tissu.
- Modules : `apps/desktop` (le rendu Qt), éventuellement un rendu cœur
  simplifié dans `formats` pour la CLI.
- Dépend de : HP-VIEW-001.
- Livré (2026-10-18) : rendu « présentation » Qt-free (`libs/stitch_render/presentation.*` : séquence entière, marge, résolution bornée par un plafond de pixels, fond tissu opaque ou transparent, alpha droit pour le PNG) et commande `openstitch-cli render <dst|pes|jef|exp|osp> <png>` (`--px-per-mm`, `--transparent`, `--fast`, `--json`). Un `.osp` utilise la séquence effective et les couleurs des objets ; un fichier machine, des couleurs de repli par bloc de fil. Tests : 6 cas `presentation :`, cas CLI `cli_render_png` ; rendu de `tentabrode` vérifié à l'écran.
- Livré (câblage bureau, 2026-10-18) : Fichier › « Exporter l'image (PNG, JPEG)… » : résolution, marge, fond tissu opaque ou transparent, qualité, JPEG ; rendu sur la séquence effective hors du fil UI si lourd, écriture atomique ; `test_export_image` ; le CLI `render` reste disponible.
- Reste : aucun.

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
- État OpenStitch : formes, `<path>` complet, `<g>`, transformations, **`viewBox`,
  `width`/`height` et unités** (`mm`/`cm`/`in`/`pt`/`pc`, `svg_import.cpp`).
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

### HP-FMT-021 — Migrations de schéma `.osp` systématiques [P1] — ◐ Partiel (2026-10-18)
- État OpenStitch : lecture tolérante `value(clé, défaut)` ; refus clair d'un fichier de
  version future (`schemaVersion > kSchemaVersion`, `project_io.cpp`) ; toujours pas de table
  de migrations ni de corpus `tests/fixtures/osp/`.
- À faire : table de migrations versionnées `vN → vN+1` testées une à une ;
  corpus d'anciens `.osp` figés dans `tests/fixtures/osp/` relus à chaque
  build ; refus clair d'un fichier d'une version *future*.
- Modules : `libs/project_io`.

---

## 3. Fils, couleurs et palettes (THR)

Aujourd'hui : un **RGB par objet**, plus un catalogue de fils autonome
(`libs/thread_palette`, HP-THR-001/002/003) pas encore relié au document
(`palettes-and-threads.md`, section *Catalogue*). Hatch est construit autour
des nuanciers de fabricants : c'est ce que l'utilisateur achète en magasin.
- Livré (2026-10-18) : `libs/project_io/migrations.*` : table de migrations versionnées v1→v7 contiguë (`static_assert`), appliquée par `load_project` avant la désérialisation ; refus `UnsupportedFormat` d'une version future ; un `project.json` forgé par version v1..v7 relu, déterminisme et idempotence (`97004b9`).
- Reste : corpus d'anciens `.osp` réels figés dans `tests/fixtures/osp/` (les anciens fichiers sont forgés en test).

### HP-THR-001 — Bibliothèque `thread_palette` [P0] — ☑ Fait (2026-10-05)
- À faire : lib cœur sans Qt : `Thread { brand, range, code, name, rgb,
  weight }`, `ThreadChart { name, threads }`, chargement depuis des fichiers de
  données (JSON/CSV) embarqués, recherche par code/nom.
- Modules : nouvelle `libs/thread_palette` (`openstitch::thread_palette`),
  tests `tests/unit/thread_palette/`.
- Acceptation : ≥ 1 nuancier chargé et interrogé en test ; déterminisme de
  l'ordre.
- Livré : `libs/thread_palette` (`openstitch::thread_palette`), lib cœur ne
  liant que `openstitch::core` (aucune dépendance tierce, aucune autre lib
  `openstitch::*`), construite sous le preset `msvc-debug`. Types valeur
  `ThreadKey { chart_id, code }`, `Thread { key, brand, range, name, rgb }`,
  `ThreadChart { chart_id, display_name, source_note, threads }`
  (`include/openstitch/thread_palette/thread.hpp`) — **données embarquées =
  constantes C++ compilées dans `libs/thread_palette/data/`, pas de
  JSON/CSV lu à l'exécution** (clarification du libellé ci-dessus : choix
  d'architecture délibéré, voir
  `specs/arch-plan/vision/20260929-110240-arm-1-ar-0.md` C-S1-02). Champ
  `weight` du libellé ci-dessus délibérément absent du type `Thread` —
  reporté à HP-THR-009 (P2), pas ajouté par erreur. Registre
  (`include/openstitch/thread_palette/catalog.hpp`, `src/catalog.cpp`) :
  `all_charts()`, `find_chart(chart_id)`, `find_by_code(chart_id, code)`,
  `search_by_name(needle)`, ordre déterministe (ordre textuel des appels de
  factory dans `catalog.cpp`, testé par
  `all_charts order is stable across two calls`). Tests :
  `tests/unit/thread_palette/test_catalog.cpp`
  (`test_thread_palette`, 11 `TEST_CASE`).
- Reste (hors entrée) : aucun consommateur ne lit encore ce catalogue
  (HP-THR-004) ; un seul nuancier aurait suffi à l'acceptation, deux sont
  livrés directement avec HP-THR-002.

### HP-THR-002 — Nuanciers des fabricants [P0] — ◐ Partiel (2026-10-05)
- **Attend (humain)** : transcription des cartes officielles Madeira/Isacord (question de licence) ; rien à coder d'ici là (2026-10-13).
- Hatch : Madeira (Polyneon, Rayon, Classic), Isacord, Robison-Anton,
  Sulky, Gunold, Marathon, Floriani, Coats, Brother, Janome, Pantone approx…
- À faire : fichiers de données par fabricant. **Point licence** : les
  associations code ↔ couleur sont des faits, mais une table recopiée d'un
  produit tiers peut être protégée ; sourcer depuis les cartes publiées par
  les fabricants et documenter la source dans `THIRD_PARTY_LICENSES.md`.
- Modules : `libs/thread_palette/data/`.
- Dépend de : HP-THR-001.
- Livré : deux nuanciers enregistrés dans le registre, dans cet ordre de
  déclaration : Madeira Polyneon 40 (`madeira_polyneon`,
  `libs/thread_palette/data/madeira_polyneon.cpp`) puis Isacord 40
  (`isacord_40`, `libs/thread_palette/data/isacord_40.cpp`), chacun avec son
  `source_note` et sa ligne dans la nouvelle sous-section « Nuanciers de
  fils » de `THIRD_PARTY_LICENSES.md` (source + date de consultation).
  **⚠️ Point licence non résolu, données PLACEHOLDER** : les deux fichiers
  ci-dessus contiennent des codes/noms/RGB **inventés** (forme plausible
  d'un nuancier réel : codes à 4 chiffres, noms de couleur usuels, RGB
  cohérent avec le nom), **pas** une transcription du vrai nuancier Madeira
  ou Isacord — l'environnement d'implémentation n'avait pas d'accès web pour
  sourcer les valeurs officielles (S1-POLICY-1 non appliquée pour le
  contenu, seulement pour la forme/structure). Chaque `source_note` le dit
  explicitement et cite l'URL à consulter. Passer cette entrée à `☑ Fait`
  seulement une fois les vraies cartes de couleurs transcrites depuis les
  sites officiels Madeira et Isacord.
- 2026-10-10 : **nuancier Générique** libre de droits (28 couleurs usuelles, aucune donnée
  de fabricant) et **import utilisateur CSV/JSON** (`project_io::read_thread_chart_file`,
  format dans `docs/source/palettes-and-threads.md`) : les cartes de marques réelles ne sont
  pas redistribuées, l'utilisateur charge les siennes ; les deux nuanciers placeholder sont
  signalés « données de démonstration fictives » dans l'interface. L'entrée reste ◐ tant
  que les cartes officielles ne sont pas transcrites.
- Reste (hors entrée) : transcription réelle des deux nuanciers (ci-dessus) ;
  les autres nuanciers de la liste Hatch (Robison-Anton, Sulky, Gunold,
  Marathon, Floriani, Coats, Brother, Janome, Pantone approximé) restent à
  ajouter, hors périmètre P0.

### HP-THR-003 — Fil le plus proche (distance perceptuelle) [P0] — ☑ Fait (2026-10-05)
- Hatch : « Match » vers le nuancier choisi.
- À faire : conversion sRGB → CIELAB (déjà faite dans `segmentation`, à
  factoriser dans `core` ou `thread_palette`), distance **CIEDE2000**, top-N
  candidats, filtre par gamme possédée (HP-THR-007).
- Modules : `libs/thread_palette`.
- Acceptation : tests contre les valeurs de référence publiées de CIEDE2000
  (jeu de Sharma).
- Livré : `include/openstitch/thread_palette/color_distance.hpp` +
  `src/color_distance.cpp`, module séparé du registre. `to_cielab(rgb)`
  écrite depuis les formules standard sRGB→linéaire→XYZ(D65)→CIELAB,
  indépendante de `cv::cvtColor` (`libs/segmentation` reste inchangée,
  `thread_palette` ne peut lier OpenCV). `ciede2000(a, b)` validée dans
  `tests/unit/thread_palette/test_color_distance.cpp` contre les 34 paires
  de référence publiées par Sharma, Wu & Dalal (2005), tolérance 1e-4 ; les
  34 paires passent. `nearest_threads(rgb, chart, top_n)` : top-N trié par
  distance croissante, égalités départagées par l'ordre de déclaration du
  nuancier, déterministe (testé par appels répétés). Filtre par gamme
  possédée explicitement hors périmètre (HP-THR-007, P1).
- Reste (hors entrée) : pas de filtre par gamme possédée (HP-THR-007). Depuis
  2026-10-10 la recherche est branchée à l'interface (`ThreadLibrary::nearest` : panneau
  Fils ▸ Catalogues « Fils les plus proches de la sélection » et « Couleurs libres → fil
  le plus proche », un pas d'annulation).

### HP-THR-004 — Fil assigné à chaque objet + remplacer une couleur partout [P0] — ☑ Fait (2026-10-10)
- Livré : `EmbroideryObject::thread` (`std::optional<ThreadKey>`, `rgb` reste la source de
  rendu) ; `commands::SetObjectThreadCommand` (un ou plusieurs objets, un seul pas
  d'annulation, états précédents restitués exactement) ; « remplacer ce fil par… » sur
  tout le motif (panneau Fils ▸ Projet) ; `.osp` schéma **v6** (champ `thread`, absent =
  couleur libre, migration testée depuis un v5) ; `ColorBlock::thread_key` renseigné ;
  API Qt-free `stitch_analysis::thread_usage` / `objects_using_thread`. Tests :
  `test_thread_commands.cpp`, `test_thread_persistence.cpp`, `test_thread_usage.cpp`,
  `test_thread_panel.cpp`. Le sélecteur est le panneau **Fils** (clic sur un fil du
  catalogue = assigner à la sélection multiple), pas la pastille de l'inspecteur
  (HP-OBJ-018).
- État avant : `EmbroideryObject::rgb` seul ; pas de sélecteur de couleur
  d'objet de broderie dans l'inspecteur (seulement la recoloration de région).
- À faire : `EmbroideryObject` porte une référence de fil optionnelle
  (`ThreadRef`) en plus du RGB d'affichage ; commande `SetObjectThreadCommand`
  (un ou plusieurs objets) ; « remplacer ce fil par… » sur tout le design ;
  sérialisation `.osp` + migration.
- Modules : `libs/document`, `libs/commands`, `libs/project_io`,
  `apps/desktop/properties_panel`.
- Dépend de : HP-THR-001 ; skill `openstitch-stitch-param` pour le champ persistant.

### HP-THR-005 — Film couleur / barre des couleurs du design [P0] — ◐ Partiel (2026-10-10)
- Livré : onglets *Projet* (fils utilisés, ordre de première couture, pastille, marque,
  référence, objets, points, longueur, durée estimée) et *Film couleur* (blocs dans
  l'ordre de couture) du panneau **Fils** (`apps/desktop/thread_panel.*`) ; sélection
  des objets d'un fil ou d'un bloc ; réordonnancement par glisser ou Monter/Descendre
  (`stitch_analysis::reorder_film_blocks` + `ReorderEmbroideryCommand`, un pas
  d'annulation, objets figés immobiles) ; fusion des blocs de même fil
  (`merge_same_thread_blocks`). Tests : `test_thread_usage.cpp`, `test_thread_panel.cpp`.
- Reste : **masquage d'un fil** depuis le film (un objet masqué sort de la séquence donc
  du film : il faut une vue qui garde les fils masqués) ; vignettes de motif par bloc ;
  barre de couleurs compacte sur le canevas ; le réordonnancement vit dans
  `stitch_analysis` et non dans `libs/optimization`.
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

### HP-THR-007 — Mes fils (stock possédé) et nuancier personnel [P1] — ☑ Fait (2026-10-18, `cc4db8a`)
- À faire : l'utilisateur coche les bobines qu'il possède ; le « plus proche »
  peut se limiter à son stock ; nuancier personnel éditable ; import/export
  CSV et formats courants de nuanciers (`.thr`, `.rgb`, `.edr`, `.gpl`) si
  documentés.
- Modules : `libs/thread_palette`, préférences `apps/desktop`.
- Livré (2026-10-18) : `thread_palette/thread_stock.*` : `ThreadStock` (fils possédés, nuancier personnel éditable), plus proche restreint au stock, JSON déterministe à relecture stricte ; 8 tests (`a5d2ccb`).
- Livré (câblage bureau, 2026-10-18) : onglet « Mes fils » du panneau Fils (cases « je possède », bascule « limiter la recherche du fil le plus proche à mes fils », nuancier personnel éditable), stock en JSON dans les préférences (hors `.osp`, hors annulation) ; la pipette respecte la bascule ; `test_thread_stock_ui`.
- Reste : import/export `.thr`/`.rgb`/`.edr`/`.gpl` ; édition d'un fil personnel existant (le ré-ajouter le remplace).

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

### HP-THR-011 — Réduire / fusionner les couleurs d'un design [P1] — ◐ Partiel (2026-10-10)
- Livré : `thread_palette::reduce_colors` (fusion agglomérative CIEDE2000, la plus lourde
  garde sa teinte, déterministe) ; bouton **Limiter à N fils** du panneau Fils (un seul
  pas d'annulation, sans aperçu préalable) ; option « Limiter à N fils » de la
  numérisation automatique (`AutoOptions::max_threads`). Tests : `test_library.cpp`,
  `test_thread_limit.cpp`, `test_thread_panel.cpp`.
- Reste : aperçu avant application ; fusion manuelle de deux fils choisis (le
  « Remplacer ce fil par… » de HP-THR-004 couvre le cas du fil du catalogue).
- État avant : la quantification existait au niveau **image** seulement.
- À faire : au niveau objets : fusionner deux fils, « réduire à N couleurs »
  (regroupement CIELAB des fils des objets), avec aperçu et une seule commande
  annulable.
- Modules : `libs/thread_palette`, `libs/commands`.

### HP-THR-012 — Pipette (couleur depuis l'image ou un objet) [P1] — ◐ Partiel (2026-10-18, `f38da87`)
- À faire : outil pipette : clic sur l'image → fil le plus proche proposé ;
  clic sur un objet → reprendre son fil.
- Modules : `apps/desktop`.
- Dépend de : HP-THR-003.

---

## 4. Lettrage et texte (TXT)

Aujourd'hui : **aucun objet texte**. Le lettrage est une des premières raisons
d'acheter Hatch (même le niveau Personalizer), avec des dizaines de polices
prénumérisées et la conversion des polices TrueType.
- Livré (2026-10-18) : `thread_palette/eyedropper.*` : `sample_color` / `sample_region` (moyenne entière déterministe sur RGB brut), `pick_thread` (plus proche dans un nuancier, la bibliothèque ou Mes fils) ; 8 tests (`a6b594e`).
- Livré (câblage bureau, 2026-10-18) : outil Pipette (touche I) : échantillonnage 5×5 de l'image affichée, menu des 5 fils les plus proches du nuancier courant ou de Mes fils, couleur libre, reprise du fil d'un objet brodé, assignation annulable par `SetObjectThreadCommand` ; `test_eyedropper_tool`.
- Reste : échantillonnage du rendu des points (seule l'image source est échantillonnée).

### HP-TXT-001 — Objet texte dans le modèle [P0] — ☑ Fait (2026-10-10)
- Fait : `document::TextObject`, lettres dérivées annotées `text_owner`, commandes
  `SetTextObjectCommand`/`RemoveTextObjectCommand`, `.osp` v6 + migration (voir
  `docs/source/lettering.md`, ADR). La base est une ligne droite (autres bases : HP-TXT-005).
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

### HP-TXT-003 — Conversion de polices TrueType/OpenType [P0] — ◐ Partiel (2026-10-10)
- Fait : FreeType (FTL) encapsulé dans `libs/lettering`, contours → `PathSet` µm, crénage `kern`,
  catalogue des polices installées côté desktop. Reste : HarfBuzz (GPOS, ligatures).
- Hatch : convertit n'importe quelle police installée.
- À faire : lecture des contours de glyphes via **FreeType** (licence FTL,
  compatible — **pas l'option GPLv2**) et mise en forme via **HarfBuzz** (MIT)
  pour le crénage/ligatures ; contours → `geometry::Path` en µm ; énumération
  des polices système (côté desktop seulement, cœur agnostique).
- Modules : `libs/lettering` (FreeType/HarfBuzz encapsulés, jamais exposés),
  `vcpkg.json`, `THIRD_PARTY_LICENSES.md`.
- Dépend de : HP-TXT-001.

### HP-TXT-004 — Lettrage satin automatique par lettre [P0] — ◐ Partiel (2026-10-10)
- Fait : auto-satin par squelette par morceau de glyphe, repli tatami (trait large, squelette
  insuffisant : 4, 6, 9, R en Vera) ou contour, alphabet complet 100 % cousu sans débordement
  (test). Reste : SVG dorés, ordre/connecteurs internes à la lettre.
- Hatch : une lettre TrueType devient des colonnes satin qui suivent les
  traits, pas un tatami.
- À faire : réutiliser l'auto-satin par squelette (`auto_satin`) sur chaque glyphe
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

### HP-TXT-007 — Espacement, crénage, alignement, interligne [P1] — ☑ Fait (2026-10-18, `164dcf5`)
- Fait : espacements lettres/mots, interligne, gauche/centre/droite/justifié, multilignes,
  crénage `kern`. Reste : crénage manuel par paire.
- À faire : espacement des lettres / des mots, interligne, alignement
  gauche/centre/droite/justifié, texte multilignes, crénage automatique
  (police) + manuel par paire.
- Modules : `libs/lettering`, inspecteur.
- Livré (2026-10-18) : crénage manuel par paire dans le cœur (`LayoutOptions::kerning_overrides`, remplace le crénage de la police) ; tests de crénage exact, remplacement, lignes vides alignées, tabulation et CRLF (`aaffb09`).
- Livré (câblage bureau, 2026-10-18) : crénage manuel par paire dans `document::TextObject` (`kerning_pairs`), saisi dans le dialogue texte (« AV: -0,3 mm »), persistant (`.osp` v8 écrit seulement si utilisé, migration v7→v8, anciens fichiers réécrits à l'identique) ; « agrandir le texte » et l'import CLI en tiennent compte (`lettering::layout_options_for`) ; tests.
- Reste : crénage GPOS (seule la table `kern` est lue).

### HP-TXT-008 — Édition individuelle d'une lettre [P1] — ☐ À faire
- Hatch : sélectionner une lettre d'un texte pour la déplacer, tourner,
  redimensionner, recolorer sans casser l'objet texte.
- À faire : décalages par lettre stockés dans l'objet texte (deltas, comme
  les overrides de points), poignées dédiées.
- Modules : `libs/lettering`, `libs/commands`, `apps/desktop`.

### HP-TXT-009 — Avertissement taille minimale [P1] — ☑ Fait (2026-10-10)
- Fait : `stitch_analysis::analyze_text_objects` (< 5 mm satin fragile, < 3 mm illisible, trait
  < 1 mm), affiché dans le panneau Analyse et le dialogue de texte.
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

### HP-TXT-012 — Unicode complet et langues [P1] — ◐ Partiel (2026-10-18, `164dcf5`)
- À faire : accents, ligatures, écritures non latines (cyrillique, grec),
  RTL (arabe, hébreu) via HarfBuzz ; gestion des glyphes absents (repli et
  avertissement, jamais un carré silencieux).
- Modules : `libs/lettering`.
- Livré (2026-10-18) : `libs/lettering/unicode.*` (sans HarfBuzz) : caractères sans rendu ignorés, recomposition/décomposition d'accents latin, grec et cyrillique, marques combinantes, repli des glyphes absents (`glyphe-remplace`), ligatures f optionnelles, ordre visuel hébreu, arabe signalé non façonné (`ecriture-non-supportee`), UTF-8 invalide sans crash ; `test_unicode` (`aaffb09`).
- Livré (câblage bureau, 2026-10-18) : case « Ligatures » du dialogue texte (champ `TextObject::ligatures`, persistant) en plus du cœur Unicode (accents, repli, hébreu).
- Reste : façonnage arabe et écritures indiennes (HarfBuzz, nouvelle dépendance à valider en licence), ligatures propres à la police, GPOS.

### HP-TXT-013 — Réglages adaptés aux petites lettres [P1] — ◐ Partiel (2026-10-18)
- Fait : sous 8 mm, sans compensation ni bord, sous-couche centrale selon le trait, satin →
  contour sous 1 mm (règles dans `lettering.md`). Reste : densité ajustée, tests de rendu.
- À faire : sous la hauteur X : pas de sous-couche ou sous-couche centrale
  seulement, densité ajustée, compensation réduite, satin → contour sous le
  seuil de largeur ; règles documentées et testées.
- Modules : `libs/lettering`, `libs/stitch_generation`.
- Livré (2026-10-18) : densité satin et tatami +0 % à 8 mm jusqu'à +15 % à 4 mm et moins (jamais plus serrée que le réglage utilisateur), point de tatami = hauteur/3 borné 1,5–3 mm ; tests de paramètres, de séquence cousue à 6 mm, sans `saut-long`, déterministe (`ec80a25`).
- Reste : validation sur broderie réelle (la courbe +15 % est une heuristique de prudence) ; cas des générateurs de `libs/stitch_generation`.

### HP-TXT-014 — Connecteurs entre lettres [P1] — ☐ À faire
- À faire : ordre de gauche à droite, fin d'une lettre près du début de la
  suivante, trajet caché si possible sinon saut + coupe selon le seuil,
  réglage « couper entre les lettres » / « entre les mots ».
- Hatch (blog Wilcom *Minimizing thread trims*, 2020) : le connecteur d'un lettrage se règle
  « à l'intérieur de l'objet » (entre lettres) ou « après l'objet » ; un lettrage système est
  un seul objet. Pont possible avec HP-SEQ-006 pour les lettres qui se chevauchent.
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
- État OpenStitch : simple / aller-retour / triple (`RunningStitchParams::repeats`) ;
  `RepeatMode::Backstitch` et `BeanStitch` existent dans `apply_repeat_mode`
  (`running_stitch.cpp`, testés) mais **ne sont pas exposés** dans les paramètres ni l'UI ;
  pas de point tige.
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

### HP-STI-004 — Satin de bordure à largeur fixe le long d'un chemin [P0] — ☑ Fait (2026-10-10)
- Livré : `border_satin_from_path/_region` (`libs/stitch_generation/border_satin.cpp`),
  `BorderSatinSpec` (largeur 0,5–20 mm, côté centré/intérieur/extérieur, coins vifs
  ou arrondis, anneaux extérieur et trous, tracés ouverts), création et régénération
  dans l'inspecteur (`main_window_engine.cpp`), `.osp`, suit le déplacement du vecteur.
  Tests `test_engine_points.cpp` (« bordure : … ») : cercle et tracé en S réguliers, sans
  croisement de barreaux. Reste : largeur variable par nœud, coins intelligents (HP-ENG-005),
  mise à l'échelle du vecteur sans régénération manuelle, validation physique.
- Brique existante : `strip_polygon(centerline, demi-largeur)` (`contour_objects.cpp`) sert au
  mode Contours ; aucun outil utilisateur ni commande `libs/commands` pour « chemin ouvert + largeur fixe ».
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

### HP-STI-010 — Remplissage en dégradé (densité variable) [P1] — ◐ Partiel (directionnel)
- Fait : `DirectionalFillParams::density_gradient` (axe + écart de début/fin,
  interpolation linéaire, borné à [0,1 ; 4] mm), tracé Jobard & Lefer à
  écart local, `.osp`, déplacement/échelle, inspecteur (case, écart de fin,
  angle). Reste : tatami à espacement variable, satin, plusieurs points de
  contrôle, poignée d'axe sur le canevas.
- Source de la méthode : Liu et al., *Directionality-Aware Design of
  Embroidery Patterns*, CGF 42(2), 2023 (DOI 10.1111/cgf.14770), reprise de
  l'idée sans code tiers.
- Hatch : Gradient fill : densité qui varie dans la forme (transparence).
- À faire : profil de densité le long d'un axe (points de contrôle), tatami à
  espacement variable ; option satin à densité variable.
- Modules : `libs/stitch_generation`.

### HP-STI-011 — Mélange de couleurs (color blending) [P2] — ◐ Partiel (action disponible)
- 2026-10-13 (intégration UI après PR #36) : dialogue avant mutation, aperçu
  schématique, couleurs manuelles sans raster et proposition depuis l'image,
  angle/écarts/inversion ; dessus inséré après le fond ; choix et édition des
  couches dans Propriétés. Tests des gestes, conversion et round-trip `.osp`.
  Le couple à dégradés complémentaires liés reste à faire.
- 2026-10-17 : **zone de transition réglable** (début / fin en % de l'axe, aplats
  aux extrémités ; `narrow_gradient_zone`, dialogue du fondu ; l'inspecteur
  conserve l'axe quand seul l'écart de fin change). Aucun changement du `.osp`.
  Critère : sur un rectangle, avec une zone 30 % – 70 %, écarts constants
  (début puis fin) hors de la zone (test `narrowed gradient zone gives constant
  spacing on both plateaus`).
- 2026-10-18 : **courbe de transition** (linéaire / douce / marquée ;
  `DensityGradient::curve`, clé `.osp` `curve` écrite hors linéaire, `set_params`
  et `inspect` ; la même courbe sur les deux couches du profil complémentaire).
  Critère : début plus serré et fin plus lâche plus longtemps, écart identique au
  milieu (test `density curve reshapes the ramp without moving its midpoint`).
  Reste : points de contrôle, réglage de la largeur de fil, courbe dans
  l'inspecteur.
- 2026-10-13 (PR #25, fusionnée le 2026-10-13) : correction du fondu écrasé (lignes du dessus orientées perpendiculairement à l'axe de couleur) et nouvelle action **Forme ▸ Fondre deux zones**.
- Fait : `autodigitize::analyze_two_color_blend` (ACP en CMY → deux couleurs,
  plan de proportion → `DensityGradient`), `thread_palette::best_thread_pair`
  (ΔE2000 + contraste WCAG), `BilateralDenoiseOp`. Voir *Fondu de couleurs à
  deux fils* dans `directional-fill.md`. Action **Broderie ▸ Fondu de couleurs
  à deux fils** (fond + remplissage de dessus, fils du nuancier du panneau Fils,
  une seule annulation). Reste : liaison entre les deux objets, dégradés non
  linéaires, réglage de la largeur de fil.
- Hatch : deux couches de dégradés inverses qui se mélangent.
- À faire : couple d'objets liés à dégradés complémentaires générés ensemble.
- Dépend de : HP-STI-010.

### HP-STI-012 — Point de croix [P2] — ☐ À faire
- Hatch : outil point de croix sur grille (Hatch intègre un module Cross Stitch).
- À faire : grille, cellules → croix (ordre des jambes cohérent), import d'une
  image pixélisée en grille, fil par cellule.
- Modules : `libs/stitch_generation`, `apps/desktop`.

### HP-STI-013 — Stipple / méandre (quilting) [P2] — ◐ Partiel
- État OpenStitch : stippling d'image ✓ (`libs/stipple`, `digitize --mode
  stipple`, voir `auto-numerisation.md`) ; reste le remplissage d'une zone
  vectorielle (méandre) et le réglage dans le desktop.
- À faire (zone) : chemin aléatoire (graine fixe) qui remplit une zone sans se croiser
  (courbe de remplissage type Hilbert ou TSP-art), espacement réglable.
- Veille (§V6, résumés lus) : Kaplan & Bosch (Bridges 2005) distribuent des
  « villes » par pointillé de Voronoï pondéré ou tramage ordonné, puis
  résolvent un TSP heuristique ; Li & Mould (2014) remplacent la tournée par un
  arbre. Risque : densité de fil non contrôlée ; l'imposer explicitement.
  Priorité P3, après le pointillé d'image déjà livré.
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

### HP-STI-017 — PhotoStitch (photo en lignes) [P2] — ◐ Partiel
- État OpenStitch : version monochrome en semis de points reliés par un tour
  TSP (`digitize --mode stipple`). Reste : trame par lignes, couleur par
  couches.
- Hatch : PhotoFlash / Color PhotoStitch : une photo devient des lignes de
  points de densité variable.
- À faire : trame par lignes dont la densité suit la luminance (mono), puis
  version couleur par couches.
- Modules : `libs/autodigitize` ou nouveau `libs/photostitch`.

### HP-STI-018 — Satin et tatami « tournants » [P1] — ◐ Partiel
- État OpenStitch : satin à barreaux (angle variable) ✓ ; remplissage
  directionnel tatami (champ de directions) ✓ — les deux briques existent
  indépendamment, mais rien ne les relie : une forme large/ronde refusée par
  l'auto-satin tombe en tatami à **angle fixe** (ou sans repli du tout),
  jamais en direction tournante.
- Recherche brevets : EP0761860B1 motive les axes/lignes caractéristiques et
  orientations interpolées ; US6390005B1 motive la vigilance sur l'espacement
  des points en virage. Voir `docs/source/patent-research.md`.
- État R&D 2026-10 : le satin tournant `IsoOffsetRing` existe pour les formes
  compactes/anneaux et expose désormais sa provenance via
  `RailConstructionMethod::IsoOffsetRing` dans `satin_column_view`, ce qui évite
  aux consommateurs de confondre anneau iso-offset et colonne issue d'un axe
  médian. Reste expérimental, sans validation machine.
- État R&D 2026-10, suite : HP-STI-018 Phase B.5b est câblée côté
  `extend_tip`. Les fourches `y`/`y_symmetric` passent désormais sous le chemin
  corridor sans croisement des premiers barreaux ; `trident` reste rapporté
  comme pointe effilée distincte.
- Hatch : Turning satin / Complex turning — le remplissage **satin lui-même**
  suit la courbure (la direction du point tourne), sans jamais changer de
  type de point.
- À faire : décision de conception à trancher avant tout code — (a) brancher
  automatiquement le remplissage directionnel tatami existant en repli
  (contournement rapide, mais reste du tatami, pas du satin) vs (b) un vrai
  mode satin tournant (zigzag entre deux rails radiaux courbes) pour une
  parité réelle avec Hatch. Valider sur cercle, disque, pétale.
- Modules : `libs/autodigitize`, `libs/stitch_generation`, `libs/auto_satin`
  si (b) ; la couverture se vérifie via `SkeletonSatinDiagnostics` (`satin_coverage` supprimé).
- Acceptation : un disque de 15 mm et un pétale produisent un remplissage
  dont la direction suit la courbure (métrique : régularité de la direction
  des fils, cf. HP-ENG-009) et ne tombent jamais silencieusement sur un
  tatami à angle fixe.
- R&D brevets (2026-10) : le moteur de traversées guidé par squelette
  (RD-PAT-001) et la réintégration d'un auto-satin explicite et protégé dans
  `autodigitize` (RD-PAT-002) sont suivis dans `docs/roadmap-rd-brevets.md`.
  Prérequis bloquant : corpus et métriques satin vs repli (RD-PAT-003).
- 2026-10-12 : squelette plus stable face au bruit de contour —
  `GraphCleanupParameters::max_protrusion` / `max_protrusion_to_radius`
  (élagage à erreur bornée, voir `satin-squelette.md`), activé par défaut dans
  `SkeletonSatinParameters`. Corpus propre inchangé ; contours bruités : moins
  de colonnes parasites (bruit ±0,2 mm : h 19 -> 15 ; ±0,3 mm : E 53 -> 43). Reste :
  mesure sur contours réels de photos, axe médian sur polygone.
- Suite recommandée : préparer le cutover Phase F seulement après validation du
  corpus complet et décision explicite sur les limites restantes (`trident`,
  `multi_neck`) ; ne pas prolonger SGSD comme stratégie principale sans preuve
  de couverture sur le corpus.

### HP-STI-018.a — Assistant de guides directionnels par squelette [P1] — ☐ À faire
- Contexte : le remplissage directionnel existe déjà (`DirectionalFillParams`
  avec `guides` et `break_lines`), mais l'utilisateur doit tracer ses guides à
  la main. Le moteur auto-satin possède déjà une brique déterministe de
  médiane de forme : rasterisation → distance field → squelette Zhang-Suen
  (`thin_zhang_suen`) → `SkeletonGraph` avec `SkeletonEdge::centerline`.
- Objectif : proposer une action d'aide qui construit automatiquement un ou
  plusieurs guides directionnels depuis la zone sélectionnée, afin de créer un
  remplissage directionnel exploitable sans devoir dessiner le premier guide à
  la main. Cette entrée ne doit PAS réintroduire de satin automatique dans
  l'auto-broderie : elle sert uniquement d'assistant pour le type
  directionnel.
- Première étape obligatoire avant code : ouvrir une PR de plan/design depuis
  `origin/main` (ex. `feature/directional-auto-guides`) et documenter les
  décisions ci-dessous. Ne pas commencer par l'UI.
- À faire cœur : ajouter une fonction pure du type
  `directional_guides_from_region(region, options) -> vector<Path>` qui :
  réutilise `auto_satin::analyze_region` / `SkeletonGraph`, filtre les arêtes
  trop courtes, choisit un chemin principal déterministe pour les formes
  simples, lisse/ré-échantillonne légèrement la centerline et retourne des
  `geometry::Path` ouverts compatibles avec `DirectionalFillParams::guides`.
- À faire UI : action sur un objet tatami ou directionnel sélectionné,
  probablement « Générer un guide de direction depuis la forme ». Sur tatami,
  convertir en remplissage directionnel puis ajouter le guide ; sur directionnel,
  proposer au minimum Ajouter vs Remplacer les guides existants.
- Décisions à trancher dans le plan avant implémentation : (1) forme branchée =
  un guide principal ou plusieurs guides ? (2) guides existants = remplacer,
  ajouter ou demander ? (3) action disponible depuis tatami uniquement,
  directionnel uniquement, ou les deux ? (4) cas cercle/quasi rond = refus
  propre, guide ambigu, ou plusieurs guides radiaux ?
- Modules : `libs/auto_satin` (lecture squelette existant), nouveau helper
  plutôt côté `libs/stitch_generation` ou petit module dédié sans Qt,
  `libs/document` seulement si options persistantes nécessaires,
  `libs/commands` + `apps/desktop/main_window_directional.cpp` pour l'action
  annulable.
- Tests cœur : rectangle long → guide central droit ; forme courbe simple →
  guide courbe dans l'axe ; T/Y → résultat déterministe documenté ; cercle →
  refus ou diagnostic stable ; aucun guide hors de la région ; sortie
  identique entre deux exécutions.
- Tests desktop : action disponible sur objet directionnel/tatami selon la
  décision ; guide ajouté/remplacé ; undo/redo exact ; aucune mutation si la
  génération échoue.
- Acceptation : un utilisateur sélectionne une zone remplissable, déclenche
  l'assistant, obtient un remplissage directionnel avec au moins un guide
  éditable, puis peut ajuster ce guide avec les outils existants ; toute la
  mutation est une seule commande annulable.
- Décisions tranchées (2026-10) : (1) forme branchée = un guide par branche
  significative (principal + branches, max 8) ; (2) guides existants = remplacés,
  y compris le guide par défaut issu de la conversion tatami. Voir
  `docs/source/directional-fill.md` § Interface.

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

### HP-STI-024 — Satin complexe avec trous [P2] — ◐ Partiel
- **Mise à jour 2026-10** : le moteur par squelette (branche de recherche, non fusionné sur `main`,
  sans essai machine) traite les anneaux par axe fermé (couverture estimée ≥ 0,99) et `two_holes`
  (0,994) sur le corpus ; `satin_planning` n'existe plus. Reste : essais machine et grands trous.
- État OpenStitch : l'auto-satin refuse les anneaux larges / formes à trous
  complexes ; un anneau fin est géré, mais en le **découpant** en 4 sections
  (`satin_planning`), pas par une vraie gestion native des trous. Une région
  à 2+ trous à contour extérieur **convexe** (`two_holes`, corpus de
  torture) n'a aujourd'hui **aucune stratégie** : aucune famille de coupe
  actuelle n'a de sommet reflex à exploiter sur un contour convexe.
- Hatch : Column C / Complex fill — un remplissage satin unique qui
  contourne nativement les trous, sans découpage visible côté utilisateur.
- À faire : nouvelle famille de coupe « entre deux trous » dans
  `libs/satin_planning` (aucune des familles existantes — normale au
  squelette, concavité, JunctionSeparator — ne s'applique à un contour
  convexe) ; ne pas patcher les familles existantes sous la pression de
  cette seule fixture.
- Modules : `libs/auto_satin`. Lire `satin-squelette.md`.
- Acceptation : la fixture `two_holes` (`test_torture_corpus.cpp`) atteint
  une couverture significative sans refus total.

---

## 6. Qualité du moteur et réglages textiles (ENG)

Hatch applique automatiquement des règles de métier (« stitch processing ») que
l'utilisateur ne voit pas. OpenStitch a les briques mais peu d'automatismes.

### HP-ENG-001 — Compensation d'étirement du tatami [P0] — ◐ Partiel (2026-10-10)
- Livré (tatami) : `TatamiParams::pull_compensation` (0–3 mm, défaut 0, réglage manuel :
  le modèle n'a pas de notion de tissu), rangées allongées dans l'axe du fil, test
  géométrique exact (dépassement = `pull` au µm près), inspecteur, `.osp`, annulable.
  Reste : remplissage directionnel, validation physique HP-PHYS-001.
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

### HP-ENG-001.a — Pièce-test de calibration et table de compensation par tissu [P1] — ☐ À faire
- Source : veille `docs/veille-innovations-2026-10.md` §V1. Aucune source lue ne
  fournit de modèle physique de l'étirement : les valeurs publiées sont des
  règles empiriques, dépendantes du tissu et non vérifiées. On mesure donc
  plutôt qu'on ne modélise. Idée de **notre conception** ; seule l'expression
  de la compensation en pourcentage de sur-couture est reprise de US5343401A
  (expiré selon Google, non vérifié par un juriste).
- État OpenStitch : `pull_compensation` est un réglage manuel par objet, sans
  lien avec la largeur de colonne ni le tissu (voir HP-ENG-001).
- À faire : (1) générateur de pièce-test déterministe (colonnes satin de
  largeurs croissantes, carré de tatami, disque, chacune de dimension nominale
  connue) exporté en DST ; (2) saisie des dimensions mesurées après broderie ;
  (3) table « largeur nominale → compensation » ajustée à ces mesures,
  sérialisée dans le `.osp` sous un nom de profil de tissu ; (4) application de
  la table par le générateur, via `effective_sequence`, quand la compensation
  d'un objet est « Auto ».
- **Ne pas** reproduire la compensation « push » par table largeur-de-bout /
  tissu décrite dans US8851001B2/US9702070B2 (Melco, affichés actifs) ; voir
  HP-FAB-001 et `docs/roadmap-rd-brevets.md` §« Brevets à éviter ».
- Modules : `libs/stitch_generation` (pièce-test, application),
  `libs/document` (profil), `libs/project_io` (schéma), `apps/desktop` (saisie).
- Dépend de : HP-ENG-001 (tatami et directionnel), HP-FAB-001 pour le choix du
  profil par projet.
- Acceptation : deux générations de la pièce-test donnent des DST identiques
  octet pour octet ; aller-retour `.osp` de la table ; avec une table nulle, la
  séquence est strictement identique à l'actuelle ; validation physique
  HP-PHYS-001 (broderie réelle, mesures consignées dans la doc).

### HP-ENG-002 — Sous-couche automatique selon la forme [P0] — ◐ Partiel (2026-10-06)
- Livré 2026-10-10 : `UnderlayMode::Auto` (défaut Manual) pour tatami, directionnel, satin
  et auto-satin, seuils documentés (`moteur-de-points.md` §10) et testés, réglable et
  désactivable dans l'inspecteur. Reste : Auto n'est pas encore le défaut des objets créés
  à la main (créés en Manual pour ne rien changer) ; pas de zigzag pour les grands tatamis.
- Livré avant : l'auto-numérisation choisit la sous-couche du tatami selon l'aire (Lot B,
  `auto_fill_underlay`) ; le satin a `center_underlay` par défaut. Manque : mode
  Auto/Manuel au niveau du document pour les objets créés à la main, et seuils par
  largeur de satin.
- État OpenStitch : sous-couches désactivées par défaut (tatami) ou booléennes
  (satin) ; l'utilisateur doit savoir quoi cocher.
- Hatch : sous-couche choisie automatiquement selon la largeur/le type
  (satin fin : centre ; moyen : bord ; large : bord + zigzag ; tatami : bord +
  parallèle).
- À faire : mode « Auto » (défaut pour les nouveaux objets) qui choisit selon
  des seuils documentés et testés ; les anciens projets gardent leurs réglages.
- Modules : `libs/stitch_generation`, `libs/document` (enum Auto/Manuel).

### HP-ENG-003 — Espacement satin automatique selon la largeur [P1] — ☐ À faire
- Note 2026-10 : le moteur par squelette adapte déjà le **pas le long de l'axe** à la
  courbure/rotation de l'orientation (US6390005B1), pas la densité selon la largeur : l'item reste ouvert.
- Hatch : Auto spacing : colonnes étroites plus lâches, larges plus serrées.
- Modules : `libs/stitch_generation/satin`.
- R&D brevets : US5343401A donne un exemple chiffré de densité modulée par la
  largeur ; suivi RD-PAT-013 (`docs/roadmap-rd-brevets.md`). Absent du code
  (vérifié 2026-10).

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
- Livré 2026-10-10 : `split_long_stitches`/`max_stitch_length` dans les finitions
  (découpage de tout point cousu trop long, tous types, testé : aucun point > max),
  options d'analyse alignées sur le projet, avertissements pré-export
  `parametre-hors-limites`. Reste : désactivé par défaut (aucun fichier existant ne
  change), invariant non testé sur tout le corpus.
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
- Livré 2026-10-10 : choix automatique du sens de couture de chaque objet
  (`auto_join` global + `join` par objet, `orient_chunk`), entrée des satins depuis la
  fin de l'objet précédent ; scène de référence : sauts 116,0 → 102,6 mm
  (`openstitch-cli engine-debug`). Reste : coût d'ordre par extrémités
  (`libs/optimization`), départ d'un contour fermé au sommet le plus proche, mesure sur
  les corpus « marine » et `tentabrode` (absents de ce dépôt).
- État OpenStitch : points d'entrée/sortie réglables (satin, tatami) ;
  routage multi-colonnes satin ; ordre des objets par centres (pas par
  extrémités : `OrderItem` n'a que `centroid`).
- Hatch : Auto start & end / Closest join : chaque objet commence au point le
  plus proche de la fin du précédent et finit près du début du suivant.
- À faire : après l'ordre, calcul des points d'entrée/sortie de chaque objet
  en fonction des voisins (tatami, directionnel, contour fermé : entrée sur le
  bord le plus proche) ; coût d'ordre basé sur ces points, pas les centres.
- Modules : `libs/optimization`, `libs/stitch_generation`.
- Acceptation : longueur totale des sauts réduite sur le corpus « marine » et
  `tentabrode` (mesurée, rapportée dans le commit).

- 2026-10-12 : pour le mode Contours, `ContourRouting::SinglePath`
  (`digitize --single-path`) relie les traits par un parcours eulérien
  (`route_single_path`) : moins de polylignes donc moins de sauts/coupes
  (grille de test : 7 -> 5 coupes), en repassant un court tronçon plutôt que
  de couper. L'ordre entre objets reste à faire.

### HP-ENG-011 — Ne pas coudre sous les objets qui recouvrent [P1] — ☐ À faire
- Hatch : Remove overlaps : la partie d'un remplissage cachée par un objet
  cousu au-dessus n'est pas cousue (moins de points, moins d'épaisseur).
- À faire : soustraction booléenne (Clipper2) des objets suivants avec une
  marge de recouvrement ; option par objet ; recalcul automatique.
- Modules : `libs/geometry`, `libs/stitch_generation`.
- R&D brevets : US6633794B2 documente la détection de points recouverts par des
  couches ultérieures ; garde-fou : ne jamais supprimer sous-couche ni trajet
  structurel. Règle d'analyse en lecture seule : RD-PAT-006
  (`docs/roadmap-rd-brevets.md`).

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
- État OpenStitch : réglage global (`SequenceFinishing`, Lots E/F) + satin propre
  (`lock_start/lock_end/lock_length/lock_passes`) ; rien par objet pour tatami, directionnel, running.
- À faire : surcharge par objet (tous types).

### HP-ENG-015 — Déplacements cousus le long des bords [P1] — ◐ Partiel
- État OpenStitch : underpath caché (tatami, satin, directionnel) ; le tatami longe le
  contour rentré quand le trajet intérieur est invalide (`tatami.cpp`) ; satin/directionnel non vérifiés en détail.
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
- État OpenStitch : le mode Contours (2026-10-05) apporte des métriques internes
  (`ContourMetrics`) et une QA adversariale, mais aucun corpus de référence ni comparaison à Hatch.
- À faire : jeu de référence (10–20 images variées : logo, dessin au trait,
  mascotte, texte, photo simple) numérisé dans Hatch et dans OpenStitch ;
  métriques (points, coupes, sauts, zones oubliées, débordements, satin vs
  tatami) ; boucle d'amélioration mesurée ; retirer l'étiquette
  « Expérimental » quand les métriques sont atteintes.
- Modules : `libs/autodigitize`, `tests/fixtures/`, `docs/source/auto-numerisation.md`.
- Dépend de : HP-PHYS-002 (même jeu).

### HP-AUTO-002 — Numérisation instantanée en un clic [P1] — ◐ Partiel
- État OpenStitch : « Numérisation automatique » existe mais demande une
  segmentation préalable et des réglages (le dialogue propose aussi le mode Contours
  avec curseur de détail, sans aperçu).
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

### HP-AUTO-004 — Vectorisation par courbes [P1] — ◐ Partiel (2026-10-12)
- État OpenStitch : la vectorisation produit des polygones (Douglas-Peucker),
  pas de courbes de Bézier ; les formes gardent des facettes et beaucoup de
  nœuds à éditer.
- À faire : ajustement de courbes cubiques (algorithme de Schneider /
  type Potrace — **Potrace est GPL : réimplémenter d'après la publication**,
  ne pas l'utiliser), détection des coins.
- Modules : `libs/vectorization`.
- Acceptation : nombre de nœuds divisé par ≥ 3 à erreur de contour égale.
- 2026-10-12 : `geometry::fit_curves` (Schneider + détection des coins, écart
  réel dans les deux sens) et `VectorizeOptions::curve_tolerance` /
  `digitize --curve-tolerance`. Critère ÷3 vérifié par test pour une tolérance
  ≥ 1,5 px (≈ ÷2 vers 1,2 px, bruit de trame). Reste : réglage dans le
  dialogue desktop, raffinement par contours actifs, et réduction des points
  courts du tatami sur formes rondes.

### HP-AUTO-005 — Détection des contours noirs (outlines) [P1] — ◐ Partiel (2026-10-06)
- État OpenStitch : le mode **Contours / Line Art** (fusionné 2026-10-05, PR #4) coud
  les traits d'un dessin comme objets running/satin à tracé ouvert, par couleur
  (`libs/autodigitize/contour_*`, `docs/source/auto-numerisation.md`). C'est une
  stratégie *alternative* : il n'existe toujours pas de mode qui détecte les
  contours noirs et les pose **par-dessus** des remplissages.
- À faire : les traits noirs fins d'un dessin (contours de coloriage)
  deviennent des objets contour/satin **par-dessus** les remplissages, au lieu
  de régions noires tatamisées.
- Modules : `libs/autodigitize`, `libs/auto_satin`.
- 2026-10-12 : extraction de traits depuis une **photo** par lignes cohérentes
  (FDoG, Kang/Lee/Chui 2007) — `image::CoherentLinesOp`, menu Image et
  `digitize --coherent-lines`. Alimente le mode Contours ; la pose par-dessus
  des remplissages reste à faire.
- **Livré le 2026-10-14 (en PR) : couche de traits sombres par-dessus les remplissages**
  (`digitize --dark-lines`, `autodigitize::auto_digitize_with_lines`). Les parties plus
  étroites que `--dark-lines-width` (1,6 mm) de l'ensemble des **pixels sombres de l'image**
  (L* <= `--dark-lines-lightness`, 25) sont lues sur l'image et non sur les régions quantifiées
  (où un trait fin anti-crénelé est découpé), retirées de la segmentation des remplissages
  (rendues aux régions voisines, puis les aplats que le trait coupait sont refusionnés : une seule
  pièce, sans trou) et cousues **en dernier** comme lignes médianes en point droit, parcours
  eulérien (mêmes analyse et garde-fous que le mode contours ; `--detail` règle le nettoyage).
  Mesure sur une illustration plate de navire (382 x 287 mm, 12 couleurs) : 55 000 px de traits
  séparés, 163 objets de ligne, 4,3 m de point droit ; mâts, vergues et haubans lisibles alors
  qu'ils fusionnaient avec les aplats sombres en un amas de blocs. **Coût** : +486 coupes
  (1 181 -> 1 675) car chaque trait est un tracé séparé. **Reste** : satin à largeur constante
  pour les traits d'environ 1 mm ou plus (point droit aujourd'hui), traits clairs (contours
  crème), réglage dans la boîte de dialogue d'auto-numérisation du bureau, couleur de trait
  unique (moyenne des pixels).

### HP-AUTO-006 — Suppression du fond [P1] — ◐ Partiel
- État OpenStitch : option « ignorer le fond » et **détection automatique du fond**
  (`segmentation::background_candidate`, case cochée selon L* et bords touchés, Lot A) ;
  transparence PNG respectée.
- À faire : choix « ne pas broder cette couleur » par couleur.

### HP-AUTO-007 — Fusion automatique des couleurs proches [P1] — ☐ À faire
- Voir HP-THR-011, déclenché à la fin de l'auto-numérisation.

### HP-AUTO-008 — Nettoyage des micro-détails [P2] — ◐ Partiel
- État OpenStitch : taille min de région, **absorption des petites régions et des
  lamelles** (`merge_small_regions`, `remove_thin_parts`, Lot D) ; le mode Contours
  a son propre nettoyage piloté par le curseur de détail (branches, boucles, éléments isolés).
- À faire : lissage des bords en escalier ; étendre le niveau de détail au mode « Formes ».

### HP-AUTO-009 — Choix du type satin / tatami fiable [P0] — ◐ Partiel
- État OpenStitch : moteur topologique par défaut, refus → tatami ; étiqueté
  expérimental. Le mode Contours a des garde-fous de largeur satin et un repli documenté,
  mais aucune métrique satin/tatami n'est enregistrée.
- À faire : fait partie de HP-AUTO-001 ; métrique spécifique : taux de bandes
  fines cousues en satin, zéro satin > largeur max.
- R&D brevets : critère de régularité par statistiques de la transformée de
  distance (RD-PAT-004) et réintégration protégée de l'auto-satin (RD-PAT-002),
  voir `docs/roadmap-rd-brevets.md`.
- Veille (`docs/veille-innovations-2026-10.md` §V2, article lu) : MSEmbGAN
  (TVCG) classe flat / satin / tatami par **érosion** du masque avec deux
  rayons (λ_f, λ_w), donc par seuils de largeur, sans apprentissage. Cela
  confirme l'approche géométrique ; il n'y a rien à apprendre de leur
  « classifieur » et leur jeu de données (dessiné sous Wilcom 9.0, licence non
  vérifiée) ne doit pas être embarqué. Contrainte brevet : voir HP-FAB-001
  (mesurer sur la géométrie, pas sur des points temporaires).

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
- État OpenStitch : import SVG et `auto_digitize_vectors` (classification AutoChoice sur
  objets vectoriels existants) présents dans `libs/autodigitize` ; manque l'action de bout en
  bout « SVG importé → broderie » côté utilisateur.
- Hatch : Convert graphics to embroidery : SVG/AI importé → objets de broderie
  avec types choisis automatiquement.
- À faire : après import SVG, appliquer la classification satin/tatami/contour
  aux formes vectorielles (sans passer par l'image).
- Modules : `libs/autodigitize`.

### HP-AUTO-013 — Outils d'analyse de la qualité d'une numérisation [P1] — ◐ Partiel (2026-10-16)
- État OpenStitch : **fait en CLI** (`libs/project_ops`, Qt-free, déterministe) : `project render`
  (rendu réaliste sur fond vert), `project fidelity` (tissu visible, détails perdus), `project
  density` (épaisseurs de fil, carte de chaleur), `project at` (quel objet a cousu un point),
  `project diagnose` (7 détecteurs avec objets et option conseillée), `project sweep` (balayage
  d'options, tableau objectif, variantes non dominées, planche de zooms), `digitize --debug-dir`
  (masques de décision), script `scripts/linux-core-setup.sh`, corpus d'invariants en test.
  Voir `docs/source/cli-project.md`, section *Outils d'analyse de la qualité*.
- Hatch : contrôle de densité et de chevauchement, simulateur de points, avertissements de
  conception ; pas d'équivalent de balayage automatique d'options.
- À faire : (1) exposer fidélité, densité (carte de chaleur superposable) et diagnostic dans le
  bureau, avec clic sur un défaut -> objet sélectionné (`project at`) ; (2) options de numérisation
  `--dark-lines-bars`, `--no-underpath`, `--fill-overlap`, `--dark-lines-objects-only` dans la boîte
  *Numérisation automatique* ; (3) sélecteur `--where` pour `project op` (changer un réglage sur tous
  les objets d'un type) ; (4) animation de l'ordre de couture et carte des angles de fil.
- Modules : `libs/project_ops` (fait), `apps/cli` (fait), `apps/desktop` (à faire).
- Dépend de : HP-AUTO-005 (couche de traits sombres).
- Acceptation : en CLI, chaque détecteur est validé sur un projet au défaut connu (tests
  `[analysis]`), l'image de stress `[invariants]` échoue si les poignées de Bézier ne sont plus bornées ;
  dans le bureau : un clic sur « tissu visible » centre la vue sur le plus gros trou.

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

### HP-VEC-002 — Tracé ouvert (ligne, polyligne, courbe ouverte) [P0] — ◐ Partiel (2026-10-13, en PR)
- Livré (second lot du 2026-10-13, en PR) : option **Tracé ouvert** (U) de la palette : le polygone devient une ligne brisée ouverte (2 points suffisent) et la courbe de Bézier une courbe ouverte (`geometry::Path::closed = false`). Un contour cousu dessus ne revient pas au départ (test `openPathOptionLeavesPolygonAndBezierOpen`).
- Reste : satin de bordure sur un chemin ouvert non vérifié dans l'interface (`border_satin.cpp` le prévoit) ; pas d'outil « ligne » distinct (c'est une option).
- État OpenStitch : `geometry::Path::closed` existe et le mode Contours produit des
  objets running à tracé ouvert ; mais **aucun outil de dessin** de ligne ouverte
  (`apps/desktop/tools.hpp`) : les outils manuels ferment toujours la forme.
- Hatch : outils de ligne ouverte (droite et courbe) pour contours et satins.
- À faire : outils « ligne » (clics, Entrée pour terminer **sans** fermer) et
  « courbe ouverte » ; `VectorObject` doit porter des chemins ouverts
  (vérifier `geometry::Path::closed`) ; point de contour et satin de bordure
  (HP-STI-004) acceptent un chemin ouvert.
- Modules : `libs/document`, `libs/geometry`, `apps/desktop`.

### HP-VEC-003 — Opérations booléennes entre formes [P1] — ◐ Partiel (2026-10-13, en PR)
- Constaté sur `main` (`eee6188`) : Forme ▸ Unir / Soustraire / Intersecter, une commande annulable, broderies de la forme retenue conservées.
- Livré (second lot du 2026-10-13, en PR) : **Exclure** (différence symétrique, Ctrl+Alt+X). Il ne reste que la fusion sur `main`.
- Hatch : Weld (souder), Trim, Intersect, Exclude.
- À faire : union / différence / intersection / exclusion sur la sélection via
  Clipper2 (déjà encapsulé : `geometry/boolean.hpp`) ; une commande annulable ;
  les objets de broderie liés sont régénérés.
- Modules : `libs/commands`, `apps/desktop`.
- Dépend de : HP-OBJ-001.

### HP-VEC-004 — Couteau : couper une forme en deux [P1] — ☑ Fait (2026-10-13, constaté sur `main`, `eee6188`)
- Livré : outil **Couteau** (K) sur toute forme vectorielle (`commands::make_split_command`, `geometry/cut.hpp`) ; chaque morceau garde son type de broderie (remplissages clonés). Vérifié par vrais gestes souris (`sidebarKnifeRealGesture`, PR #25).
- État OpenStitch : ligne de coupe **satin** uniquement (`DrawSatinCutLine`).
- À faire : couteau général sur toute forme vectorielle (`geometry/cut.hpp`
  existe), les deux morceaux gardent le type de broderie.

### HP-VEC-005 — Supprimer / remplir les trous [P1] — ◐ Partiel (2026-10-13, en PR)
- Livré (second lot du 2026-10-13, en PR) : Forme ▸ **Supprimer les trous** et **Combler les petits trous…** (seuil en mm²), `commands::make_remove_holes_command`, une commande.
- Reste : « supprimer CE trou » (clic sur un trou précis).
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

### HP-VEC-007 — Lisser / simplifier un tracé à la demande [P2] — ☑ Fait (2026-10-18, `89a3e2d`)
- À faire : commande « lisser » (réduction des nœuds + ajustement de courbes)
  sur un objet existant.
- Dépend de : HP-AUTO-004.
- Livré (2026-10-18) : `libs/geometry/smooth.*` : `smooth_path` / `smooth_path_set` (aplatit puis ajuste des courbes à une tolérance en µm, extrémités des tracés ouverts conservées, déterministe) ; 5 tests (`061b4f3`).
- Livré (câblage bureau, 2026-10-18) : menu Forme › « Lisser… » (tolérance en mm, 0,2 par défaut) sur la sélection, `SmoothVectorPathsCommand` (un seul pas d'annulation, refus si rien ne change) ; tests Catch2 et QTest.
- Reste : aucun.

### HP-VEC-008 — Convertir un polygone en courbes [P2] — ☑ Fait (2026-10-18, `89a3e2d`)
- Dépend de : HP-AUTO-004.
- Livré (2026-10-18) : `libs/geometry/to_curves.*` : `convert_to_curves` (tangentes colinéaires aux sommets peu anguleux, coins francs et extrémités inchangés, courbe passant exactement par les sommets) ; 4 tests (`ac1599a`).
- Livré (câblage bureau, 2026-10-18) : menu Forme › « Convertir en courbes » sur la sélection, `ConvertToCurvesCommand` (un pas d'annulation) ; tests Catch2 et QTest.
- Reste : aucun.

### HP-VEC-009 — Magnétisme (grille, nœuds, guides) [P1] — ◐ Partiel (2026-10-06)
- État OpenStitch : accrochage aux sommets, milieux de segment et centres de forme
  pendant le dessin, avec indicateur (`findSnapPointMm`) ; manquent grille, guides
  et l'activation par touche/bouton.
- À faire : accrochage à la grille, aux nœuds et aux guides, activable
  (touche + bouton), tolérance en pixels écran.
- Modules : `apps/desktop/canvas_view`.

### HP-VEC-010 — Outil de mesure [P1] — ☑ Fait (2026-10-18, `f38da87`)
- Hatch : mesurer une distance/un angle sur le canevas.
- À faire : outil règle : glisser → distance en mm, angle, affiché et copié.
- Livré (2026-10-18) : `libs/geometry/measure.*` : distance, longueur de chemin, angle, aire (µm, mm, mm²) ; 5 tests (`0bec0f8`).
- Livré (câblage bureau, 2026-10-18) : outil Règle (touche Q) : clic-glisser, règle temporaire (distance mm, Δx/Δy, angle), résultat dans la barre d'état et copiable, sans mutation du document ; gestes documentés ; `test_measure_tool`.
- Reste : accroche (snap) sur la règle.

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

### HP-VEC-014 — Formes prédéfinies supplémentaires [P2] — ☑ Fait (2026-10-18, `89a3e2d`)
- À faire : étoile, cœur, flèche, rectangle arrondi, arc, spirale — outils
  paramétriques (poignées) ou bibliothèque (HP-LIB-002).

### HP-VEC-015 — Combler les vides d'un bloc uni (par couverture réelle) [P1] — ◐ Partiel (2026-10-18, en PR)
- Besoin (retour utilisateur) : dans un bloc uni dont les zones ne se distinguent que par la couleur ou le type de point, changer un satin ou un tatami laisse des jours sans fil où l'on voit le tissu.
- Livré (2026-10-18) : `stitch_analysis::find_uncovered_pockets` (`libs/stitch_analysis/uncovered_pockets.*`) : les vides sont mesurés depuis les POINTS cousus de la séquence effective (masque de couverture fin, passes visibles seulement), pas depuis les contours vectoriels ; enveloppe du bloc fermée sur un écart réglable, ouverture morphologique (largeur minimale 0,3 mm), aire minimale/maximale. `commands::make_fill_pockets_command` rattache chaque poche au plus grand remplissage voisin (agrandit son vecteur s'il est seul et sans retouche, sinon crée un objet vectoriel + tatami de mêmes réglages et couleur) en UNE commande annulable. Emprise partagée `document::embroidery_footprint` (rails du satin inclus).
- Livré (CLI) : `project apply` op `fill_block_holes` (`objects`, `closing_gap_mm`, `max_area_mm2`).
- Livré (câblage bureau) : Forme › « Combler les vides du bloc… » (dialogue écart et aire maximale, sélection du bloc, annulation en un pas) ; test QTest `fillBlockHolesFillsTheGapBetweenTwoZones`.
- Reste : retouche manuelle d'une poche avant application (aperçu, exclusion) ; pas de mesure sur corpus des seuils (largeur minimale, écart de fermeture : valeurs d'ingénierie) ; un liseré de tatami laissé par la génération elle-même (ex. première rangée raccourcie d'un rectangle par défaut) est lui aussi détecté et comblé par un petit objet : à confirmer côté moteur.
- Dépend de : HP-VIEW-005 (carte de densité, même séquence effective).

---

## 9. Manipulation d'objets (OBJ)

État au 2026-10-13 : sélection multiple (clic modifié, rectangle, liste), translation et
redimensionnement à la souris, alignement, dupliquer / décaler, supprimer, ordre de couture.
Sur `main` depuis le 2026-10-13 : copier/coller, rotation, miroir, transformation du design entier. Manquent encore les
poignées de rotation sur le canevas, les groupes et la barre X/Y/L/H. C'est le **deuxième mur** après les formats :
tout utilisateur de Hatch fait Ctrl+C/Ctrl+V, sélection rectangle, rotation.
- Livré (2026-10-18) : `libs/geometry/shapes.*` : étoile, cœur, flèche, rectangle arrondi, arc, spirale (cadre exact, aire, symétrie, cas dégénérés) ; 10 tests (`d3b1575`).
- Livré (câblage bureau, 2026-10-18) : menu Forme › « Formes prédéfinies » (étoile à branches réglables, cœur, flèche, rectangle arrondi, arc, spirale), taille demandée par un dialogue paramétrique, insertion annulable au centre de la vue ; `test_vector_tools`.
- Reste : poignées sur le canevas et entrée dans la palette d'outils ; intégration à la bibliothèque HP-LIB-002.

### HP-OBJ-001 — Sélection multiple [P0] — ◐ Partiel (2026-10-13)
- Constaté sur `main` (`1ab521a` et suivants) : Ctrl/Maj+clic, rectangle de sélection (fenêtre ou croisement), sélection multi-lignes depuis la liste Document, inspecteur de groupe ; tests `rectangleLeftToRightIsWindow…`, `shiftRectangleAddsToTheSelection`, `documentListMultiSelectionDrivesRegionSelection`. Les critères d'acceptation sont remplis.
- Livré (PR #27, fusionnée le 2026-10-13 (`296ff34`)) : **Sélectionner tous les objets** (Ctrl+Maj+A ; Ctrl+A reste aux régions) et **Sélectionner les objets de même couleur** (`main_window_objects.cpp`).
- Reste : boîte englobante commune avec poignées (avec HP-OBJ-005) ; modèle de sélection encore dans `MainWindow` (pas de classe `selection_model`).
- À faire : Ctrl/Maj+clic, rectangle de sélection (glisser dans le vide),
  Ctrl+A, sélection depuis la liste Document (multi-lignes), sélection de
  tous les objets d'une couleur ; boîte englobante commune avec poignées ;
  inspecteur multi-objets (HP-OBJ-019). Modèle de sélection sorti de
  `MainWindow` vers une classe dédiée testable.
- Modules : `apps/desktop` (nouvelle `selection_model`), tests QTest.
- Acceptation : QTest : sélection rectangle, ajout/retrait Ctrl+clic,
  synchronisation liste ↔ canevas.

### HP-OBJ-002 — Couper / copier / coller [P0] — ◐ Partiel (2026-10-13)
- Livré (PR #27, fusionnée le 2026-10-13 (`296ff34`)) : Ctrl+X / Ctrl+C / Ctrl+V / Ctrl+Maj+V (coller sur place). Fragment `document::ObjectFragment` sérialisé au schéma du `.osp` (`project_io::fragment_to_json` / `fragment_from_json`, MIME `application/x-openstitch-objects+json`) ; `commands::collect_fragment` et `make_paste_command` (nouveaux identifiants, broderies réalignées, une commande annulable par collage, décalage cumulé de 5 mm). Tests : `test_vector_ops.cpp` (paste), `test_roundtrip.cpp` (fragment), `copyPasteKeepsEmbroideryAndIsOneUndoStep`, `cutThenPasteInPlaceRestoresTheShape`.
- Reste : collage entre deux instances non essayé à la main (testé dans un seul processus) ; un collage décalé retire les retouches manuelles de points ; les lettres d'un texte collées deviennent des formes ordinaires.
- À faire : Ctrl+X/C/V sur objets vectoriels **et** leurs objets de broderie
  (paramètres, fil, overrides) ; nouveaux identifiants à la collation ; collage
  décalé ou à la même position (Ctrl+Maj+V) ; entre deux projets / deux
  instances (presse-papiers système, MIME dédié contenant du JSON `.osp`) ;
  une seule commande annulable par collage.
- Modules : `libs/project_io` (sérialisation d'un sous-ensemble),
  `libs/commands`, `apps/desktop`.
- Dépend de : HP-OBJ-001.

### HP-OBJ-003 — Déplacement au clavier [P0] — ☑ Fait (2026-10-13, constaté sur `main`, `6a13ede`)
- Livré : flèches = 0,1 mm, Maj = 1 mm, sélection multiple déplacée d'un bloc, rafale fusionnée en un seul pas d'annulation (`TranslateVectorObjectCommand::mergeWith`, `CompositeCommand::setMergeKey`). Tests `arrowKeyNudgesSelectedObject…`, `nudgeMovesWholeMultiSelectionInOneUndoStep`. Le pas de grille n'existe pas (pas de grille magnétique dans l'application).
- État OpenStitch : flèches = 0,1 mm, Maj+flèches = 1 mm (`canvas_view.cpp`), testé ;
  manquent la fusion en un seul undo par rafale, le pas de grille et le multi-objet.
- À faire : flèches = 0,1 mm (ou pas de grille), Maj+flèches = 1 mm ; une
  commande fusionnée par rafale (undo en un coup).
- Modules : `apps/desktop`, `libs/commands` (fusion de commandes consécutives).

### HP-OBJ-004 — Rotation libre d'objets [P0] — ◐ Partiel (2026-10-13, en PR)
- Livré (second lot du 2026-10-13, en PR) : **poignée de rotation sur le canevas** (au-dessus de la forme sélectionnée, glisser = pivoter autour du centre, Maj = pas de 15°, une commande au relâchement). Reste révisé : pivot déplaçable, poignée pour une sélection multiple, aperçu pendant le glisser.
- Livré (PR #27, fusionnée le 2026-10-13 (`296ff34`)) : `commands::make_transform_command` + `AffineTransform::rotation` (`libs/commands/transform_ops.*`) : contours, angle du tatami, guides et ruptures directionnels, dégradé, rails/barreaux/entrée-sortie des satins, guides d'auto-satin ; annulation exacte par instantané. Menu Édition ▸ Transformer : pivoter de 90° à gauche/droite (Ctrl+[ / Ctrl+]), pivoter d'un angle. Acceptation vérifiée par test : tatami 30° tourné de 45° → 75° ; 360° et quatre quarts de tour = géométrie identique.
- Reste : poignées de rotation sur le canevas, Maj = pas de 15°, pivot déplaçable ; les retouches manuelles de points ne tournent pas.
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

### HP-OBJ-006 — Miroir horizontal / vertical [P0] — ◐ Partiel (2026-10-13)
- Livré (PR #27, fusionnée le 2026-10-13 (`296ff34`)) : miroir horizontal / vertical de la sélection autour de son centre (`AffineTransform::mirror`, même commande que la rotation), paramètres dépendants retournés (angles, guides, rails). Test `transform: mirror flips shape, row angle, guides and satin rails`.
- Reste : les rails A/B d'un satin ne sont pas échangés (le sens de couture de la colonne est donc inversé).
- À faire : `MirrorObjectsCommand` sur la sélection (axe = centre de la
  sélection), paramètres dépendants miroités (angles, rails échangés pour
  garder l'orientation, guides).
- Modules : `libs/geometry`, `libs/commands`, `apps/desktop`.

### HP-OBJ-007 — Inclinaison (skew) [P2] — ☐ À faire

### HP-OBJ-008 — Aligner et répartir [P1] — ◐ Partiel (2026-10-13)
- Constaté sur `main` (`1ab521a`) : Édition ▸ Aligner la sélection (gauche, centre, droite, haut, milieu, bas) sur la boîte de la sélection.
- Reste : alignement sur le premier objet ou sur le cadre ; répartir horizontalement/verticalement.
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

### HP-OBJ-012 — Dupliquer les objets de broderie [P1] — ◐ Partiel (2026-10-13)
- Livré (PR #27, fusionnée le 2026-10-13 (`296ff34`)) : **Dupliquer avec la broderie** (Ctrl+D) sur la sélection multiple : vecteur + broderie + fil, décalage de 2 mm, un pas d'annulation.
- Reste : décalage paramétrable, duplication répétée au même décalage.
- État OpenStitch : « Dupliquer » sur un vecteur.
- À faire : Ctrl+D sur la sélection (vecteur + broderie + fil), décalage
  paramétrable ; « dupliquer avec décalage répété » (le deuxième Ctrl+D
  reprend le même décalage).

### HP-OBJ-013 — Réseaux et dispositions (array, couronne, kaléidoscope, miroir-fusion) [P2] — ☐ À faire
- Hatch : Create layouts : réseau linéaire/circulaire, couronne (wreath),
  kaléidoscope, miroir-fusion.
- À faire : générateurs de copies transformées en une commande ; option
  « liées » (modifier l'original met à jour les copies) plus tard.

### HP-OBJ-014 — Transformer le design entier [P0] — ☑ Fait (2026-10-13, `296ff34`)
- Livré (PR #27, fusionnée le 2026-10-13 (`296ff34`)) : Édition ▸ Design entier : centrer dans le cadre, ajuster à la zone brodable (échelle + centrage), redimensionner en %, pivoter de 90°, miroirs. Les points sont régénérés. Test `wholeDesignCanBeCenteredFittedAndMirrored`.
- Reste : un design importé (points sans objets) n'est pas transformé.
- À faire : « centrer dans le cadre », « redimensionner le design » (toute la
  broderie, recalcul des points grâce à la régénération), rotation 90°/miroir
  du design complet — indispensable pour faire tenir un design dans le cadre
  de sa machine.
- Dépend de : HP-OBJ-004, HP-OBJ-005, HP-OBJ-006.

### HP-OBJ-015 — Barre de propriétés X / Y / L / H / angle [P1] — ☐ À faire
- Hatch : champs numériques de position/taille/rotation de la sélection.
- À faire : champs en mm dans la barre contextuelle ou l'inspecteur, cadenas
  de proportions ; chaque validation = une commande.

### HP-OBJ-016 — Suppression multiple [P0] — ◐ Partiel (2026-10-13)
- Constaté sur `main` : Suppr supprime la sélection d'objets, multiple comprise, en une commande (`MainWindow::deleteSelection`).
- Reste : même touche pour les points en mode édition (non vérifié).
- État OpenStitch : supprimer un objet, supprimer la broderie en gardant la
  forme ; Suppr = supprimer la **région** sélectionnée (conflit de sens).
- À faire : Suppr supprime la sélection courante quelle qu'elle soit (objets,
  régions, points en mode édition), une seule commande.
- Dépend de : HP-OBJ-001.

### HP-OBJ-017 — Renommer les objets [P2] — ☐ À faire
- À faire : F2 / double-clic dans la liste.

### HP-OBJ-018 — Changer la couleur d'un objet de broderie [P0] — ◐ Partiel (2026-10-13, en PR)
- Livré (second lot du 2026-10-13, en PR) : **pastille de couleur dans l'inspecteur** (`button_embroideryColor`), qui ouvre le sélecteur de couleur libre pour toute la sélection et suit les changements (annulation comprise). Le nuancier reste dans le panneau Fils. Il ne reste que la fusion sur `main`.
- Livré (PR #27, fusionnée le 2026-10-13 (`296ff34`)) : Édition ▸ **Couleur de la broderie…** (`QColorDialog`, RGB libre, sélection multiple, un pas d'annulation, fil de nuancier retiré).
- Reste (révisé) : la pastille dans l'inspecteur.
- Livré : la version « fil » (clic sur un fil du panneau Fils = assigner à la sélection
  multiple, un pas d'annulation) et `SetObjectThreadCommand` avec fil vide = couleur
  libre.
- Reste : la pastille et le sélecteur **dans l'inspecteur** (nuancier + RGB libre
  via `QColorDialog`) ; aucune entrée d'interface n'appelle encore la commande avec un
  RGB libre.
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

### HP-SEQ-001 — Amélioration 2-opt / Or-opt [P1] — ◐ Partiel (2026-10-06)
- État OpenStitch : 2-opt existe pour le routage *interne* d'un objet
  (`RoutingConfig::two_opt`, `routing.cpp`) ; il manque pour l'ordre *entre*
  objets (`libs/optimization/src/order.cpp`), seul concerné par `limitations.md`.
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
- Hatch / Wilcom (aide *Apply automatic branching*, lue le 2026-10-14) : Branching regroupe les
  objets sélectionnés en **un seul objet**, les **resequence**, minimise les connecteurs et
  **régénère les points**, avec un point d'entrée et un de sortie pour le groupe ; chaque objet
  doit toucher ou chevaucher un autre du groupe ; pour des lignes de point droit, il fusionne
  les doubles passes au milieu, là où Closest Join ne fait qu'échanger les bouts. Cela réordonne
  et régénère : hors du périmètre « passe finale » de HP-SEQ-006. **Décision du mainteneur
  (2026-10-14)** : pas de réordonnancement des régions ; le « branching » voulu est la liaison
  cachée entre deux régions (HP-SEQ-006.a et .d), appliquée à la fin. Reste ouvert ici : fusionner
  les doubles passes de lignes de point droit qui se touchent, à l'intérieur d'un objet.
- Piste de comparaison (veille §V5, principe lu) : Li & Mould (2014) montrent
  qu'un arbre a un contour extérieur formant un seul trajet fermé sans
  auto-intersection, obtenu en longeant l'arbre des deux côtés. Le squelette
  d'une forme fine est déjà un arbre ; comparer à `route_columns` avant d'en
  tirer quoi que ce soit (RD-PAT-008).
- Modules : `libs/stitch_generation` (routage), `libs/optimization`.

### HP-SEQ-005 — Connecteurs réglables par objet [P2] — ◐ Partiel (2026-10-14, en PR)
- Hatch (aide en ligne, lue le 2026-10-14) : propriétés d'objet ▸ onglet Stitching, trois
  réglages de coupe — « Si le connecteur suivant ≥ X mm » (3 mm par défaut), « Toujours »
  (outil *Always Tie-Off & Trim*), « Désactivé » (pas de coupe : un connecteur cousu caché
  sous un autre objet est plus efficace qu'une coupe).
- Livré (cœur et inspecteur, en PR) : `EmbroideryObject::connector` (`ConnectorMode` :
  `Inherit`, `AlwaysTrim`, `NeverTrim`), connecteur APRÈS l'objet vers le suivant de même fil.
  `AlwaysTrim` force point d'arrêt + coupe et n'est jamais remplacé par un pont caché ;
  `NeverTrim` supprime coupe et points d'arrêt (le fil reste tendu en saut). Sans effet avant un
  changement de fil, un arrêt ou une coupe posée à la main. Persistance `.osp` (clé
  `connector`, absente = hérite), `SetEmbroideryConnectorModeCommand` annulable, menu
  « Connecteur suivant » de l'inspecteur, compteurs `forced_trims` / `forced_no_trim` du bilan.
- **Reste** : seuil de coupe propre à un objet (le « ≥ X mm » de Hatch), mode « saut + coupe »
  vs « cousu » explicite, application à une sélection multiple (HP-OBJ-019), validation machine.
- Modules : `libs/document`, `libs/stitch_generation/finish.cpp`, `libs/commands`,
  `libs/project_io`, `apps/desktop/properties_panel`.

### HP-SEQ-006 — Minimiser les coupes [P1] — ◐ Partiel (2026-10-13, en PR)
- À faire : préférer un déplacement cousu caché sous un objet cousu ensuite
  (même couleur ou couverture garantie) plutôt qu'une coupe ; métrique :
  nombre de coupes sur le corpus.
- Découpé le 2026-10-13 en sous-entrées .a, .b, .c ci-dessous ; conception et
  sources dans `docs/veille-reduction-coupes-2026-10.md`. Contrainte posée par le
  mainteneur : l'optimisation s'applique **à la toute fin** du flux (dans
  `finish_sequence`, dernière passe d'`effective_sequence`), sans réordonner les
  objets ni régénérer quoi que ce soit en amont.

- Livré 2026-10-18 (audit `Possible.osp`, branche `fix/tatami-liaisons-bord`) : les liaisons
  entre rangées voisines du tatami tolèrent 0,15 mm de sortie au bord d'une fente ou d'un coin
  rentrant (`connector_invalid`, `kLinkEdgeTol`) au lieu de devenir un saut : 973 des 1 459 sauts
  de tatami du projet étaient de ce type. Mesure : sauts 2 862 → 1 525, coupes 928 → 792 ; soleil
  141 → 4 sauts. **Reste** (manques constatés) : parcours des rangées glouton sans retour par le
  fil déjà cousu (au-delà de `underpathCap`, 16 mm, on coupe) ; moteur directionnel : même test
  strict à assouplir ; satins auto (741 sauts) à analyser ; entrée de chaque tatami non chaînée à
  la sortie du précédent (voir HP-ENG-010) ; `hidden_travel` désactivé par défaut (928 → 531
  coupes avec les réglages les plus larges sur ce projet). Détail : `docs/source/tatami.md`.

### HP-SEQ-006.a — Pont caché entre objets en passe finale [P1] — ◐ Partiel (2026-10-13, en PR)
- Livré (cœur, en PR, non fusionné) : `HiddenTravel` dans `SequenceFinishing`
  (désactivé par défaut, persisté dans le `.osp`), `hidden_travel.{hpp,cpp}`
  (grille « dernier recouvrement », érosion, saut recouvert, A* déterministe)
  branché dans `finish_sequence` ; 13 cas de test (zone en L, couverture lâche,
  changement de couleur, coupe posée, recouvrement antérieur qui ne cache rien,
  déterminisme). **Export à la demande (2026-10-13)** : `project_io::ExportOptimizations`
  / `export_sequence` ; case « Réduire les coupes » du dialogue d'export du bureau (tous
  formats, résumé recalculé sur la séquence réellement écrite, projet intact) ; CLI
  `osp2dst --reduce-trims`. Mesure réelle (`docs/source/moteur-de-points.md`) : fixture
  `tentabrode`, 6 coupes sur 442 au réglage par défaut, car les régions segmentées
  sont juxtaposées sans recouvrement. **Reste** : réglage persistant du projet dans les
  « Options de génération » du bureau (l'export à la demande est fait), corpus « marine »,
  validation machine HP-PHYS-001, relecture juridique des brevets.
- État OpenStitch : `finish_sequence` (`libs/stitch_generation/src/finish.cpp`)
  coupe tout déplacement réel entre deux objets (`objectGap`) et tout
  déplacement de plus de `trim_threshold` (3 mm), avec verrous de sortie et
  d'entrée autour de chaque coupe. Les trajets cachés n'existent qu'à
  l'intérieur d'un objet (HP-ENG-015). La passe `StitchPass::Travel` existe.
- Hatch / Wilcom : Branching et course de liaison « imbriquée » dont l'objet
  suivant recouvre le trajet ; documentation et blogs publics, sources dans la
  note de veille.
- À faire : dans `finish_sequence`, pour chaque écart de même fil qui produirait
  une coupe, chercher un trajet entièrement recouvert par des points cousus
  **plus tard** ; preuve par une grille d'entiers « indice du dernier segment
  `TopStitch` qui couvre la cellule », masque érodé de la demi-largeur du fil
  plus une marge ; deux modes : saut caché sans coupe (écart court, aucun point
  ajouté) et pont cousu (A* déterministe, points ≤ `bridge_stitch_length`,
  passe `Travel`). Sans coupe, sans verrous à cet endroit. Jamais de pont à
  travers un changement de couleur, un Stop ou une coupe retouchée. Réglages
  dans `SequenceFinishing` (`.osp`), **désactivés par défaut**.
- Modules : `libs/stitch_generation` (passe), `libs/document` (réglages),
  `libs/project_io` (schéma `.osp`), `apps/desktop` (inspecteur) ; cœur sans Qt.
- Dépend de : HP-ENG-010 (qualité de l'ordre amont, gain dépendant de lui).
- Acceptation : scènes synthétiques `engine-debug` (chevauchement accepté,
  formes séparées refusées, remplissage lâche refusé, changement de couleur
  conservé, satin manuel sans surface) ; tout point d'un pont est dans le
  masque érodé ; passe désactivée = séquence strictement identique ; idempotence ;
  deux exécutions et `OPENSTITCH_THREADS=1` donnent le même DST ; nombre de
  coupes (`compute_stats().trims`), de points de verrou supprimés et de longueur
  ajoutée rapportés avant/après ; corpus réel (« marine », `tentabrode`, absents
  du dépôt) et validation physique HP-PHYS-001. Brevets : revue technique faite le 2026-10-14 (revendications
  de US8851001B2 et US8271123B2 lues, aucun élément repris par la passe ; voir
  `docs/revue-brevets-optimisation-trajets-2026-10.md`), relecture par un conseil toujours à faire.

### HP-SEQ-006.b — Déplacement sur surface déjà cousue de même couleur [P2] — ◐ Partiel (2026-10-13, en PR)
- Livré (cœur, en PR) : `HiddenTravel::allow_same_color_surface`, surface reconstruite
  par avancement séquentiel et remise à zéro à chaque changement de fil ; CLI
  `--hidden-travel-same-colour`. Mesure : sur `tentabrode`, 20 coupes sur 442 à
  40 mm de plafond, pour 371 mm de fil ajouté. **Reste** : réglage par objet, inspecteur,
  test dédié « jamais à travers une autre couleur non recouverte ».
- À faire : option de HP-SEQ-006.a autorisant le trajet sur une surface déjà
  cousue **de la même couleur** (équivalent du « travel on edges » de Hatch) quand
  aucun recouvrement ultérieur n'existe. Moins sûr (relief visible sur un satin),
  donc désactivé par défaut et réglable par objet.
- Dépend de : HP-SEQ-006.a.
- Acceptation : comme .a, plus un test montrant qu'un trajet ne traverse jamais
  une surface d'une autre couleur non recouverte.

### HP-SEQ-006.d — « Optimiser les trajets » : un bouton, trois niveaux, tout à la fin [P1] — ◐ Partiel (2026-10-14, en PR)
- Demande du mainteneur (2026-10-14) : l'utilisateur travaille sans penser aux déplacements ;
  **avant l'export** il demande « Optimiser les trajets ». Rien n'est réordonné au niveau des
  régions ; trois niveaux, tous appliqués à la toute fin : (1) **entre deux régions**, un pont
  caché si le trajet est recouvert plus tard (branching au sens « liaison cachée ») ; (2) **dans
  un objet**, les liens sous-couche ↔ remplissage ; (3) **au niveau atomique de la sous-couche**.
  Impératif : sur le remplissage, **rien ne doit se voir**.
- Livré (cœur, CLI, dialogue d'export, en PR) : `HiddenTravel::{between_objects, within_object,
  reorder_underlay}` (trois niveaux indépendants, les trois actifs par défaut une fois
  l'optimisation demandée) ; `reorder_underlay_passes` (`underlay_order.{hpp,cpp}`) : ordre et
  sens des passes de sous-couche d'un même objet, coût = longueur + pénalité de coupe, exact
  jusqu'à 6 passes (essai de toutes les permutations et sens), plus proche voisin au-delà, appliqué
  seulement si le coût baisse strictement ; dialogue d'export : case « Optimiser les trajets » et
  trois cases de niveau ; CLI `--reduce-trims-levels` / `--hidden-travel-levels`.
- **Garantie testée** : la couche supérieure (tous les points `TopStitch`, dans l'ordre) est
  strictement identique avec ou sans optimisation, sur une scène construite et sur un projet
  généré ; les points de sous-couche sont les mêmes, seulement enchaînés autrement ; les passes
  séparées par une coupe posée, un changement de fil ou un arrêt ne sont pas regroupées.
- **Mesure** (fixture `tentabrode`, 442 coupes) : 426 avec les trois niveaux (−16), dont 23 groupes
  de sous-couche réordonnés (1,84 m de saut en moins) et 12 ponts, tous dans un objet. Peu de
  coupes disparaissent parce que la plupart des écarts restants relient deux ÎLOTS de
  remplissage séparés par du tissu nu (haut du remplissage → bas du suivant) : aucun point
  cousu plus tard ne peut les cacher et réordonner le remplissage se verrait.
- **Reste** : ordre des îlots d'un même objet (invisible quand ils sont disjoints), lien
  sous-couche → remplissage choisi au point d'entrée le plus proche, passage en tâche de fond
  du calcul du résumé, validation machine HP-PHYS-001.
- Modules : `libs/stitch_generation` (`finish.cpp`, `hidden_travel`, `underlay_order`),
  `libs/document` (`finishing.hpp`), `libs/project_io`, `apps/desktop/machine_export_dialog`,
  `apps/cli`.
- Dépend de : HP-SEQ-006.a.
- Acceptation : couche supérieure identique (test), déterminisme, désactivé = séquence
  identique, `--reduce-trims-levels` limite chaque niveau, mesure avant/après rapportée.

### HP-SEQ-006.e — Mode avancé : réordonner les régions d'une même couleur [P2] — ◐ Partiel (2026-10-14, en PR)
- Demande du mainteneur (2026-10-14) : une option « plus efficace mais plus invasive » qui peut
  réarranger les régions à l'intérieur d'une même couleur (on suppose les régions déjà triées par
  couleur, ce qui est le cas courant).
- Livré (cœur, CLI, dialogue d'export, en PR) : `HiddenTravel::reorder_regions` (désactivé par
  défaut, persisté) ; `reorder_regions_same_color` (`region_order.{hpp,cpp}`). Garde-fous : jamais
  d'échange entre couleurs (changement de fil, arrêt, fin = barrières) ; **l'ordre relatif de deux
  régions dont les emprises se touchent ou se recouvrent est conservé** (emprise = points élargis
  de la demi-largeur de fil plus la marge, grille de 0,25 mm) ; chaque région reste intacte (mêmes
  points, même sens, jamais retournée) ; régions verrouillées, connecteur choisi à la main ou sans
  objet source = épinglées, elles bornent les groupes. Ordre : plus proche voisin sous contrainte
  d'ordre, puis déplacements d'une région tant que le coût baisse ; groupe modifié seulement si le
  coût baisse strictement. CLI `--reduce-trims-levels regions` (jamais inclus dans `all`).
- **Mesure** (fixture `tentabrode`, 442 coupes) : 149 régions déplacées, 1,87 m de saut en moins,
  mais **seulement 1 coupe en moins de plus** : chaque frontière entre deux objets reste coupée
  (règle `objectGap`, correctif du 2026-10-08), l'ordre ne la supprime pas ; le gain est en
  longueur de déplacement et en temps, et rend les ponts cachés plus probables.
- **Reste** : prise en compte des ponts possibles dans le coût (aujourd'hui : distance seule),
  garde-fou sur le tirage du tissu (l'ordre de couture influe sur la déformation), validation
  machine HP-PHYS-001.
- Modules : `libs/stitch_generation` (`region_order`, `finish.cpp`), `libs/document`,
  `libs/project_io`, `apps/desktop/machine_export_dialog`, `apps/cli`.
- Dépend de : HP-SEQ-006.d.
- Acceptation : ordre relatif des régions qui se touchent conservé, régions verrouillées en
  place, changements de fil intacts, chaque région identique (tests), désactivé = séquence
  identique, déterminisme.

### HP-SEQ-006.c — Rapport de réduction des coupes [P1] — ◐ Partiel (2026-10-13, en PR)
- Livré (cœur, en PR) : `TrimReport` et `effective_sequence_report` (même chemin de
  calcul qu'`effective_sequence`, vérifié par test) ; CLI `digitize --hidden-travel`
  affiche le bilan avec les raisons de refus. **Reste** : `engine-debug` et le panneau
  d'analyse du desktop, estimation de durée (HP-PROD-002).
- À faire : rapport lisible (CLI `engine-debug` et panneau d'analyse) : coupes
  avant/après, ponts acceptés et refusés **avec la raison** (changement de
  couleur, pas de recouvrement, trop long, coupe retouchée), points de verrou
  économisés, fil ajouté, estimation de durée (HP-PROD-002). Sert de mesure
  d'acceptation de .a.
- Modules : `libs/stitch_generation`, `apps/cli`, `libs/stitch_analysis`.
- Dépend de : HP-SEQ-006.a.
- Acceptation : les raisons de refus sont énumérées et testées ; le rapport est
  déterministe.

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

### HP-VIEW-001 — Rendu réaliste des points [P0] — ◐ Partiel (2026-10-13)
- Constaté sur `main` (`0a4c140` et suivants) : lib `stitch_render` (fils, ombrage, tissu) et vue réaliste du desktop (Affichage ▸ Rendu réaliste, Ctrl+Maj+R, réglages).
- Reste : vérifier le critère « 100 000 points en < 100 ms après modification » dans `bench_main_window.cpp` (non mesuré lors de cette revue).
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
- Hatch (aide en ligne *View connectors*, lue le 2026-10-14) : *Afficher le design* propose
  « Connecteurs » et « Fonctions » ; les fonctions s'affichent par des symboles — cercle pour
  l'attache d'entrée, triangle pour la coupe et l'attache de sortie. Utile ici pour vérifier
  les coupes avant l'export (HP-SEQ-006) : reprendre cet affichage, il rend visible ce que
  la passe de ponts cachés a supprimé ou conservé.

### HP-VIEW-004 — Fond tissu et mise en situation sur vêtement [P2] — ☐ À faire
- Hatch : afficher le design sur un produit (t-shirt, casquette, serviette).
- À faire : couleur de tissu (HP-THR-008), texture, images de produits avec
  zone brodable, cadre montré dessus.

### HP-VIEW-005 — Carte de densité [P1] — ◐ Partiel (2026-10-18, `c046aa4`)
- État OpenStitch : `limitations.md` : « pas de carte de densité ».
- À faire : cœur : grille de densité (pénétrations/mm², épaisseur cumulée)
  calculée par `stitch_analysis` ; UI : surimpression colorée (échelle
  séquentielle) + règle d'analyse « zone trop dense ».
- Modules : `libs/stitch_analysis`, `apps/desktop`.
- Livré (2026-10-18) : `libs/stitch_analysis/density_map.*` : grille de densité (pénétrations/mm², longueur de fil, épaisseur en couches) bornée par `max_cells`, zones sur-denses 4-connexes à seuil configurable avec objet dominant ; 7 tests (`1679296`). Seuil de 4,0 /mm² heuristique, non mesuré sur corpus.
- Livré (câblage bureau, 2026-10-18) : Affichage › « Carte de densité » (case mémorisée) : surimpression bleu→rouge avec légende pén./mm², calcul sur la séquence effective hors du fil UI et mis en cache ; zones sur-denses listées dans le panneau Analyse (clic = centrer, seuil réglable 4,0 par défaut) ; ne modifie jamais le document ; `test_density_map_ui` (12 cas).
- Reste : vérification visuelle dans l'application réelle (palette, légende, centrage au clic non vérifiés en headless) ; calibrage du seuil sur corpus.

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
- Piste (veille §V8, résumé de US8818773, « Expired - Fee Related » selon
  Google, échéance affichée incohérente : à vérifier) : rendre chaque point en
  prenant, dans une base d'images pré-rendues indexée par longueur et angle
  d'éclairage, l'image correspondante. À mesurer avec le banc avant de choisir ;
  à ne pas adopter sans gain démontré sur le rendu actuel.

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

### HP-PROD-001 — Fiche de production imprimable [P0] — ◐ Partiel
- Hatch : Production worksheet : aperçu du design, dimensions, nombre de
  points, temps estimé, cadre, **liste des fils dans l'ordre** (marque, code,
  nom, pastille), notes, client.
- Fait : `stitch_analysis::make_production_sheet` (données pures, séquence
  effective) ; JSON stable et HTML ; Fichier ▸ Fiche de production… (aperçu avant
  impression, impression `QPrinter`, export PDF A4 `QPdfWriter`) ; `openstitch-cli
  production` (texte / `--json` / HTML) ; aperçu réaliste, dimensions, cadre,
  compteurs, temps (hypothèse réglable, défaut 700 pts/min), blocs dans l'ordre
  de couture avec pastille + code hexadécimal, objets, avertissements, notes.
  Voir `docs/source/production-sheet.md`.
- Reste : la **référence de fil** (marque, code, nom) n'apparaît que lorsqu'un bloc
  porte un `thread_key` — les objets du document n'en portent pas encore
  (HP-THR-004) ; champ « client » dédié (aujourd'hui : dans les notes) ; PDF depuis
  le CLI (Qt-free : HTML seulement).
- Modules : `libs/stitch_analysis` (données), `apps/desktop`, `apps/cli`.
- Dépend de : HP-THR-004 (liste de fils), HP-PROD-002.

### HP-PROD-002 — Estimation de la durée de couture [P1] — ◐ Partiel
- À faire : points / vitesse machine (profil) + temps par changement de fil,
  par coupe, par saut ; affichée dans les statistiques, le résumé d'export,
  la fiche.
- Fait (fiche de production) : points / vitesse réglable + temps par changement de fil
  (30 s) et par coupe (2 s). Reste : profil machine (HP-HOOP-002), affichage dans les
  statistiques et le résumé d'export.
- Modules : `libs/stitch_analysis`.
- Dépend de : HP-HOOP-002 (vitesse).

### HP-PROD-003 — Gabarit de placement imprimé à l'échelle 1:1 [P1] — ☐ À faire
- Hatch : impression du design à taille réelle avec croix de centrage et axes.
- À faire : impression 1:1 vérifiée (règle imprimée de 10 cm), sur plusieurs
  pages si besoin.

### HP-PROD-004 — Aperçu avant impression général [P1] — ☐ À faire

### HP-PROD-005 — Consommation de fil par couleur et canette [P1] — ☑ Fait (2026-10-18, `6f8fe96`)
- État OpenStitch : longueur de fil totale.
- À faire : par couleur, estimation fil de canette, en mètres.
- Livré (2026-10-18) : `thread_consumption` : fil cousu / dessus / canette par couleur (m), nombre de canettes (capacité réglable, 100 m par défaut), coefficients réglables non mesurés ; 4 tests (`8385ada`).
- Livré (câblage bureau, 2026-10-18) : consommation de fil par couleur, totaux et canettes dans la fiche de production (section HTML, clé JSON additive `consumption`, capacité de canette réglable dans le dialogue) ; tests.
- Reste : affichage dans le panneau Fils et les statistiques ; calibrage des coefficients sur machine.

### HP-PROD-006 — Devis (prix par 1 000 points) [P3] — ☐ À faire

### HP-PROD-007 — Règles d'analyse supplémentaires [P1] — ◐ Partiel (2026-10-18, `c046aa4`)
- À faire : zone trop dense / recouvrements empilés (HP-VIEW-005), détails
  < 1 mm, texte trop petit (HP-TXT-009), satin trop large, objet sans point
  d'arrêt, fil trop proche de la couleur du tissu, dépassement de la zone
  cousable de la machine, nombre de couleurs > aiguilles de la machine.
- Modules : `libs/stitch_analysis`.
- Livré (2026-10-18) : règles `detail-trop-petit`, `objet-sans-arret`, `trop-de-coupes`, `densite-excessive` dans `stitch_analysis` (les deux dernières désactivées par défaut, seuils non mesurés) ; 6 tests (`ecb1e08`).
- Livré (câblage bureau, 2026-10-18) : les règles `detail-trop-petit` et `objet-sans-arret` s'affichent dans le panneau Analyse ; `trop-de-coupes` et `densite-excessive` s'activent avec leurs seuils (50 coupes, 4,0 pén./mm²) dans « Règles optionnelles » ; tests.
- Reste : satin trop large, fil trop proche de la couleur du tissu (HP-THR-008), nombre de couleurs supérieur aux aiguilles et zone cousable (HP-HOOP-002) ; seuils non mesurés sur corpus.

### HP-PROD-008 — Corrections automatiques depuis l'analyse [P2] — ◐ Partiel (2026-10-17)
- État OpenStitch : **fait** : `Finding::fix` (donnée pure, `libs/stitch_analysis`) et bouton
  « Corriger » du panneau Analyse, une commande annulable par clic : `hors-cadre` (ramener
  dans le cadre), `texte-trop-petit` (5 mm), `parametre-hors-limites` (ramener dans la plage),
  `point-long` (découper les points trop longs) et `saut-sans-coupe` (activer les finitions,
  seulement si leur simulation supprime le constat). Rapport unique `analyze_project` pour le
  panneau, le pré-export et `project quality`. Voir `docs/source/analysis-and-validation.md`.
- À faire : `saut-long` (optimiser l'ordre), `couches-superposees` (retirer le remplissage
  caché, voir RD-PAT-006), `trait-trop-fin` ; application des corrections en CLI
  (`project op`) ; instruire les 414 points sous 0,1 mm mesurés (probables doublons de la
  génération, cause non confirmée) ; mesurer sur de vrais motifs (le corpus du dépôt n'a ni
  texte ni réglage manuel). Fait le 2026-10-17 après mesure : `saut-long` ne signale plus un
  déplacement qui contient une coupe, `point-court` scindé (avertissement < 0,1 mm,
  information par objet de 0,1 à 0,5 mm) ; `vide` et `trop-de-points` conservés.
- Modules : `libs/stitch_analysis`, `apps/desktop`.
- Acceptation : un clic sur « Corriger » applique une seule commande annulable (undo
  rétablit l'état) et le constat disparaît à la ré-analyse (tests `test_analyze_project`,
  `test_analysis_fix`).

---

## 15. Cadres et machines (HOOP)

Aujourd'hui : un cadre **rectangulaire** de taille libre (10–500 mm), persisté.

### HP-HOOP-001 — Bibliothèque de cadres par marque [P1] — ◐ Partiel (2026-10-13)
- Livré (PR #25, fusionnée le 2026-10-13) : 8 cadres rectangulaires usuels (`document/hoop_presets.hpp`), cadres enregistrés par l'utilisateur, bouton Pivoter, marge de sécurité (`Canvas::margin`) et zone brodable affichée et utilisée par l'analyse.
- Reste : cadres par marque, formes non rectangulaires, gabarits.
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
- Veille (`docs/veille-innovations-2026-10.md` §V4, sources lues) : OriStitch
  (SCF '25) place les cadres par heuristique gloutonne de recouvrement
  d'ensemble : candidats = produit des x et y minimaux des boîtes englobantes,
  deux orientations par candidat, on retient le cadre qui contient entièrement
  le plus d'unités restantes, on retire, on recommence (O(N⁴)). **Limite** :
  une unité doit tenir entière dans un cadre ; un objet plus grand qu'un cadre
  fait échouer l'algorithme, donc il faut aussi découper les gros objets.
  StitchOver (UIST '26) ajoute des points de jonction (zigzag en verrou) entre
  deux cadrages pour garder la continuité d'un fil ; la documentation Wilcom
  décrit des repères cousus dans la zone de recouvrement, non enregistrés dans
  le design.
- À faire (précisions) : placement glouton **déterministe** (candidats triés
  explicitement) ; découpe des objets plus grands qu'un cadre ; zone de
  recouvrement réglable ; repères de recalage cousus à l'export, absents du
  projet ; un fichier machine par cadrage ; rapport des objets coupés.
- Modules : `libs/stitch_generation` (découpe), `libs/formats`, `apps/desktop`.
- Acceptation : deux exécutions donnent les mêmes cadrages ; l'union des
  cadrages couvre tous les points du design ; aucun point n'est perdu ni
  dupliqué hors recouvrement ; chaque fichier tient dans le cadre déclaré.

### HP-HOOP-005 — Rotation du cadre / design en paysage [P1] — ◐ Partiel (2026-10-13)
- Livré (PR #25, fusionnée le 2026-10-13 et PR #27, fusionnée le 2026-10-13 (`296ff34`)) : bouton Pivoter du dialogue du cadre (échange largeur/hauteur) ; « Pivoter le design de 90° ».

### HP-HOOP-006 — Centrer automatiquement dans le cadre [P1] — ◐ Partiel (2026-10-13)
- Livré (PR #27, fusionnée le 2026-10-13 (`296ff34`)) : « Centrer dans le cadre » et « Ajuster à la zone brodable » (voir HP-OBJ-014). Pas de centrage automatique à l'import.
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
- **Risque brevet à instruire avant tout code** (veille §V3) : US8851001B2
  (Melco, affiché actif jusqu'en 2032 par Google ; revendications 1 et 19 lues)
  couvre le choix du type de point, dont la sous-couche, en comparant à un jeu
  de valeurs la taille d'un élément mesurée sur des **points temporaires**, après
  agrandissement ou changement de tissu. Le profil de tissu ne doit donc **pas**
  changer le *type* de point en mesurant des points temporaires ; il ne règle que
  des facteurs (densité, compensation). Mesure de taille sur la géométrie
  (transformée de distance). Analyse non juridique : faire relire. Les
  revendications de US9702070B2 n'ont pas pu être lues.
- Modules : `libs/document`, `libs/stitch_generation`.
- Dépend de : HP-ENG-002, HP-ENG-001.a (table de compensation).

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
- R&D brevets : US5438520A et JP3769602B2 (Barudan) décrivent le flux
  « un contour → coupe, positionnement, bâti, satin » ; suivi RD-PAT-009
  (`docs/roadmap-rd-brevets.md`).

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

Aujourd'hui : ouvrir image/SVG, enregistrer `.osp` (chemin mémorisé depuis
HP-FILE-002 : Ctrl+S réécrit, Ctrl+Maj+S = Enregistrer sous), ouvrir `.osp`,
import/export DST et DXF, indicateur « modifié », garde partagée
(fermeture et Nouveau projet), fichiers récents (HP-FILE-003), sauvegarde automatique
et récupération après plantage (HP-FILE-004).

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
- Reste (hors entrée) : le chemin « Enregistrer » de la garde n'ouvre plus de
  sélecteur pour un document déjà rattaché à un fichier (HP-FILE-002) ; un document sans
  image ne régénère toujours pas de points (`refreshImage` sort tôt), limite
  indépendante de cette entrée.

### HP-FILE-002 — Enregistrer / Enregistrer sous [P0] — ☑ Fait (2026-09-23)
- État OpenStitch : « Enregistrer le projet… » ouvre toujours un dialogue.
- À faire : chemin courant mémorisé ; Ctrl+S enregistre directement,
  Ctrl+Maj+S = Enregistrer sous ; nom du fichier dans le titre de la fenêtre ;
  écriture atomique (fichier temporaire + renommage) pour ne jamais corrompre
  un `.osp` en cas de crash pendant l'écriture.
- Modules : `apps/desktop`, `libs/project_io` (écriture atomique).
- Livré : `MainWindow::currentProjectPath_` porte le fichier `.osp` du document.
  « Fichier ▸ Enregistrer le projet » (Ctrl+S, `action_saveProject`) réécrit ce
  fichier sans dialogue et n'ouvre le sélecteur que pour un document encore sans
  fichier ; « Enregistrer le projet sous… » (Ctrl+Maj+S, `action_saveProjectAs`)
  demande toujours un chemin, complète l'extension `.osp` si la plateforme ne
  l'a pas fait, puis l'adopte. L'ouverture d'un projet installe la cible
  (`openProjectFile`, extrait de `loadProject` — seam de test et futur point
  d'entrée de HP-FILE-003/005) ; `resetDocumentState()` l'efface, donc tout
  remplacement de document (Nouveau, image, SVG, projet, import DST) empêche un
  Ctrl+S d'écraser le projet précédent. Titre de fenêtre
  « <fichier>[*] — OpenStitch Studio » (« Sans titre » avant le premier
  enregistrement). Écriture atomique : déjà assurée par
  `project_io::save_project` (temporaire + renommage, testée par
  `tests/unit/project_io/test_roundtrip.cpp`) — rien à ajouter côté cœur.
  Tests : `saveActionsAreInFileMenuWithStandardShortcuts`,
  `savingAnOpenedProjectRewritesItWithoutAskingAPath`,
  `newProjectForgetsTheSaveTargetAndResetsTheTitle`
  (`tests/unit/desktop/test_main_window.cpp`).
- Reste (hors entrée) : pas de `.osp.bak` (HP-FILE-009).

### HP-FILE-003 — Fichiers récents [P0] — ☑ Fait (2026-10-05)
- À faire : sous-menu Fichier ▸ Récents (10), liste sur l'écran d'accueil
  (avec vignettes quand HP-FMT-020 existe), suppression des entrées disparues.
- Modules : `apps/desktop/recent_files.hpp/.cpp` (nouveau),
  `apps/desktop/empty_state_widget.*`, `apps/desktop/main_window.*`.
- Livré : `apps/desktop/recent_files.hpp/.cpp` (fonctions libres,
  `addRecentFile`/`pruneMissingRecentFiles`/`QSettings` clé `recent/files`,
  dédup/troncature à 10). `MainWindow::refreshRecentFilesUi()` resynchronise
  `recentFiles_` (load + purge + save) de façon synchrone dès l'appel (y
  compris le premier, au constructeur), seule la reconstruction des widgets
  (sous-menu Fichier ▸ Récents, liste de l'écran d'accueil) étant différée
  d'un cycle (évite de détruire, depuis son propre gestionnaire de clic, le
  `QAction`/`QPushButton` qui vient de déclencher l'appel). `openRecentFile()`
  partage la garde `confirmDiscardChanges` + ouverture entre le menu et
  l'écran d'accueil. Pas de vignettes (HP-FMT-020 n'existe pas encore).
  Tests : `addRecentFileDeduplicatesAndTruncatesToTen`,
  `recentFilesAndMenuReflectTwoSavesAndOpensInOrder`,
  `clickingRecentButtonForDeletedFileWarnsAndPrunesWithoutCrashing`
  (`tests/unit/desktop/test_main_window.cpp`).

### HP-FILE-004 — Sauvegarde automatique et récupération après plantage [P0] — ☑ Fait (2026-10-05)
- État OpenStitch : `limitations.md` : « pas d'autosave ».
- À faire : autosave périodique (hors thread UI si lourd) dans le dossier
  applicatif ; au démarrage suivant un plantage, proposer la récupération.
- Modules : `apps/desktop/autosave.hpp/.cpp` (nouveau), `apps/desktop/main_window.*`.
- Livré : `apps/desktop/autosave.hpp/.cpp` — fonctions libres + deux structs
  (`AutosaveSlot`, `AutosaveCandidate`), gabarit `recent_files.hpp` (pas de
  classe, pas d'état caché : le créneau est toujours recalculé depuis
  `currentProjectPath_` via `slotFor()`, pur). `MainWindow` : timer
  `autosaveTimer_` (120 s fixe, P0, ASM-S11-01) démarré dans le constructeur,
  déclenchant `onAutosaveTick()` — écrit, sur le thread UI via
  `project_io::save_project`, un instantané `.osp` + sidecar JSON sous
  `QStandardPaths::AppDataLocation/autosave/<slug>.*` (jamais le fichier
  utilisateur) dès que `isWindowModified()` et le document non vide ;
  `setCurrentProjectPath()` abandonne le créneau de l'ancienne identité à
  chaque changement (nouveau document, enregistrement, ouverture) ;
  `closeEvent()` arrête le timer et purge le créneau courant sur une
  fermeture réellement acceptée. Au démarrage (`checkAutosaveRecovery()`,
  différé après le premier passage de la boucle d'évènements qui suit
  `show()`), chaque créneau orphelin propose Récupérer/Ignorer
  (`QMessageBox`, boutons `action_autosaveRecover`/`action_autosaveIgnore`) ;
  « Récupérer » charge le contenu comme document **sans nom**
  (`currentProjectPath_` vide, `isWindowModified() == true`) — jamais
  réassocié au chemin d'origine ni au créneau autosave lui-même (invariant
  central) — et purge le créneau dans les deux cas (traité une fois pour
  toutes). Slug : SHA-1 hex du chemin canonique du fichier utilisateur, ou
  `untitled-<pid>` si jamais enregistré (AD-S11-1, decision du
  code-planner — n'importe quel hachage stable convenait). Tests :
  `autosaveTickWritesASeparateFileAndLeavesTheUserFileUntouched`,
  `autosaveTickSkipsWhenDocumentUnmodifiedOrEmpty`,
  `autosaveRecoveryAcceptLoadsAsUntitledDocumentAndPurgesSlot`,
  `autosaveRecoveryIgnoreDiscardsSlotWithoutLoading`,
  `cleanCloseDiscardsTheCurrentAutosaveSlot`
  (`tests/unit/desktop/test_main_window.cpp`).
- Reste (hors entrée) : intervalle fixe, non configurable (ASM-S11-01, P0) ;
  écriture synchrone sur le thread UI — pas de passage en tâche de fond
  (aucune infrastructure async n'existe encore, `CLAUDE.md#Architecture`) ;
  pas de vignette dans le dialogue de récupération (HP-FMT-020 hors
  périmètre) ; pas de `.osp.bak` versionné (HP-FILE-009, entrée distincte).

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
- Audit 2026-10-18 (BRO-09, BRO-10) : constaté aujourd'hui, 4 modes d'édition + 15 outils à
  activer par menu, pas d'entrée par double-clic, nom du mode actif absent de la barre d'état.
  **Réalisation par étapes** : d'abord Échap d'un niveau (HP-UX-018) et double-clic pour entrer
  dans l'édition (satin → `satinEditModeAct_`), puis indication permanente du mode ; la fusion
  complète en un mode « Remodeler » n'est engagée que si ces gestes ne suffisent pas.
  Acceptation ajoutée : double-clic sur un satin sélectionné active son mode d'édition, Échap le
  quitte sans changer la sélection.
- Modules : `apps/desktop`.

### HP-UX-002 — Création directe sans dialogue [P1] — ◐ Partiel (2026-10-18, branche `ux-lot-c1-pr`)
- À faire : « Créer un tatami » crée immédiatement avec les défauts (ou le
  dernier réglage) ; les paramètres se règlent ensuite dans l'inspecteur.
- Audit 2026-10-18 (BRO-19, PAR-03) : **décision du 2026-10-18 : les dialogues de création
  (`createTatamiObject`, contour, satin : `main_window.cpp:4161, 4221-4260`) sont supprimés** ;
  une entrée « Créer avec réglages… » reste dans le sous-menu pour qui veut saisir les valeurs
  avant création. Création immédiate au dernier réglage, sélection, une `ICommand`. Condition :
  HP-PERF-009 (l'inspecteur règle en direct sans geler l'interface). Voir HP-UX-028.
  Acceptation : `createTatamiAct_` ne laisse aucun `QDialog` ouvert et pousse un pas
  d'annulation.
- Livré : « Créer un contour / tatami / satin » créent tout de suite au dernier réglage mémorisé (`ui_memory::creation`, `QSettings`, aucun champ de document ni `.osp`), en une `ICommand` ; le sous-menu « Créer avec réglages » garde les dialogues, préremplis et mémorisés ; la question « satin impossible, utiliser un tatami ? » reste (refus honnête). Tests : `test_creation_flow` (aucune modale, un pas d'annulation, réutilisation du dernier réglage). Tests existants adaptés (`createSatinObject…`) : l'acceptation de dialogue est remplacée par la vérification qu'aucune modale ne s'ouvre.

### HP-UX-003 — Inspecteur homogène pour tous les types [P1] — ◐ Partiel
- À faire : mêmes sections pour tous (Général : nom, fil, visible ; Points ;
  Sous-couche ; Compensation ; Entrée/sortie ; Avancé), unités affichées,
  info-bulle sur chaque champ, bouton « réinitialiser au défaut » par champ,
  valeurs par défaut documentées.
- Audit 2026-10-18 (BRO-11, BRO-12, BRO-13, PAR-17) : constaté, tatami ≈ 13 champs dont la
  sous-couche toujours déroulée, directionnel ≈ 25, satin ≈ 20, aucun bouton « défaut » ni
  défauts mémorisés. **Précision** : en tête, les trois réglages d'usage (espacement, angle,
  compensation) ; sections « Sous-couche » et « Avancé » repliées, ouverture mémorisée par
  `ui_memory` ; icône ↺ à droite d'un champ qui diffère du défaut (info-bulle « Défaut : 0,40 mm »)
  ; table unique (nom, plage, défaut, unité) pour info-bulles et ↺ (HP-UX-042).
  Acceptation : par défaut `spin_underlaySpacing` n'est pas visible, déplier la section le rend
  visible ; modifier `spin_rowSpacing` affiche ↺ qui restaure `TatamiParams{}.row_spacing`.
- Modules : `apps/desktop/properties_panel`.

### HP-UX-004 — Palette d'outils organisée par tâches [P1] — ☐ À faire
- Hatch : onglets / boîtes à outils : Numériser, Éditer, Lettrage, Disposer,
  Couleurs…
- À faire : regroupement logique des outils, libellés et icônes cohérents.
- Audit 2026-10-18 (QT-02) : **mesuré** : à 1024 × 640 la palette (≈ 17 actions) déborde dans `»`.
  Le volet « petit écran » (deux colonnes, Terminer/Annuler dans la barre contextuelle) est porté
  par HP-UX-026 ; le regroupement par tâche (formes en un bouton à menu, courbes, satin, couper)
  reste ici.

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
- Audit 2026-10-18 (BRO-07) : **constaté dans le code, la description ci-dessus est périmée** :
  molette = zoom au curseur, Espace+glisser, clic milieu, pinch et Ctrl+molette existent
  (`interaction_map.cpp:65-80`) et le glisser dans le vide en Sélection fait un rectangle (S6).
  Statut ◐ conservé (configurabilité et tests réels sur pavé tactile non vérifiés) ; ne pas
  régresser. Reste : zoom sur la sélection et retour au zoom précédent (HP-UX-023).

### HP-UX-010 — Barre d'état informative [P1] — ◐ Partiel
- À faire : taille de la sélection, nombre de points de la sélection et du
  design, couleur courante, zoom, unité.

### HP-UX-011 — Raccourcis et barres personnalisables [P2] — ☐ À faire

### HP-UX-012 — Assistant de démarrage image → broderie [P1] — ◐ Partiel
- État OpenStitch : bandeau workflow (6 étapes) informatif.
- Hatch : assistants pas à pas pour les débutants.
- À faire : assistant guidé (image → taille → couleurs → fond → numériser →
  vérifier → exporter pour ma machine).
- Audit 2026-10-18 (PAR-09, PAR-06) : l'assistant reste **optionnel et non bloquant** (les
  experts gardent leurs raccourcis, pas de profil Débutant/Expert). Briques planifiées :
  accueil avec « Guide de 2 minutes », « Dessiner » et « Texte » (voir UX-43), préréglages de
  numérisation (HP-UX-030), cadre dans l'import (HP-UX-033), panneau Qualité ouvert après F8
  (HP-UX-031).

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
- Audit 2026-10-18 (BRO-25, PAR-05) : le menu Déboguer est déjà masqué derrière
  `debugMenuEnabled()` (`main_window.cpp:5363-5366`). Restent visibles : « Convertir
  automatiquement en satin (expérimental)… », « Remodelage satin (avancé) », deux entrées pour la
  même intention (« Créer un satin automatique » / « Convertir… »). Proposition : un menu
  « Avancé » replié et une seule entrée de satin automatique ; le message d'échec du satin offre
  un bouton « Utiliser un tatami ».

### Audit UX/UI du 2026-10-18 : lots A à E (HP-UX-017 à HP-UX-044)

Issus de l'audit [`docs/ux/audit-ux-ui-2026-10-18.md`](ux/audit-ux-ui-2026-10-18.md)
(complète `docs/ui-ux-audit-2026-10-13.md`, registre `UX-xx`, que cette roadmap ne citait pas).
Stratégie, arbitrages et ordre : [`docs/ux/strategie-interface.md`](ux/strategie-interface.md) ;
synthèse : [`docs/ux/synthese.md`](ux/synthese.md). **Aucune de ces tâches ne modifie le moteur
de points, les formats ni le schéma `.osp`** (une seule exception à confirmer : HP-UX-044). Toute
mutation reste une `ICommand` ; tout nouveau texte passe par `tr()`. Chaque entrée est câblée dans
`apps/desktop` avec un test QTest headless (sans pixel ni `sleep`, noms `TEST_CASE` ASCII).
Constats : `PAR-`/`BRO-`/`QT-` = rapport d'audit ; `UX-` = audit du 2026-10-13.
Tâches existantes enrichies plutôt que dupliquées : HP-UX-001/002/003/004/009/012/016.
Prérequis commun des lots B et C : **HP-QA-008** (garde de taille minimale, §23).
**HP-UX-040 (contrat de l'IA : repli couleur, annulation tardive) a été retiré le 2026-10-18 à la demande du mainteneur** : l'IA n'est pas une priorité. Les constats `UX-15`/`UX-16` de l'audit du 2026-10-13 restent consignés dans `docs/ui-ux-audit-2026-10-13.md` sans tâche.

#### Lot A — Quick wins et gestes (P1/P2, complexité S)

### HP-UX-017 — Ctrl+O ouvre tout (projet, image, SVG, fichier machine) [P1] — ◐ Partiel (2026-10-18, branche `claude/ux-lot-a-quickwins`)
- Problème : Ctrl+O n'ouvre que des images (`main_window.cpp:699-701`) ; `openPath` sait déjà router `.osp/.dst/.svg/image` (`main_window_ux.cpp:83-89`). Constat PAR-01.
- Comportement attendu : Ctrl+O = « Ouvrir… » universel (filtre « Tous les fichiers pris en charge ») ; « Importer une image… » garde Ctrl+Maj+I ; l'entrée « Ouvrir un projet… » reste. Le garde des modifications non enregistrées s'applique comme aujourd'hui.
- Complexité : S. Composants : `apps/desktop/main_window.cpp`, `main_window_ux.cpp`.
- Dépend de : —
- Acceptation : un test QTest ouvre via l'action un `.osp`, un `.dst`, un `.svg` et un PNG et vérifie la bonne branche ; le raccourci Ctrl+O est unique (aucun conflit `QAction`). Décision utilisateur du 2026-10-18 : changement de raccourci accepté.
- Non-régression : garde de remplacement (Enregistrer / Ne pas enregistrer / Annuler), récents, glisser-déposer.
- Livré : action « Ouvrir… » (Ctrl+O, `openAnyAct_`) qui route par extension via `openPath` (`.osp`, fichier machine, SVG, image) ; « Importer une image ou un SVG… » passe sur Ctrl+Maj+I ; test `ctrlOIsTheUniversalOpenAndImportHasItsOwnShortcut` (Ctrl+O unique). Reste : test de routage par extension à travers le dialogue de fichier (non automatisable sans dialogue natif), filtre mémorisé, mention dans l'aide.

### HP-UX-018 — Échap remonte d'un niveau, jusqu'à la sélection d'objets [P1] — ◐ Partiel (2026-10-18, branche `claude/ux-lot-a-quickwins`)
- Problème : Échap ne désélectionne que les régions et ferme tous les modes d'un coup (`main_window.cpp:6479-6509`). Constat BRO-04. Voir HP-UX-001.
- Comportement attendu : un appui remonte d'un niveau : mode d'édition → outil → sélection d'objets → sélection de régions. Quitter un mode conserve la sélection.
- Complexité : S. Composants : `apps/desktop/main_window.cpp` (raccourci Échap).
- Dépend de : —
- Acceptation : objet sélectionné en outil Sélection, Échap vide `currentSelection()` ; dans un mode d'édition, un premier Échap quitte le mode et garde la sélection, un second la vide.
- Non-régression : Échap pendant un tracé multi-clics, fusion de régions, champs de saisie (Échap y reste local).
- Livré : quand il n'y a plus de mode ni d'outil à relâcher, Échap désélectionne la broderie ou les objets (après les régions, comme avant) ; test `escapeDropsToolThenObjectSelection`. Reste : niveaux « mode d'édition » puis « outil » encore relâchés ensemble par un seul Échap (comportement d'origine conservé pour ne pas casser les tests d'outils) ; aide des gestes.

### HP-UX-019 — Un seul prédicat de sélection et tolérance de clic [P1] — ◐ Partiel (2026-10-18, branche `ux-lot-a2`)
- Problème : trois règles pour le même geste (clic simple filtré ; Maj/Ctrl/Alt non ; rectangle filtré) et aucune tolérance de clic : un tracé ouvert ou un détail de 1 mm est presque inclicable (`selection_hit_test.cpp:16, 71-73, 94-100`, `main_window.cpp:8723-8727, 8879, 8918-8926, 8984`). Constats BRO-02, BRO-03.
- Comportement attendu : une fonction `selectableAt(project, pos, filtre, tolérance)` utilisée par clic, Maj/Ctrl/Alt, rectangle, liste et flèches. Distance au trait ≤ max(0,4 mm ; 4 px écran) ; l'objet qui **contient** le point garde la priorité.
- Complexité : M (tolérance exprimée en mm à travers le zoom). Composants : `apps/desktop/selection_hit_test.*`, `main_window.cpp`.
- Dépend de : —
- Acceptation : filtre de couleur actif → `objectsAtPointMm` et le clic simple renvoient le même ensemble ; un chemin ouvert de 0,1 mm est sélectionné à 0,3 mm, pas à 5 mm ; un objet masqué reste exclu.
- Non-régression : `test_selection_hit_test`, performance du hit-test sur 500 objets.
- Livré : `objectsAtPointMm(project, pos, toleranceMm, accept)` : un objet qui contient le point l'emporte, sinon repli sur le contour à ≤ max(0,4 mm ; 4 px) (trait fictif) ; `MainWindow::selectableObjectsAt` est le prédicat unique (visible, filtres d'affichage, tolérance) pour le clic simple, Maj/Ctrl+clic et « sélectionner dessous » ; tests `toleranceMakesAnOpenPathClickableButStaysLocal`, `toleranceIsAFallbackAndPredicateFilters`. Reste : le rectangle de sélection et les flèches n'utilisent pas encore `selectableObjectsAt` (le rectangle applique déjà le filtre) ; test de performance du hit-test sur 500 objets.

### HP-UX-020 — Cyclage de sélection par re-clic et menu « dessous » enrichi [P2] — ◐ Partiel (2026-10-18, branche `ux-lot-a2`)
- Problème : Alt+clic ouvre un menu de noms sans pastille ni type ; un nom en double reçoit « (#id) » (`interaction_map.cpp:90`, `main_window.cpp:8980-9023`). Constat BRO-01.
- Comportement attendu : un clic répété au même point (rayon 2 px) sélectionne l'objet suivant dessous ; la barre d'état affiche « 2/4 sous le curseur » ; les entrées du menu portent pastille de couleur et type de point (survol = surbrillance).
- Complexité : S. Composants : `apps/desktop/main_window.cpp`, `canvas_view.cpp`.
- Dépend de : HP-UX-019.
- Acceptation : trois formes superposées, trois clics identiques sélectionnent haut, milieu, bas, puis reviennent en haut ; un clic ailleurs remet le cycle à zéro.
- Livré : re-clic au même point (rayon 2 px) sélectionne l'objet suivant dessous, message « n/N sous le curseur » dans la barre d'état ; le menu « dessous » affiche une pastille de couleur ; test `clickingTheSameSpotCyclesThroughOverlappingObjects`. Reste : type de point dans les entrées du menu, surbrillance de l'objet au survol d'une entrée.

### HP-UX-021 — Vectorisation (F7) sans modale [P2] — ◐ Partiel (2026-10-18, branche `claude/ux-lot-a-quickwins`)
- Problème : chaque F7 ouvre une modale « Détail » pour un curseur déjà mémorisé (`main_window_workflow.cpp:175-206`). Constat PAR-07.
- Comportement attendu : F7 applique la dernière valeur (`ui_memory`) ; Maj+F7 ouvre le réglage.
- Complexité : S. Composants : `apps/desktop/main_window_workflow.cpp`.
- Dépend de : —
- Acceptation : F7 n'ouvre aucun `QDialog` ; Maj+F7 en ouvre un et mémorise la valeur ; sortie identique à l'actuelle pour une même valeur.
- Livré : F7 réutilise le dernier détail (`ui_memory`) sans dialogue ; l'entrée de menu et Maj+F7 ouvrent le réglage ; `askVectorizeDetail` extrait ; test `quickVectorizeOpensNoDialogAndMenuStillAsks`. Reste : afficher le détail utilisé dans la barre d'état.

### HP-UX-022 — Sortir de la simulation ; rendu réaliste après lecture [P1] — ◐ Partiel (2026-10-18, branche `claude/ux-lot-a-quickwins`)
- Problème : `simStep_ >= 0` désactive le rendu réaliste (`main_window_realistic.cpp:140`) et ne revient à −1 qu'à la régénération (`main_window.cpp:1229, 7790, 7796`) ; aucun bouton de sortie. Constat PAR-10 (chemin lu, effet non exécuté).
- Comportement attendu : bouton « Quitter la simulation » et sortie automatique en fin de lecture ; Ctrl+Maj+R affiche alors le rendu complet. Alternative : tracer les segments réalistes jusqu'à `simStep_`.
- Complexité : S (sortie) / M (réaliste progressif). Composants : `apps/desktop/main_window.cpp`, `main_window_realistic.cpp`.
- Dépend de : —
- Acceptation : lecture jusqu'au bout puis activation du rendu réaliste → rendu complet affiché ; le bouton remet `simStep_ = -1` sans régénérer.
- Livré : bouton « ⏹ Fin » (`simExitAct_`), sortie automatique en fin de lecture et quand le curseur atteint la fin (`exitSimulation()` : `simStep_ = -1` puis `renderStitches()`) ; tests `simulationExitButtonAndEndOfSliderLeaveSimulationMode`, `endOfPlaybackLeavesSimulationMode` ; l'assertion de `simulationPlayPauseResumeAndSeekChangesRenderedPoints` est adaptée (fin de curseur = hors simulation, couture complète toujours vérifiée). Reste : rendu réaliste progressif jusqu'à `simStep_` (variante M), test du rendu réaliste réellement affiché après lecture.

### HP-UX-023 — Ctrl+A selon la couche, touches de couches, zoom sur la sélection [P2] — ◐ Partiel (2026-10-18, branche `ux-lot-a2`)
- Problème : Ctrl+A sélectionne des régions (objets sur Ctrl+Maj+A, `main_window.cpp:895-898`) ; aucun raccourci de couche ; aucun zoom sur la sélection. Constats BRO-05, BRO-08, BRO-22.
- Comportement attendu : Ctrl+A suit la couche affichée ; 1 à 4 = Image/Régions/Vecteurs/Points ; Z = zoom sur la sélection, Maj+Z = zoom précédent, Ctrl+1 = 100 %.
- Complexité : S. Composants : `apps/desktop/main_window*.cpp`, `canvas_view.cpp`, `interaction_map.cpp` (aide).
- Dépend de : —
- Acceptation : projet vectoriel sans segmentation → Ctrl+A sélectionne tous les objets visibles ; après Z la vue contient la boîte de la sélection ; l'aide des gestes liste les nouvelles touches.
- Non-régression : Espace/focus (UX-40), touches dans les champs de saisie.

#### Lot B — Responsive et panneaux (P1/P2)
- Livré : Ctrl+A suit la couche (objets visibles si Vecteurs est affiché, sinon régions ; l'action est active dès qu'il y a des formes) ; touches 1/2/3/4 = Image/Régions (W conservé)/Vecteurs/Points ; Z = zoom sur la sélection, Maj+Z = zoom précédent (aller-retour), Ctrl+1 = taille réelle ; tests `selectAllFollowsTheActiveLayer`, `layerKeysAndZoomShortcutsAreUnique`, `zoomToSelectionFramesTheObjectAndPreviousRestoresTheView`. Reste : mention dans l'aide des gestes (`interaction_map`).

### HP-UX-024 — Docks en onglets, docks Fils et Couleurs défilants [P1] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : Analyse, Historique, Fils, Couleurs sont empilés à droite (`main_window_analysis.cpp:176`, `main_window_editing.cpp:637`, `main_window_threads.cpp:102`, `main_window_colors.cpp:25`) ; `ThreadPanel` (≈ 450 px minimum) n'a pas de zone défilante ; `colorsDock_` est créé paresseusement et sa place est perdue (`main_window_colors.cpp:19-25`). Constats QT-03, QT-04, QT-15 (tailles estimées, classe B).
- Comportement attendu : `tabifyDockWidget` des quatre docks ; `QScrollArea` (`setWidgetResizable(true)`) pour Fils et `SegmentationColorsPanel` ; `colorsDock_` créé caché au constructeur ; disposition restaurée bornée à l'écran.
- Complexité : M. Composants : `apps/desktop/main_window_*.cpp`, `thread_panel.*`, `segmentation_colors_panel.*`, `main_window_ux.cpp`.
- Dépend de : HP-QA-008.
- Acceptation : tous docks actifs, `window.minimumSizeHint().height() <= 480` ; `threadDock_->minimumSizeHint().height() <= 200` ; aller-retour `saveUiLayout`/`restoreUiLayout` incluant `colorsDock_`.
- Non-régression : tests qui supposent plusieurs docks visibles ensemble ; « Réinitialiser la disposition ».
- Livré : docks Analyse, Fils, Couleurs, Ordre, Workflow dans des zones défilantes (`scrollableDockContent`), Analyse/Historique/Fils/Couleurs en onglets, `colorsDock_` créé caché au constructeur, docks flottants bornés à l'écran. Mesuré : minimum de fenêtre tous docks ouverts 1291 × 1443 → 701 × 373, dock Fils 697 × 393 → 68 × 84. Reste : validation visuelle manuelle.

### HP-UX-025 — Dialogues bornés à l'écran et dialogues longs repensés [P1] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : aucun `QScrollArea`/`QSplitter` dans les dialogues ; tailles initiales jusqu'à 1000 × 720 ; Texte ≈ 750-800 px, Export machine ≈ 690, Segmentation IA ≈ 900 de haut ; Mélange de couleurs `setMinimumWidth(970)` ; accueil ≈ 440 px (`production_dialog.cpp:56`, `ai_segmentation_dialog.cpp:135`, `text_dialog.cpp:285-357`, `machine_export_dialog.cpp:43-181`, `color_blend_dialog.cpp:195`, `empty_state_widget.cpp:27-86`). Constats QT-07 à QT-12 (classe B pour les tailles ; `970` est constaté).
- Comportement attendu : utilitaire `fitDialogToScreen(QDialog*)` au `showEvent` (92 % de `availableGeometry`, contenu long dans un `QScrollArea` transparent, boutons OK/Annuler hors zone défilante) ; Texte en colonne défilante ou onglets Texte/Avancé ; Export machine avec « Optimisations » repliable ; Mélange de couleurs empilé sous un seuil ; Segmentation IA en `QSplitter` ; accueil défilant.
- Complexité : M (plusieurs fichiers disjoints, parallélisables). Composants : `apps/desktop/*_dialog.cpp`, `text_dialog.cpp`, `empty_state_widget.cpp`.
- Dépend de : HP-QA-008.
- Acceptation : pour chaque dialogue, `d.resize(10000,10000); show();` → taille ≤ 92 % de la zone utile ; `minimumSizeHint()` ≤ 880 × 440 (Mélange, Texte, Export, IA) ; accueil avec 5 noms de récents longs : toutes les actions atteignables.
- Non-régression : valeurs mémorisées, `test_help_dialogs` (aide Gestes 460 × 320), focus initial des dialogues.
- Livré : `responsive.hpp` (`scrollDialogBody`, `fitDialogToScreen` à 92 %, `ElidedTextLabel`) appliqué à Texte, Export machine (section « Optimisations » repliable), Mélange (`setMinimumWidth(970)` retiré), Segmentation IA, Production, Import, aides et accueil. Minimums avant → après : Texte 1152 × 658 → 296 × 116, Export 1265 × 448 → 356 × 116, Mélange 970 × 515 → 296 × 122, IA 1172 × 866 → 200 × 116. Reste : splitter de la Segmentation IA (remplacé par une zone défilante), Préférences IA hors périmètre.

### HP-UX-026 — Barres du bas fusionnées, palette sur deux colonnes, densité automatique [P1] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : quatre rangées de barres + bande des fils ≈ 255 px ; palette ≈ 17 actions qui déborde dans `»` (Terminer/Annuler la forme inclus) ; « Confortable » par défaut même sur 768 px ; barre d'état ≈ 450 px figés (`main_window.cpp:470-476, 507-556, 6234-6344, 6355-6477`, `main_window_threads.cpp:541-548`, `app_theme.cpp:434-444`). Constats QT-01, QT-02, QT-14, QT-16, QT-17, BRO-24. **Mesuré** : canevas 360 × 421 à 1024 × 640.
- Comportement attendu : simulation + bande des fils en une rangée visible seulement si une séquence existe ; barre contextuelle dans la principale ; palette en deux colonnes sous 480 px de haut, Terminer/Annuler dans la barre contextuelle pendant un tracé ; densité « Compacte » au premier lancement si la hauteur utile < 800 ; libellés sans espaces de fin ; `cursorLabel_` élidable.
- Complexité : M. Composants : `apps/desktop/main_window.cpp`, `main_window_threads.cpp`, `app_theme.cpp`, `design_tokens.cpp` (fichiers de convergence : un seul propriétaire).
- Dépend de : HP-QA-008 ; voir HP-UX-004 (regroupement par tâche).
- Acceptation : à 911 × 480 avec séquence chargée, `view_->height() >= 260` et `view_->width() >= 450` ; `finishDrawAct_` visible pendant un tracé ; sans `QSettings` et écran simulé de 480 px de haut, `AppTheme::density() == Compact` ; `statusBar()->minimumSizeHint().width() <= 450`.
- Non-régression : tests qui retrouvent les barres par `objectName`, `hintsWidgetNeverForcesWindowWidth`.
- Livré : simulation et bande des fils sur une rangée visible seulement avec une séquence, palette sur deux colonnes sous 520 px de haut, densité « Compacte » au premier lancement sous 800 px de hauteur utile, barre d'état 754 → 250 px, barre contextuelle élidable (921 → 45), bouton de cadre de la barre d'état à minimum explicite. Reste : fusion de la barre contextuelle avec la principale (tronquerait la principale à 911 px), Terminer/Annuler la forme dans la barre contextuelle ; **mesuré** : à 911 × 480 avec Document et Propriétés ouverts le canevas ne fait que ~205 × 305 (≥ 450 de large impossible sans le mode portable : 471 × 305).

### HP-UX-027 — Mode « portable » à une colonne et inspecteur étroit [P2] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : deux colonnes ouvertes à 911 px laissent ≈ 250 px au canevas ; l'inspecteur ne rétrécit pas sous ses libellés (`main_window.cpp:499`, `properties_panel.cpp:163-170`) ; aucun `QSplitter`. Constats QT-05, QT-06 (classe B).
- Comportement attendu : action « Panneaux : mode portable » (proposée automatiquement sous 1000 px) qui tabifie Document et Propriétés dans une seule colonne ; `QFormLayout::WrapLongRows` dans l'inspecteur.
- Complexité : M. Composants : `apps/desktop/main_window*.cpp`, `properties_panel.cpp`.
- Dépend de : HP-UX-024.
- Acceptation : à 911 × 480 après l'action, `view_->width() >= 450` ; `propertiesDock_->minimumSizeHint().width() <= 260`.

#### Lot C — Workflows de broderie (P1/P2)
- Livré : action « Mode portable » (menu Panneaux) : Document, Ordre, Workflow et Propriétés en une colonne d'onglets, persistée, proposée une fois sous 1000 px de large, annulée par « Réinitialiser la disposition » ; `WrapLongRows` dans l'inspecteur.

### HP-UX-028 — Type de point identique pour une ou N formes, aussi au clic droit [P1] — ◐ Partiel (2026-10-18, branche `ux-lot-c1-pr`)
- Problème : une forme sans couture n'a aucun choix de type dans l'inspecteur ; 2 formes ou plus ont un combo + « Appliquer » ; le menu contextuel n'offre pas le type ; « Directionnel » absent pour une forme neuve (`properties_panel.cpp:452-550, 597-634`, `main_window.cpp:5317-5328, 5374-5386, 6232-6301`). Constats PAR-02, PAR-03, PAR-04.
- Comportement attendu : mêmes quatre boutons (Contour, Tatami, Satin, Directionnel) pour toute sélection de formes, en tête de l'inspecteur, dans la barre contextuelle et le clic droit ; un clic crée la couture avec le dernier réglage, en **une** `ICommand` annulable (réutiliser `setStitchTypeForSelection`).
- Complexité : M. Composants : `apps/desktop/properties_panel.cpp`, `main_window*.cpp`.
- Dépend de : HP-UX-002 ; HP-OBJ-019.
- Acceptation : forme neuve, un clic sur chacun des quatre types → couture créée, zéro modale, un pas d'annulation ; clic droit sur forme non cousue → sous-menu à quatre entrées.
- Non-régression : retouches manuelles (ADR-014), conversion de groupe, `check_no_raw_sequence_bypass`.
- Livré : les quatre boutons Contour / Tatami / Satin / Directionnel (signal `stitchTypeRequested` vers `setStitchTypeForSelection`) en tête de l'inspecteur (forme neuve, couture, N formes), dans la barre contextuelle (1 ou N formes) et dans un sous-menu « Type de points » du clic droit sur une forme non cousue. Test existant adapté : `inspectorShowsNObjets` (le combo `combo_multiType` n'existe plus).

### HP-UX-029 — Avertir avant d'invalider des retouches manuelles [P1] — ◐ Partiel (2026-10-18, branche `ux-lot-c1-pr`)
- Problème : `paramsEdited` s'exécute sans tester l'état de l'objet (`main_window.cpp:6931-6948`) : une modification de paramètre rend les retouches de points obsolètes (signalé après coup par ⚠, `document_panel.cpp:43-56`). Les retouches ne sont pas perdues (ADR-014) mais plus appliquées. Constat BRO-20.
- Comportement attendu : à la première modification d'un objet « ✎ », bandeau non modal dans l'inspecteur « Cette modification désactive N retouches (Ctrl+Z pour annuler) » ; N dans l'info-bulle de ⚠ ; bouton « Réappliquer les retouches » quand c'est possible. Pas de confirmation bloquante.
- Complexité : M (comptage en lecture seule via `classify_all_edit_states`, aucun changement de cœur). Composants : `apps/desktop/properties_panel.*`, `main_window.cpp`.
- Dépend de : —
- Acceptation : objet « ✎ » à 14 retouches → `paramsEdited` affiche l'étiquette avec « 14 » ; annuler restaure l'état « ✎ » ; objet propre → aucun bandeau.
- Non-régression : `docs/lot8-manual-editing-design.md` (états Clean/ManuallyEdited/Dirty), fusion de commandes scalaires.
- Livré : bandeau non modal `label_overridesWarning` « Cette modification désactive N retouches » avec bouton « Réappliquer les retouches », qui suit annulation, rétablissement et abandon ; N repris dans l'info-bulle de ⚠ de l'inspecteur. Reste : bandeau pour un groupe d'objets retouchés (limité à l'objet unique), ⚠ du panneau Document inchangé.

### HP-UX-030 — Préréglages de numérisation automatique et résumé chiffré [P2] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : F8 ouvre deux modales puis ~25 réglages d'expert ; le résumé final ne donne que le nombre d'objets (`main_window.cpp:4320-4342, 4369-4768, 4929-4937`). Constat PAR-06. Voir HP-UX-012.
- Comportement attendu : trois préréglages (« Logo / aplats », « Dessin au trait », « Photo / pointillé ») ; section « Avancé » repliée et mémorisée ; question Ajouter/Remplacer intégrée au dialogue ; résumé final : points, durée estimée, fils, nombre de problèmes, bouton « Voir ». **Aucun changement de moteur** : seuls les paramètres existants sont regroupés.
- Complexité : M. Composants : `apps/desktop/main_window.cpp` (ou `main_window_workflow.cpp`), `ui_memory.hpp`.
- Dépend de : HP-QA-001 (conflits de fusion).
- Acceptation : F8 sur segmentation neuve = une modale ; chaque préréglage produit les mêmes paramètres que les valeurs équivalentes saisies à la main (test d'égalité des options) ; valeurs rechargées au lancement suivant.
- Livré : préréglages « Logo / aplats », « Dessin au trait », « Photo / pointillé » (`autodigitize_presets.hpp`, remplissent les options existantes, aucun changement de moteur), réglages avancés repliés et mémorisés, Ajouter/Remplacer intégré (une modale), résumé final « N points, durée, K fils, P problèmes » en barre d'état avec bouton « Voir » ; tests d'égalité préréglage = options. Reste : le chemin SVG garde sa question Ajouter/Remplacer.

### HP-UX-031 — Panneau « Qualité » unique [P2] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : dock Analyse masqué au démarrage, statistiques en `QMessageBox`, trois dialogues texte (fidélité, épaisseurs, diagnostic), corrections une à une (`main_window_analysis.cpp:177, 211-311, 338-393`, `main_window.cpp:7892`). Constat PAR-13.
- Comportement attendu : étape 1 : ouverture automatique du dock Analyse après F8 et avant l'export ; étape 2 : onglets Problèmes / Statistiques / Fidélité / Épaisseur ; « Corriger tout ce qui est sûr » (déplacer dans le cadre, allonger, scinder), annulable en un pas. Règles existantes de `stitch_analysis` uniquement.
- Complexité : L (étape 2). Composants : `apps/desktop/main_window_analysis.cpp`, `libs/stitch_analysis` (lecture seule).
- Dépend de : HP-PROD-007/008 ; HP-UX-024.
- Acceptation : après F8 le dock s'ouvre seul ; « Corriger tout » = une entrée d'annulation et un re-calcul sans nouveau problème sûr ; mêmes diagnostics que `project quality` (CLI).
- Livré (étape 1) : le dock Analyse s'ouvre seul après F8 et avant l'export, onglets Fidélité / Épaisseur / Diagnostic dans le dock, « Corriger tout ce qui est sûr » en une `CompositeCommand` (un seul Ctrl+Z, aucune nouvelle règle d'analyse). Reste : onglets Problèmes / Statistiques dans le même contrôle, agrandissement de texte encore individuel.

### HP-UX-032 — Export rapide et décision explicite avant export [P2] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : Ctrl+E ouvre ~15 contrôles puis « Choisir le fichier… » (bouton par défaut) puis la boîte système ; « Voir les problèmes » ferme le dialogue et n'apparaît qu'en cas d'erreur ; aucun enregistrement `.osp` proposé ; l'analyse est recalculée à chaque case ; coupes réglées à deux endroits (`machine_export_dialog.cpp:63-187, 318`, `main_window.cpp:8316, 8378, 8422-8443`, `generation_options_dialog.cpp:43-60`). Constats PAR-12, PAR-18, UX-10 (partie ouverte).
- Comportement attendu : Ctrl+Maj+E « Exporter avec les derniers réglages » vers le dernier dossier ; « Enregistrer le projet d'abord » et « Voir les problèmes » (sans fermer) dès qu'il y a un avertissement ; bouton par défaut non destructif tant que des risques existent ; aperçu recalculé après 300 ms d'inactivité ; vocabulaire « réglage du projet / de ce fichier » commun aux deux écrans de coupes.
- Complexité : M. Composants : `apps/desktop/machine_export_dialog.*`, `main_window.cpp`.
- Dépend de : —
- Acceptation : export rapide = raccourci + nom de fichier ; le bouton par défaut n'est pas « Choisir le fichier… » quand `blocking` ou des avertissements existent ; cocher 5 options en < 300 ms ne déclenche qu'une analyse.
- Non-régression : octets DST/PES/JEF/EXP inchangés (SHA-256 sur un projet de référence).
- Livré : Ctrl+Maj+E « Exporter avec les derniers réglages » (`runMachineExport` commun, repli sur le dialogue si le format refuse) ; dialogue : « Enregistrer le projet d'abord », « Voir les problèmes » sans fermer, bouton par défaut non destructif en présence de risques, aperçu recalculé après 300 ms, vocabulaire « réglage du projet / de ce fichier » ; test d'égalité d'octets (DST, PES, JEF, EXP) export rapide contre dialogue. Test existant adapté : `optionReachesTheSummaryAndTheCaller` (délai de 300 ms voulu).

### HP-UX-033 — Cadre accessible depuis l'import et la barre d'état [P2] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : l'import constate un dépassement mais le cadre ne se règle que par menu ou barre contextuelle (`import_dialog.cpp:211-218`, `main_window.cpp:1095, 6347`). Constats PAR-08, PAR-20.
- Comportement attendu : sélecteur de cadre (préréglages existants) dans `ImportDialog` ; taille du cadre cliquable dans la barre d'état.
- Complexité : S/M. Composants : `apps/desktop/import_dialog.*`, `main_window.cpp`.
- Dépend de : —
- Acceptation : le cadre choisi dans l'import est celui du projet ; Annuler restaure l'ancien cadre.
- Livré : sélecteur de cadre dans `ImportDialog` (le cadre ne devient celui du projet qu'à la validation), bouton « Cadre L × H mm » dans la barre d'état. Reste : validation visuelle.

### HP-UX-038 — Valeurs mixtes et portées honnêtes en multi-sélection [P1] — ◐ Partiel (2026-10-18, branche `ux-lot-c1-pr`)
- Problème : le formulaire de groupe affiche la valeur d'un représentant comme commune ; deux modèles coexistent (« Appliquer à N objets » et champs en direct) ; entrée/sortie, dégradé et bordure n'agissent que sur un objet (`properties_panel.cpp:597-643, 836, 1206, 1304`). Constats BRO-17, UX-33, UX-34.
- Comportement attendu : « — » par champ quand les valeurs diffèrent ; éditer un champ n'uniformise que ce champ ; un seul modèle (édition en direct, commande composite) ; contrôles à portée individuelle isolés dans « Objet actif seulement ».
- Complexité : M. Composants : `apps/desktop/properties_panel.*`, `main_window_editing.cpp`.
- Dépend de : HP-OBJ-019.
- Acceptation : deux tatamis à 0,3 et 0,5 mm affichent un champ indéterminé ; changer l'angle ne modifie pas la densité ; une seule entrée d'annulation par édition.

Le lot C comprend aussi **HP-PERF-009** (régénération différée à l'édition de paramètres, §22).

#### Lot D — Cohérence et productivité (P2)
- Livré : « — » pour les champs de contenu mixte (nombres, cases tri-état, listes sans choix courant), éditer un champ n'uniformise que ce champ, plus de bouton « Appliquer à N », rafales d'édition de groupe fusionnées en un pas d'annulation, contrôles à portée individuelle isolés sous « Objet actif seulement » (entrée/sortie, connecteur, dégradé, bordure). Tests existants adaptés : `multiSelectionBlockAppliesSettingsToAllObjectsInOneStep`, `multiSelectionBlockCanConvertEveryObjectToContourInOneStep` (l'ancien bloc « Appliquer » est remplacé par les champs en direct et les boutons de type).

### HP-UX-034 — Mode « Isoler » (protection de session) [P2] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : `EmbroideryObject::locked` fige l'ordre de couture, il ne protège pas l'objet (`embroidery_object.hpp:393`, `selection_hit_test.cpp:97`) ; un glisser accidentel de 3 mm sur un fond fini casse l'alignement. Constat BRO-15.
- Comportement attendu : « Isoler la sélection » masque/atténue les autres objets et les exclut du hit-test, du rectangle et des flèches **pendant la session** ; « Quitter l'isolation » les restaure. **Décision du 2026-10-18 : aucun champ persistant, aucun changement de `.osp`.** Un verrou d'édition persistant reste hors périmètre (nécessiterait un champ et une migration).
- Complexité : M. Composants : `apps/desktop/selection_hit_test.*`, `canvas_view.cpp`, `document_panel.*`.
- Dépend de : HP-UX-019 (prédicat unique).
- Acceptation : en isolation, un clic sur un objet non isolé ne le sélectionne pas et le glisser du fond ne le déplace pas ; l'état n'est pas écrit dans le `.osp` (SHA-256 identique avant/après sauvegarde).
- Livré : action « Isoler la sélection » (Maj+I, case à cocher) : les formes non isolées sont masquées et exclues du clic, de Maj/Ctrl/Alt+clic et du rectangle (via `vectorPassesFilter`) ; état de session (`isolatedIds_`) remis à zéro à chaque nouveau document ; test `isolationHidesAndExcludesOthersWithoutTouchingTheProject` (octets du `.osp` identiques avant/pendant l'isolation, aucun champ ajouté). Reste : atténuation (au lieu de masquage) des autres formes, exclusion des flèches clavier, entrée dans le menu contextuel et la liste Document.

### HP-UX-035 — Copier/coller le style et défauts de points [P2] — ◐ Partiel (2026-10-18, branche `ux-lot-d3`)
- Problème : pas de « copier le style » ; défauts de création en dur (0,4 / 3 / 0°, `main_window.cpp:4232-4244`) ; `ui_memory` ne couvre pas les paramètres de couture. Constat BRO-21. Voir HP-UX-007.
- Comportement attendu : Ctrl+Alt+C / Ctrl+Alt+V sur la sélection (une `ICommand`) ; « Utiliser ces réglages comme défaut pour les nouveaux objets » (stocké par `QSettings`, jamais dans le projet).
- Complexité : M. Composants : `apps/desktop/properties_panel.*`, `ui_memory.hpp`, `main_window_editing.cpp`.
- Dépend de : HP-UX-003.
- Acceptation : coller le style de A sur B donne `B.params == A.params` en une commande annulable ; un nouvel objet reprend le défaut enregistré.
- Livré : « Copier le style » (Ctrl+Maj+C) et « Coller le style » (Ctrl+Maj+Y ; Ctrl+Alt écarté car c'est AltGr sur clavier AZERTY) : les paramètres de couture de l'objet actif sont appliqués, en **une** `CompositeCommand` annulable, aux objets sélectionnés de même type de point ; les autres types sont ignorés avec un message ; test `pasteStyleCopiesParamsInOneUndoableStepAndSkipsOtherTypes`. Les défauts de création sont couverts par HP-UX-002 (dernier réglage mémorisé en `QSettings`). Reste : « Utiliser ces réglages comme défaut » explicite, copie de la couleur de fil.

### HP-UX-036 — Bascule de vue à quatre états et barre de simulation informative [P2] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : quatre couches dans Affichage ▸ Calques, une seule en barre ; une couche masquée rend le clic muet sans explication ; compteur de simulation = indice de commande, pas de saut par couleur ni de durée (`main_window.cpp:1060-1075, 6114, 7699-7735, 7807, 8721`). Constats BRO-23, PAR-11, UX-29.
- Comportement attendu : bascule Dessin / Points / Réaliste / Simulation dans la barre (le menu Calques reste) ; message « Vecteurs masqués » quand un clic tombe sur une couche cachée ; simulation : « point n/N · mm:ss · fil 3/7 », couleur précédente/suivante.
- Complexité : M. Composants : `apps/desktop/main_window*.cpp`. Voir HP-SIM-002/004.
- Dépend de : HP-UX-022.
- Acceptation : activer « Points » masque `showVectorsAct_` ; un clic en mode Points affiche un message d'état précis ; le saut par couleur positionne `simStep_` au début du bloc.
- Livré : boutons « ⏮ Couleur » / « Couleur ⏭ » (saut au début du bloc de couleur précédent/suivant, sortie de la simulation après le dernier), « fil k/m » dans l'info de simulation, message d'état explicite quand un clic tombe sur une forme alors que la couche Vecteurs est masquée (touche 3) ; tests `simulationColorJumpsLandOnColorBlocks`, `hiddenVectorLayerExplainsAClickOnAShape`. Reste : bascule Dessin / Points / Réaliste / Simulation dans la barre, durée estimée « mm:ss » (dépend d'une vitesse machine).

### HP-UX-037 — Liste Document : sélection étendue, vecteurs, ordre par glisser-déposer [P2] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : sélection simple pour les objets, vecteurs non cousus absents, repli et défilement perdus à chaque rafraîchissement, Document et Ordre de couture dupliqués (`document_panel.cpp:237, 276, 349-402, 523-563`, `main_window.cpp:6745-6747, 7224-7295`). Constats BRO-06, PAR-14, UX-13, UX-45.
- Comportement attendu : `ExtendedSelection` synchronisée avec `currentSelection()` ; lignes vectorielles indépendantes des coutures ; repli/sélection/défilement restaurés par identité ; ordre de couture par glisser-déposer dans la même liste ; menu de ligne (Masquer, Isoler, Figer, Supprimer).
- Complexité : L. Composants : `apps/desktop/document_panel.*`, `main_window.cpp`.
- Dépend de : HP-OBJ-001, HP-OBJ-010, HP-OBJ-011 ; HP-QA-001.
- Acceptation : Ctrl+clic sur 3 lignes → 3 identifiants dans la sélection et « 3 objets » dans l'inspecteur ; SVG de plusieurs formes entièrement navigable avant création des points ; fermer un groupe puis rafraîchir ne change ni son repli ni la position de lecture.
- Livré : liste Objets en sélection étendue synchronisée avec la fenêtre, formes vectorielles non cousues comme lignes indépendantes, repli/sélection/défilement restaurés par identité après rafraîchissement, glisser-déposer qui émet `orderChangeRequested` (commande de réordonnancement existante), menu de ligne (Masquer, Figer, Supprimer) ; 8 tests. Reste : le glisser-déposer à la souris réelle n'est pas exercé en test (`handleDrop` appelé directement), fusion visuelle avec le dock « Ordre de couture » (laissé en place). Test existant adapté : `documentPanelCheckBoxesAndRenameAreUndoableCommands` (une 3ᵉ ligne vectorielle voulue).

### HP-UX-039 — Contrat d'état de l'interface (workflow, enregistrement, erreurs) [P2] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : le workflow est un lanceur (Vérification/Export « disponible » dès qu'une séquence existe) ; l'heure d'enregistrement est celle du rafraîchissement ; erreurs de génération/autosave uniquement en barre d'état éphémère (`main_window.cpp:6730-6734, 526-536`, `main_window_ux.cpp:260-278`). Constats UX-14, UX-32, UX-26.
- Comportement attendu : états d'étape liés à la révision du document (vérifié, exporté, périmé après modification) ; instant de sauvegarde mémorisé ; notification d'erreur persistante avec reprise. **En mémoire seulement** : aucun champ `.osp`.
- Complexité : M. Composants : `apps/desktop/main_window_workflow.cpp`, `main_window_ux.cpp`, `workflow_panel.*`.
- Dépend de : HP-QA-001 (partiel).
- Acceptation : sauver à T1, éditer, annuler à T2 → T1 affiché ; export réussi visible puis invalidé par une modification ; un SVG ne réclame pas d'étape Régions.
- Livré : l'instant « Enregistré à HH:mm » est mémorisé à la sauvegarde/ouverture (`savedAt_`) et n'est plus l'heure du dernier rafraîchissement (UX-32) ; test `savedIndicatorShowsTheSaveTimeNotTheRefreshTime`. Reste : états d'étape du workflow liés à la révision du document (UX-14), notifications d'erreur persistantes (UX-26).

#### Lot E — Finitions (P2/P3)

### HP-UX-041 — Barre Fils accessible au clavier [P2] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : pastilles peintes, activées à la souris ; « +N » non actionnable (`thread_strip.cpp:25-31, 55-71, 111-157`). Constat UX-46. Voir HP-THR-005, HP-I18N-004.
- Comportement attendu : focus et activation clavier des pastilles (boutons natifs ou `QAccessible`), « +N » ouvre la liste complète, nom et code annoncés.
- Complexité : S. Composants : `apps/desktop/thread_strip.*`.
- Dépend de : —
- Acceptation : tous les fils sélectionnables au clavier au-delà de la largeur disponible ; test de focus/activation `ThreadStrip` (aucun n'existe aujourd'hui).
- Livré : un seul arrêt de tabulation avec focus visible, flèches/Début/Fin/Entrée/Espace/Ctrl+flèches, case « +N » réelle (menu de tous les fils avec icône, nom et code), annonce du fil focalisé ; `test_thread_strip` (7 cas).

### HP-UX-042 — Icône de type de point et info-bulles complètes [P3] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : le type se lit (préfixe texte) mais ne se voit pas ; info-bulles partielles (`document_panel.cpp:209-217`, `properties_panel.cpp:801, 901-958`). Constats BRO-13, BRO-14.
- Comportement attendu : icône de type (tatami = hachures, satin = colonne, contour = pointillé) dans la liste ; filtre par type ; table unique (nom, plage, défaut, unité) alimentant info-bulles et bouton ↺ (HP-UX-003).
- Complexité : S. Composants : `apps/desktop/document_panel.*`, `properties_panel.*`, `ui_icons.*`.
- Dépend de : HP-UX-003.
- Acceptation : `item->icon(0)` diffère selon le type ; chaque champ d'inspecteur a une info-bulle avec plage et défaut.
- Livré (icône et filtre) : `icons::stitchGlyph()` (tatami, satin, contour, directionnel, texte, vecteur) dans la liste Document, combo de filtre par type compact à côté de la recherche. Reste : table unique plage/défaut/unité pour les info-bulles de l'inspecteur et le bouton ↺ (dépend de HP-UX-003).

### HP-UX-043 — Pastilles et règles adaptées au DPI et à la taille de police [P3] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : pastilles `QPixmap` non adaptées au DPR, règles à 7,5 pt et boîtes de 12 px fixes (`import_dialog.cpp:38`, `text_dialog.cpp:356`, `thread_panel.cpp:198`, `ruler.cpp:37, 90`, `app_theme.cpp:352`). Constats QT-13, UX-28.
- Comportement attendu : fabrique `makeSwatch(couleur, tailleLogique, dpr)` ; métriques des règles dérivées de la police.
- Complexité : S. Composants : `apps/desktop/ruler.cpp`, `app_theme.cpp` (convergence), dialogues.
- Dépend de : —
- Acceptation : avec `QT_SCALE_FACTOR=1.5`, `swatch.devicePixelRatio() == qApp->devicePixelRatio()` ; police agrandie : `sizeHint` des règles cohérent.
- Livré : `makeSwatch(couleur, taille, dpr)` (`ui_swatch.hpp`), règles dont la police et les boîtes suivent les métriques et réagissent au changement de police, aperçu d'import net en HiDPI. Reste : `text_dialog.cpp`, `thread_panel.cpp`, `app_theme.cpp` à basculer sur `makeSwatch`.

### HP-UX-044 — Import non proportionnel : appliquer ou retirer la hauteur [P3] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : `placement()` renvoie largeur et hauteur mais `mm_per_pixel` n'utilise que la largeur (`libs/document/src/image_placement.cpp:67-71`) ; le champ hauteur est déjà grisé honnêtement (`import_dialog.cpp:64-72`). Constat UX-07.
- Comportement attendu : soit retirer définitivement le champ hauteur modifiable (état actuel documenté), soit appliquer les deux axes partout. **Toute option qui touche le placement est à valider sans casser les `.osp` existants.**
- Complexité : S (retrait) / L (anisotrope). Composants : `apps/desktop/import_dialog.*`, `libs/document` (si anisotrope).
- Dépend de : —
- Acceptation : taille saisie, canevas, segmentation et sortie machine coïncident, ou le champ n'est plus modifiable ; projets existants rechargés à l'identique.

---

## 21. Internationalisation et accessibilité (I18N)

Aujourd'hui : textes en **français** dans `tr()`, aucun `QTranslator`, aucun
fichier `.ts`. Hatch est disponible en une dizaine de langues.
- Livré (option sûre) : aucun import anisotrope, `libs/document` et `.osp` intacts ; la hauteur reste liée à la largeur (la modifier redimensionne l'image entière) avec une info-bulle explicite ; « Conserver les proportions » reste verrouillée. Écart avec l'énoncé : la hauteur n'est pas grisée dans le code actuel.

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

État au 2026-10-13 : segmentation et auto-numérisation tournent dans un fil de travail
(`BusyIndicator::run`), génération parallèle par objet ; auto-numérisation < 1 s sur l'image
de référence. Restent : progression et annulation, régénération de toute la séquence
à chaque modification (~40 ms), scène reconstruite à chaque rafraîchissement.

### HP-PERF-001 — Tâches en arrière-plan avec progression et annulation [P0] — ◐ Partiel (2026-10-13)
- Constaté sur `main` (`b810cf0`) : `BusyIndicator::run` exécute segmentation et auto-numérisation dans un fil de travail pendant que l'interface continue de peindre.
- Reste : progression réelle, annulation (`std::stop_token`), instantané du projet et résultat appliqué par commande de façon générale ; test « un événement traité pendant une auto-numérisation ».
- À faire : infrastructure de tâches (thread de travail, instantané immuable
  du `Project` en entrée, résultat appliqué par commande sur le thread UI),
  progression et jeton d'annulation passés aux fonctions du cœur (sans Qt :
  callback / `std::stop_token`) ; migrer segmentation, auto-numérisation,
  génération, IA.
- Modules : `libs/core` (progress/cancel), `apps/desktop`.
- Acceptation : l'UI reste réactive (QTest : un événement traité pendant une
  auto-numérisation) ; annuler ne modifie pas le document ; déterminisme
  inchangé (même résultat en tâche de fond qu'en synchrone).

### HP-PERF-002 — Régénération incrémentale par objet [P0] — ◐ Partiel (2026-10-18)
- À faire : cache de la séquence générée par objet, clé = empreinte (paramètres
  + géométrie source + contexte de routage) ; modifier un objet ne régénère
  que lui et ses liaisons ; `effective_sequence` assemble les tranches.
  Respecter la garde structurelle `check_no_raw_sequence_bypass.cmake`.
- Modules : `libs/stitch_generation`.
- Acceptation : séquence octet pour octet identique avec et sans cache
  (test d'équivalence comme `test_skeleton_equivalence.cpp`) ; modification
  d'un objet sur `tentabrode` < 50 ms.
- Livré (2026-10-18) : cache à clé exacte de la séquence des objets droits, tatami et directionnels (`libs/stitch_generation/src/chunk_cache.*`, copie des paramètres et de la géométrie comparée par `operator==`, budget 64 Mio, mutex, éviction LRU) ; `generate_sequence` ne régénère que les objets modifiés. Tests `chunk cache : …` (équivalence octet pour octet avec/sans cache, invalidation paramètres/géométrie/identifiant, 1 fil contre 8). `tentabrode.png` Release : `effective_sequence` 63 → 7 ms (rien ne change), 63 → 8 ms (objet médian modifié), empreinte inchangée. Voir `docs/performance-audit.md` §9.
- Reste : le plus gros objet (fond) reste régénéré en entier (67 ms) — cache par région ; satins routés et auto-satin hors cache (entrée dépendante de l'objet précédent).

### HP-PERF-003 — Auto-numérisation plus rapide [P1] — ☑ Fait (2026-10-13, `ba54c07`)
- Livré (PR #26, fusionnée le 2026-10-13 (`ba54c07`)) : régions vectorisées et essayées en satin en parallèle, résultat réassemblé dans l'ordre. `tentabrode.png` : 77 ms (922 ms avec « Satin automatique »), cible < 5 s atteinte. Voir `docs/performance-audit.md` §7.
- État OpenStitch : 94,6 s → ~18 s (`docs/performance-audit.md`).
- À faire : profiler de nouveau, paralléliser par région (résultat réassemblé
  dans un ordre déterministe) ; cible < 5 s sur `tentabrode.png`.

### HP-PERF-004 — Rendu de scène incrémental [P1] — ☐ À faire
- Voir HP-VIEW-009 : ne plus reconstruire toute la scène dans `refreshImage()`.

### HP-PERF-005 — Parallélisation des générateurs [P1] — ☑ Fait (2026-10-13, constaté sur `main`, `65c6397`)
- Livré : `generate_sequence` génère remplissages, contours et squelettes d'auto-satin en parallèle (`parallel_for`), assemblage séquentiel dans l'ordre de couture ; résultat indépendant du nombre de fils. La PR #26 étend le principe à la segmentation, la vectorisation et l'auto-numérisation.
- À faire : génération des objets indépendants en parallèle, assemblage
  ordonné (déterminisme strict).

### HP-PERF-006 — Démarrage rapide [P2] — ☐ À faire
- À faire : mesurer le démarrage, charger paresseusement l'IA et les nuanciers.

### HP-PERF-007 — Mémoire sur gros projets [P2] — ☐ À faire
- À faire : mesurer un projet de 500 000 points et 2 000 objets ; plafonds.

### HP-PERF-008 — Suivi des performances dans le temps [P2] — ◐ Partiel
- État OpenStitch : `openstitch-bench` hors CTest.
- À faire : exécution périodique en CI (non bloquante), historique des mesures.

### HP-PERF-009 — Régénération différée à l'édition de paramètres [P1] — ◐ Partiel (2026-10-18, branche `ux-lot-c1-pr`)
- Problème : chaque pas d'un champ de l'inspecteur pousse une `SetStitchParamsCommand` (fusionnée en un pas d'annulation) puis régénère **sur le fil UI** (`main_window.cpp:6931-6948`) : un grand remplissage gèle l'interface à chaque pas de molette ou de flèche. Constat BRO-18 (UX-04, UX-11). Condition de la création directe (HP-UX-002).
- Comportement attendu : la commande est appliquée immédiatement ; la régénération est différée de 150 ms d'inactivité et exécutée hors fil UI (`BusyIndicator::run` ou la tâche de `HP-PERF-001`) ; pendant le calcul l'aperçu précédent reste affiché ; un résultat périmé est rejeté. Option : aperçu basse résolution pendant le glisser.
- Complexité : M. Composants : `apps/desktop/main_window.cpp`, `properties_panel.cpp`. **Aucun changement du moteur de points** : la sortie reste octet pour octet identique à la régénération synchrone.
- Dépend de : HP-PERF-001 (infrastructure de tâches), HP-PERF-002 (cache par objet, déjà ◐).
- Acceptation : cinq `valueChanged` en moins de 150 ms déclenchent une seule régénération ; un événement UI est traité pendant la régénération d'un gros objet ; séquence identique octet pour octet à la version synchrone ; annuler (Ctrl+Z) restaure paramètres et aperçu.
- Tests de non-régression : fusion de commandes scalaires, `check_no_raw_sequence_bypass`, déterminisme (`OPENSTITCH_THREADS=1`).

---

## 23. Robustesse et qualité logicielle (QA)
- Livré : `scheduleParamsRefresh()` (débounce 150 ms), régénération hors fil UI sur un instantané du projet (`std::async` + timer de scrutation ; `BusyIndicator::run` écarté car modal), résultat périmé rejeté par numéro de série (incrémenté à chaque édition et à chaque `refreshImage`), `flushPendingRefresh()` avant export, analyse, statistiques, simulation, image et fiche de production, éditions de groupe incluses. Tests : cinq éditions en moins de 150 ms = une régénération, séquence différée identique à `effective_sequence`, événement UI traité pendant le calcul, résultat périmé rejeté, undo restaure paramètres et aperçu. Reste : mesure de latence sur un très gros remplissage ; échec intermittent de `test_main_window` sous charge (watchdog 300 s) observé aussi sans ce lot.

### HP-QA-001 — Découper `main_window.cpp` [P1] — ☐ À faire
- État OpenStitch : `apps/desktop/main_window.cpp` ≈ 324 Ko (2026-10-06), un seul fichier
  pour menus, outils, modes d'édition, rendu, simulation. Chaque nouvelle
  fonctionnalité UI de cette liste l'alourdit et ralentit tout le monde.
- À faire : extraire par responsabilité (contrôleur de sélection, outils de
  dessin, modes d'édition satin/points/directionnel, rendu des points,
  simulation, fichiers) — comme a commencé `main_window_directional.cpp` ;
  aucun changement de comportement, tests QTest verts à chaque étape.
- Faire **tôt** : avant HP-OBJ-001 idéalement.

### HP-QA-002 — Journal et rapport de plantage [P1] — ☐ À faire
- État OpenStitch : `libs/core/src/log.cpp` n'a qu'un sink console (pas de journal fichier ni de
  minidump) ; la récupération après plantage existe côté autosave (HP-FILE-004), sans rapport.
- À faire : journal fichier (spdlog déjà présent) dans le dossier applicatif,
  minidump en cas de crash (Windows `MiniDumpWriteDump`), « envoyer le rapport »
  (lien vers un ticket prérempli, jamais d'envoi silencieux).

### HP-QA-003 — Fuzzing des décodeurs [P1] — ◐ Partiel (2026-10-18)
- À faire : cibles libFuzzer (clang-cl ou job Linux) pour DST, PES, JEF, EXP,
  SVG, DXF, `.osp` ; corpus minimal commité.
- Livré (2026-10-18) : `test_fuzz_decoders` : DST, PES, JEF, EXP, SVG, DXF et compagnon, graines fixes, toutes les troncatures, octets altérés ou aléatoires, en-têtes saturés, reproductibilité ; un vrai défaut corrigé : boucle infinie de l'import SVG sur un caractère non alphabétique après `Z` (`svg_import.cpp`), avec test de non-régression (`265e4b8`).
- Reste : cibles libFuzzer/ASan en CI ; `.osp` (`libs/project_io`).

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

### HP-QA-007 — Interface programmatique `.osp` en ligne de commande (pilotable par un agent) [P1] — ◐ Partiel (en PR #46)
- État OpenStitch : `openstitch-cli project …` (`libs/project_ops`, Qt-free) : `new`,
  `digitize` (image → `.osp` natif éditable), `inspect` (JSON stable), `apply` / `op` /
  `set` (lots d'opérations transactionnels, `--dry-run`, écriture atomique, codes d'erreur
  stables), `operations`, `check` (couverture par objet `stitch_analysis::object_coverage`,
  SVG de diagnostic), `export` (DST/PES/JEF/EXP/SVG, mêmes octets que `osp2dst`), `select`,
  `diff`, `validate`, `digitize-batch`, `commands --json`. 30 opérations : style, paramètres
  (dont guides, ruptures, dégradés, rails, barreaux), ordre, formes, transformations,
  fusion/découpe, auto-satin (vrai pipeline), import SVG, lettrage (police fournie par
  fichier). Documentation : `docs/source/cli-project.md`.
- Hatch : pas d'équivalent (Hatch n'est pas scriptable) ; différenciant d'OpenStitch.
- À faire : fusion de la PR #46 sur `main` ; polices intégrées accessibles sans fichier ;
  édition en ligne de commande des courbes de Bézier des guides/rails (aujourd'hui
  polylignes) ; couleurs du SVG reprises à l'import ; sélection par lot (`--where`) branchée
  directement dans `apply` ; essai sur machine réelle d'un projet produit par un agent.
- Modules : `libs/project_ops` (cœur), `apps/cli/project_cmd.cpp`, `libs/stitch_analysis`.
- Dépend de : —
- Acceptation : cycle image → `.osp` → inspection → lot → rechargement → DST testé sur
  des images du dépôt (`tests/unit/project_ops/test_workflow.cpp`, `tests/cli/`) ; un lot
  refusé laisse le fichier octet pour octet inchangé (SHA-256 vérifié) ; `project export`
  == `osp2dst` (SHA-256).

### HP-QA-008 — Garde de taille minimale et tests de parcours UX [P1] — ◐ Partiel (2026-10-18, branche `ux-lot-d2`)
- Problème : aucun test ne plafonne `minimumSizeHint()` de la fenêtre (tous docks ouverts) ni des dialogues ; l'audit UX du 2026-10-18 a **mesuré** 523 × 441 de minimum (docks par défaut) et un canevas de 360 × 421 à 1024 × 640 (`openstitch-ui-shots`), mais les tailles de dialogues restent des estimations (classe B). Plus de dix trous de couverture recensés (`docs/ux/audit-ux-ui-2026-10-18.md` §5).
- À faire : (1) mesurer les `minimumSizeHint` de tous les dialogues et les consigner ; (2) test « tous docks visibles → `window.minimumSizeHint() <= 911 × 480` » (objectif long terme 683 × 360) ; (3) `dialogsMinimumSizeFitsWorkArea` (≤ 880 × 440, aide ≤ 460 × 320) ; (4) tests ciblés sur les trous : `isClean`/heure d'enregistrement (UX-32), `setDefault` du dialogue d'export (UX-10), ordre de tabulation, `ThreadStrip`, `DocumentPanel` (repli, vecteurs), état mixte de groupe (UX-33/34), noms accessibles via `QAccessible`.
- Modules : `tests/unit/desktop/` ; éventuellement `tests/bench/ui_shots.cpp` pour imprimer les tailles des dialogues. Noms `TEST_CASE`/fonctions QTest en ASCII, headless, sans pixel ni `sleep`.
- Dépend de : — (préalable des lots B et C de HP-UX-017…044).
- Acceptation : les tests existent, échouent sur les valeurs actuelles qui dépassent le budget (rouge d'abord) puis passent après les tâches B ; aucun test ne fige un comportement jugé défaut (ex. Entrée = export).

---

## 24. Distribution et plateformes (DIST)

Aujourd'hui : un script Inno Setup (`packaging/windows/installer.iss`), un build
Windows MSVC, un workflow de release sur tag (`.github/workflows/release.yml`) et une
release « latest » publiée par la CI (`ci.yml`) ; installateur non signé.
- Livré : `test_ui_responsive` (21 cas) : plafonds de `minimumSizeHint` de la fenêtre tous docks ouverts et des dialogues, noms accessibles, palette sur deux colonnes, densité automatique ; mesures avant/après dans `docs/ux/mesures-dialogues.md` (rouge d'abord : 14 échecs sur 18, puis vert). Reste : `isClean`/heure (couvert par `savedIndicatorShowsTheSaveTimeNotTheRefreshTime`), ordre de tabulation, état mixte de groupe (couvert par HP-UX-038 quand fusionné) ; `setDefault` du dialogue d'export volontairement non figé ; Préférences IA (802 × 523) hors budget.

### HP-DIST-001 — Installateur Windows signé et publié [P0] — ◐ Partiel
- État OpenStitch (2026-10-06) : `.github/workflows/release.yml` construit, teste et publie
  l'installateur Inno sur un tag `vX.Y.Z` (notes générées) ; `ci.yml` publie une release
  « latest » (zip) ; `windeployqt` branché ; désinstallation propre. Manquent : signature de code,
  associations de fichiers, runtime MSVC, et une release taguée effectivement publiée.
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
| 2026-09-23 | Claude (session pilotée) | HP-FILE-002 ☑ : chemin `.osp` mémorisé (Ctrl+S réécrit sans dialogue, Ctrl+Maj+S = Enregistrer sous), nom du fichier dans le titre, cible oubliée à chaque changement de document ; écriture atomique déjà en place côté `project_io`. |
| 2026-10-05 | Claude (session pilotée) | HP-THR-001 ☑, HP-THR-003 ☑ (bibliothèque `thread_palette`), HP-FILE-003 ☑, HP-FILE-004 ☑ ; HP-THR-002 ◐ (nuanciers placeholder). |
| 2026-10-05 | Claude (session multi-agents) | Mode **Contours / Line Art** fusionné (PR #4) : `libs/autodigitize/contour_*`, CLI `digitize --mode contours`, dialogue desktop avec curseur de détail. |
| 2026-10-06 | Claude (revue d'ensemble) | **Audit complet des 256 entrées contre `main`.** Statuts : AUTO-005, VEC-002, VEC-009, OBJ-003, SEQ-001, ENG-002 ☐ → ◐. « État OpenStitch » corrigé : FMT-001 (PR #5), FMT-014, FMT-021, THR-004, AUTO-001/002/006/008/009/012, STI-001, STI-004, ENG-010/014/015, QA-001/002, DIST-001 et introductions des sections 19 et 24. Ajout du tableau de bord et de la règle « ☑ = sur `main` ». |
| 2026-10-06 | Claude (audit UI) | Audit du câblage et de l'ergonomie Qt : `docs/ui-audit-2026-10.md` (constats, tests d'invariants `test_ui_invariants`, plan multi-agents par lots). Aucun statut HP-UX modifié ; HP-UX-006/009/010/013/015 sont confirmés ouverts. |
| 2026-10-10 | Claude (palettes de fils) | Fils branchés au document et à l'interface : HP-THR-004 ☑ (`EmbroideryObject::thread`, `SetObjectThreadCommand`, `.osp` v6), HP-THR-005 ◐ et HP-OBJ-018 ◐ (panneau **Fils** : projet, catalogues, film couleur), HP-THR-011 ◐ (« Limiter à N fils »), HP-THR-002 reste ◐ (nuancier Générique libre + import CSV/JSON, aucune carte de marque redistribuée). API Qt-free `stitch_analysis::thread_usage` pour la fiche de production. |
| 2026-10-13 | Claude (session pilotée) | **Revue de la vague 1 contre `main`** : OBJ-001, OBJ-008, OBJ-016, VIEW-001, PERF-001 ☐ → ◐ (déjà en grande partie sur `main`, statuts en retard) ; PERF-005 ☑ (génération parallèle). FMT-002…005 et THR-002 marqués « attend (humain) ». |
| 2026-10-13 | Claude (session pilotée) | **Manipulation d'objets** (en PR) : OBJ-002 copier/couper/coller, OBJ-004 rotation, OBJ-006 miroir, OBJ-012 dupliquer avec broderie, OBJ-014 design entier, OBJ-018 couleur libre, OBJ-001 tout sélectionner / même couleur ; HOOP-001/005/006 ◐ (PR #25) ; PERF-003 ◐ (PR #26) ; STI-011 corrigé. Tableau de bord recalculé. |
| 2026-10-13 | Claude (session pilotée) | **Second lot** (en PR) : OBJ-018 pastille de couleur dans l'inspecteur, OBJ-004 poignée de rotation sur le canevas, VEC-002 tracé ouvert, VEC-003 Exclure, VEC-005 trous. Constaté sur `main` : OBJ-003 ☑, VEC-004 ☑ (statuts en retard). Tableau recalculé. |
| 2026-10-13 | Claude (session pilotée) | PR #25, #26 et #27 fusionnées : mentions « en PR » retirées du premier lot ; OBJ-014 ☑ et PERF-003 ☑ (critères remplis sur `main`). |
| 2026-10-15 | Claude (session CLI agent) | **HP-QA-007** ajoutée (◐, PR #46) : interface programmatique `.osp` en ligne de commande (`project …`, lots transactionnels, import SVG, lettrage, export, contrôle qualité). Total 264 → 265. |
| 2026-10-18 | Claude (session autonome) | HP-PERF-002 ◐ : cache de régénération par objet (`chunk_cache`), `effective_sequence` 63 → 7/8 ms sur `tentabrode` ; reste le gros objet unique et les satins routés. |
| 2026-10-18 | Claude (session autonome) | HP-FMT-011 ◐ : `stitch_render::render_sequence` + commande `openstitch-cli render` (PNG réaliste, fond transparent, résolution choisie) ; reste JPEG et dialogue du bureau. |
| 2026-10-18 | Claude (session autonome, agents) | HP-VEC-007/008/010/014 ◐ : cœur géométrie (`smooth_path`, `convert_to_curves`, mesure, six formes prédéfinies) ; commandes et outils du bureau restent à câbler. |
| 2026-10-18 | Claude (session autonome, agents) | HP-VIEW-005 ◐, HP-PROD-005 ◐, HP-PROD-007 ◐ : `density_map`, `thread_consumption` et quatre règles d'analyse dans `stitch_analysis` ; affichage à faire. |
| 2026-10-18 | Claude (session autonome, agents) | HP-FMT-021 ◐ (migrations versionnées v1→v7), HP-THR-007 ◐ (stock de fils), HP-THR-012 ◐ (pipette) : cœur livré, IHM à faire. |
| 2026-10-18 | Claude (session autonome, agents) | HP-TXT-007 ◐ (crénage manuel par paire), HP-TXT-012 ◐ (Unicode sans HarfBuzz : accents, repli, RTL hébreu), HP-TXT-013 ◐ (densité et points des petites lettres) ; champs `TextObject` et inspecteur restent à faire. |
| 2026-10-18 | Claude (session autonome, agents) | HP-FMT-009 ◐ et HP-FMT-005 ◐ (compagnon de couleurs `.inf`/`.csv`, codec), HP-QA-003 ◐ (fuzzing déterministe des décodeurs, boucle infinie SVG corrigée) ; câblage bureau/CLI à faire. |
| 2026-10-18 | Claude (session autonome, agents) | Câblage bureau (5 lots, agents) : HP-VEC-007/008/010/014 ☑ (menu Forme, outil Règle), HP-FMT-011 ☑ (Exporter l'image), HP-FMT-009 ☑ et HP-FMT-005 ◐ (compagnon de couleurs à l'export et à l'import, `osp2dst --color-file`), HP-PROD-005 ☑ (consommation sur la fiche), HP-THR-007 ☑ (Mes fils), HP-TXT-007 ☑ (crénage manuel, `.osp` v8 conditionnel), HP-TXT-012 ◐ (ligatures), HP-VIEW-005 ◐ (carte de densité), HP-PROD-007 ◐ (règles optionnelles), HP-THR-012 ◐ (outil Pipette). |
| 2026-10-18 | Claude (session autonome, agents) | Règle ajoutée : sauf demande explicite, toute fonctionnalité est câblée dans le bureau (hook, skills, `CLAUDE.md`). |
| 2026-10-18 | Claude (audit UX/UI, 4 analyses en lecture seule) | **Audit UX/UI** : `docs/ux/` (rapport, parcours, stratégie, synthèse). +30 entrées ☐ : HP-UX-017…044 (lots A à E, §20), HP-PERF-009 (§22), HP-QA-008 (§23) ; HP-UX-001/002/003/004/009/012/016 précisés ; les constats `UX-xx` de `docs/ui-ux-audit-2026-10-13.md` sont enfin reliés à des tâches. Décisions : verrou d'édition = mode Isoler de session (pas de champ `.osp`), Ctrl+O universel, dialogues de création supprimés (option « Créer avec réglages… »). Total 266 → 296. Aucun code modifié. |
| 2026-10-18 | Claude (lot A, quick wins) | HP-UX-017, 018, 021, 022 ◐ (branche `claude/ux-lot-a-quickwins`, PR empilée sur #71) : Ctrl+O universel, Échap désélectionne les objets, F7 sans modale (Maj+F7 règle), sortie de simulation. Quatre tests QTest ajoutés + deux tests existants adaptés (mnémonique, fin de curseur) ; suite desktop Release 37/37. Total ◐ 91 → 95, ☐ 180 → 176. |
| 2026-10-18 | Claude (lot A2, sélection) | HP-UX-019, 020, 023 ◐ (branche `ux-lot-a2`) : prédicat de sélection unique avec tolérance de clic en repli, cyclage au re-clic, Ctrl+A selon la couche, touches 1 à 4, zoom sur la sélection. Tests ajoutés dans `test_selection_hit_test` et `test_ux_global`. Total ◐ 95 → 98, ☐ 176 → 173. |
| 2026-10-18 | Claude (lot D, partie 1) | HP-UX-034, 036, 039 ◐ (branche `ux-lot-d2`) : mode Isoler de session, sauts de couleur et « fil k/m » en simulation, message de couche masquée, heure d'enregistrement fixe. Tests dans `test_ux_global`. Total ◐ 98 → 101, ☐ 173 → 170. |
| 2026-10-18 | Claude (lots B, C/E et liste Document, 4 agents) | HP-QA-008, HP-UX-024, 025, 026, 027, 030, 031, 032, 033, 037, 041, 042, 043, 044 ◐ (branche `ux-lot-d2`) : responsive (fenêtre 1291 × 1443 → 701 × 373 de minimum, dialogues bornés, mode portable), numérisation à préréglages, export rapide, dock Qualité, barre Fils au clavier, liste Document étendue. Tests : `test_ui_responsive`, `test_workflow_ux`, `test_thread_strip`, `test_document_panel`. Total ◐ 101 → 115, ☐ 170 → 156. |
| 2026-10-18 | Claude (lot C édition, agent) | HP-UX-002, 028, 029, 038 et HP-PERF-009 ◐ (branche `ux-lot-c1-pr`) : création directe sans modale, quatre boutons de type pour 1 ou N formes, bandeau de retouches, valeurs mixtes, régénération différée hors fil UI. Tests : `test_creation_flow` (19 cas). Total ◐ 115 → 120, ☐ 156 → 151. |
| 2026-10-18 | Claude (HP-VEC-015, comblement de bloc) | HP-VEC-015 ◐ (branche `claude/embroidery-block-hole-fill-7c00ac`) : vides d'un bloc détectés depuis les points cousus, rattachés au plus grand remplissage voisin ; cœur, op CLI `fill_block_holes`, menu Forme et tests. |
| 2026-10-18 | Claude (lot D, partie 2) | HP-UX-035 ◐ (copier/coller le style). **HP-UX-040 (contrat IA) retiré** à la demande du mainteneur (l'IA n'est pas une priorité) ; total 296 → 295, ◐ 120 → 121, ☐ 151 → 149. |
| 2026-10-18 | Claude (audit coupes remplissage) | HP-SEQ-006 ◐ : liaisons de rangées du tatami tolérantes au bord (0,15 mm) ; `Possible.osp` sauts 2 862 → 1 525, coupes 928 → 792 ; manques consignés dans l'entrée (retour par le fil cousu, directionnel, satins auto, chaînage des entrées). |
