# PR #336 — Inventaire de parité des fondus de broderie

Date : 13 novembre 2026. État de la branche candidate, **pas** une validation textile.

## Important : ne pas confondre variantes QA et types de fondu

Les **16 scénarios CLI** de `scripts/qa_pr336_gradients.py` couvrent des combinaisons du nombre
de couleurs (2/3/5/8), texture, champ de transition et satin. Ce ne sont **pas 16 algorithmes
indépendants**. Une variante est « implémentée » seulement si l'utilisateur peut la créer dans
Studio, si elle génère des points réels et si la CI teste ses fichiers OSP / DST / preview.

Trois dimensions distinctes doivent rester explicites dans l'interface :

1. **Type d'effet** : dégradé de densité d'un fil / mélange de plusieurs fils / satin multicolore.
2. **Algorithme de mélange** : densités complémentaires, surcouches, bords feather, tramage,
   passé empiétant, etc.
3. **Trajectoire géométrique** : axe droit, rayon, courbe / guide peint ; et **profil de
   répartition** : rampe, lissage, S, réglage par couche.

## État vérifié du code

| Famille / référence | Présence dans PR #336 | Lacune ou réserve concrète |
|---|---|---|
| Densité seule, un fil (Hatch Gradient Fill, PE-DESIGN Gradation) | Moteur `DensityGradient` existant | Le dégradé mono et le fondu de fils restent non combinables sur une même forme |
| Mélange complémentaire 2–8 fils (Hatch Multi Blend) | Oui, groupe logique, vraies couches | Couverture textile à vérifier ; nombre limité à 8 fils |
| Fond constant + voiles de couleur | Oui, `complementary=false` | Taux de surcouche fixe, non réglable fil par fil |
| Lisse | Oui | Profil commun, mais les réglages avancés restent limités |
| Feather edge | **Approximatif** (`DensityTexture::Feathered`) | Pas de vrais contrôles gauche/droite, largeur maximale, irrégularité et forme du feathering de Hatch |
| Points dispersés / tramage | **Approximatif** (`Stippled`) | Pas de masque peint, grille de bruit ou grain librement contrôlé |
| Long-and-short / passé empiétant | **Partiel** (`LongShort`) | Pas d'orientation locale par fil ni de guide propre à chaque couleur |
| Géométrie linéaire | Oui | Ne garantit pas l'alignement des mêmes rangées de piqûres entre couleurs au niveau de l'outil Ink/Stitch « Linear Gradient Fill » |
| Géométrie radiale | Oui (distance au centre) | Différent d'un moteur de **points tournants** « Radial Fill » ; ce n'est que le champ de progression du fondu |
| Géométrie courbe | Partiel (champ parabolique `curve_bend`) | Pas de guide Florentine/InkStitch arbitraire dessiné dans l'éditeur de fondu |
| Satin multicolore | Oui (répartition déterministe de traversées) | Pas de vrai mélange de profils de densité par rail / interpolation continue des traversées pour chaque fil |
| Profils de transition | 9 profils monotones en cours d'intégration | Ce ne sont pas des copies à l'identique des huit profils de Hatch v4 ; vérifier formules, limites et UX |
| Profil **indépendant par fil** (Hatch Multi Blend) | **Première implémentation candidate ajoutée** : `layer_curves` + sélecteurs Studio, OSP, undo | Ne gère pas encore l'écartement propre à chaque fil, ni les courbes de densité personnalisées ; CI à confirmer |
| Position **modifiable** de chaque arrêt de couleur | **Absent** | Les arrêts sont actuellement répartis régulièrement `i/(N-1)` dans le moteur |
| Épaisseur / densité propre à chaque fil | **Absent** | Les poids sont des rampes triangulaires fixes ; `0.30` pour les surcouches non complémentaires |
| Grilles de densité / motifs personnalisés (PE-DESIGN) | **Absent** | Pas d'éditeur de motifs de gradation ni de sélection des motifs préréglés |
| Ink/Stitch « Linear Gradient Fill » à rangées communes | **Non équivalent** | Notre moteur crée N couches directionnelles avec leurs propres tracés ; ne prétendre aucune parité sans comparatif de placement des points |
| Ink/Stitch « Convert to Gradient Blocks » | **Absent comme outil dédié** | Pas de découpe d'un remplissage en blocs éditables depuis un dégradé |
| Mélange de densité mono + plusieurs fils | **Bloqué explicitement** | `gradient_incompatibility` refuse la coexistence pour ne pas perdre le gradient d'origine |
| Sélection de vrais fils de nuancier | **Absent** | Les nouveaux fils sont des couleurs RGB libres, pas des `ThreadKey` réels |
| Masque spatial peint / transitions locales | **Absent** | Le moteur accepte un axe et une texture, pas un champ de mélange édité à la main |

