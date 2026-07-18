# Phase 0 — Stratégie d'encodage et de décodage DST (Tajima)

Le format DST est documenté publiquement (documentation communautaire, EduTechWiki, code source zlib de libembroidery servant de référence croisée). Aucune ingénierie inverse n'est nécessaire. Module : `libs/formats/dst/`, indépendant de la génération de points (§17).

## 1. Rappel du format (ce que nous implémentons)

### 1.1 En-tête — 512 octets ASCII

Champs terminés par CR, complété par des espaces (0x20) jusqu'à 512 :

```
LA:<nom 16 car.>   ST:<nb points, 7 chiffres>   CO:<nb changements couleur, 3>
+X:<étendue>  -X:  +Y:  -Y:      (en unités 0,1 mm depuis l'origine)
AX:<position finale ±x>  AY:     (retour au point de départ)
MX: MY: PD:                      (multi-motif : zéros/valeurs par défaut)
```

### 1.2 Corps — enregistrements de 3 octets

Chaque enregistrement encode un déplacement (dx, dy) ∈ [−121, +121] en unités de 0,1 mm, par bits de valeurs ±1/±3/±9/±27/±81 répartis sur les 3 octets, plus des bits de type dans l'octet 3 :

- point normal : bits 7–6 de l'octet 3 = `0b00…` (les 2 bits bas fixés `11`) ;
- **jump** : bit 7 octet 3 ;
- **color change** : bits 7+6 octet 3 (la machine s'arrête ; le DST ne stocke *pas* les couleurs — seulement les arrêts) ;
- fin de fichier : `0x00 0x00 0xF3`.

### 1.3 Limitations documentées (docs/formats/dst.md, à rédiger avec le code)

- Pas de couleurs réelles (uniquement des arrêts « changement de fil ») → l'ordre de la palette doit être communiqué autrement (notre fichier projet la conserve ; on peut générer un fichier d'accompagnement texte listant les fils).
- Pas de Trim explicite standardisé (convention : séquence de jumps courts déclenche la coupe sur beaucoup de machines ; nous émettons N jumps configurables pour un Trim logique et le documentons).
- Déplacement max ±12,1 mm par enregistrement → subdivision automatique en jumps intermédiaires.
- Résolution 0,1 mm ; aucune notion d'objet, de courbe, de paramètre. **Un DST ne conserve pas les objets éditables** — message explicite dans l'UI d'export.

## 2. Stratégie d'encodage

Entrée : `StitchSequence` (positions absolues en µm). Sortie : octets DST.

1. **Validation préalable** (via `stitch_analysis`) : séquence non vide, bornes < limites du format (±121 × N raisonnable), pas de NaN possible par construction (entiers).
2. **Quantification** : conversion µm → unités 0,1 mm en travaillant sur les **positions absolues quantifiées** puis en dérivant les deltas : `delta_i = round(pos_i/100) − round(pos_{i−1}/100)`. Garantit une erreur bornée à ±50 µm **sans dérive cumulative** (l'encodage naïf des deltas arrondis accumule l'erreur).
3. **Subdivision** : tout delta > 121 unités est découpé en jumps intermédiaires ; un Trim logique devient N jumps (paramètre, défaut 3) ; ColorChange → enregistrement dédié ; Stop → color change sans changement de fil documenté.
4. **En-tête calculé après le corps** : ST/CO/±X/±Y/AX/AY dérivés de la séquence encodée réelle (pas de la séquence source) — cohérence garantie.
5. **Déterminisme** : même séquence d'entrée → mêmes octets, exigence des tests golden.

## 3. Stratégie de décodage

1. Lire l'en-tête, le **tolérer laxiste** (fichiers du terrain souvent non conformes) : seuls ST/CO sont exploités à titre indicatif, la vérité est le corps.
2. Décoder enregistrement par enregistrement ; tout octet de fin manquant ou enregistrement invalide → erreur utilisateur claire (« fichier DST tronqué à l'octet N »), jamais de crash (fuzzing prévu, §24).
3. Produire une `StitchSequence` (positions absolues reconstruites) + un objet `ManualStitches` par bloc de couleur dans le document.
4. Comparer les bornes recalculées à l'en-tête : divergence → avertissement non bloquant.

## 4. Tests spécifiques au codec

- **Aller-retour** : séquence → encode → decode → séquence' ; assert : mêmes types de commandes, positions égales à ±50 µm, mêmes compteurs.
- **Golden** : petits motifs de référence (carré, ligne, deux couleurs, saut long subdivisé) avec les octets attendus archivés.
- **Bornes** : delta exactement ±121, ±122 (subdivision), motif vide (refusé), motif d'un seul point.
- **Croisé** : nos fichiers relus par un outil tiers (libembroidery CLI) en test manuel documenté, pas en CI (pas de dépendance).
- **Fuzzing** (post-MVP) : libFuzzer sur le décodeur, corpus = golden + fichiers tronqués/corrompus.