## Travaux nécessaires à la parité réelle

**P0 — fonctionnalité structurelle et intégrité** :

1. Compléter et vérifier les **profils par couche** (courbe, écart des rangées, intensité/seed,
   limite de recouvrement) avec sauvegarde OSP, undo/redo, édition du nombre de couleurs.
2. Stocker les **positions manuelles de chaque couleur** comme paramètres de groupe stables,
   au lieu des arrêts uniformes `i/(N-1)`. Valider ordre, coïncidences et cas 2..8.
3. Créer une véritable composition des **gradients de densité mono et multi-fils** plutôt
   que réutiliser destructivement le seul `DirectionalFillParams::density_gradient`.
4. Ajouter une comparaison de la **couverture totale**, des mailles et des sauts lors de
   changements de profils individuels : ne pas supposer la somme des densités égale à 1.

**P1 — familles de traitement manquantes** :

5. Implémenter le **gradient à rangées partagées** (à l'instar d'Ink/Stitch Linear Gradient
   Fill), qui conserve les mêmes positions de piqûre entre couleurs.
6. Implémenter le **gradient par blocs éditables** (type Convert to Gradient Blocks).
7. Compléter les contrôles de **feathering** (côté, largeur, irrégularité) et les vrais
   paramètres de tramage.
8. Ajouter la géométrie **Florentine / ligne-guide personnalisée**, indépendamment du
   remplissage directionnel qui existe déjà.
9. Ajouter les **motifs de gradation PE-DESIGN** ; sans reproduire les noms ou réglages
   propriétaires non documentés, assurer des résultats différenciés et testables.
10. Étendre le **satin multicolore** avec profils dédiés sans empiler les sous-couches.

**P2 — ergonomie, nuancier, qualité** :

11. Associer chaque couleur aux **références de fils réels** disponibles ; permettre le
    remplacement, le réordonnancement et l'ajustement individualisé des couches.
12. Construire une galerie d'aperçus **par vraie famille** (pas par seul nombre de fils).
13. QA : tests 2/3/5/8 fils, comparatif OSP/DST/PNG, couleurs non vides, profils
    différents et reproductibles, bornes points <=7 mm, temps CPU/mémoire sur grand motif.
14. Valider physiquement au moins trois tissus / densités et documenter limites.

**Définition de terminé** : un type ne doit pas être marqué « intégré » parce qu'une
étiquette QML existe. Il doit avoir un algorithme distinct, être accessible, produire un
fichier machine correct, et réussir les tests moteur/UI/CI correspondants. Le rendu simulé
ne remplace pas l'essai textile.

## Références officielles (revérifiées le 13/11/2026)

- Hatch v4 — *Gradient fill effects*: https://hatch.embroideryhelp.net/v4/en/OnlineHelp/New_Release/rn_-_release-1/Gradient_fill_effects_IMPROVED.htm
- Hatch v4 — *Create stitch blends*: https://hatch.embroideryhelp.net/v4/en/OnlineHelp/Editing/edit_advanced/Create_stitch_blends.htm
- Hatch — *Gradient fill settings*: https://hatch.embroideryhelp.net/v4/en/OnlineHelp/Stitches/stitch_effects/Gradient_fill_settings.htm
- Brother PE-DESIGN 11 — *Creating a gradation*: https://support.brother.com/g/s/hf/htmldoc/ped/im/ped11/en/PED11_EN/pages/10_1235768.html
- Ink/Stitch — *Color blending*: https://inkstitch.org/tutorials/color-blending/
- Ink/Stitch — *Stitch library*: https://inkstitch.org/docs/stitch-library/
