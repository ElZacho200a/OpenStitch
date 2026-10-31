# Veille scientifique — compensation du tirage prédite

Date : 2026-10-28 — contexte : PR #170, `PullMode::Predicted`.

## 1. Résultat important

Les publications donnent des **variables physiques**, des mesures de distorsion,
des tables de compensation conseillée et même des régressions sur les
propriétés du textile brodé. Elles ne donnent **pas** une table universelle
`FabricResponse(gain, poisson, sigma_along, sigma_across)` directement
transférable dans OpenStitch. Les différentes grandeurs ne doivent pas être
confondues.

En particulier :
- `compensation_strength` (actuel : 3x, borné à 6x) est un facteur **empirique
  de commande**, non une propriété matérielle mesurée.
- `uncalibrated_confidence` décrit l'incertitude de la prédiction ; elle ne
  doit pas être modifiée pour obtenir artificiellement un déplacement plus grand.
- La déformation physique doit être mesurée *après* broderie, alors que
  `pull_compensation` est la surpiqûre prescrite *avant* broderie.

## 2. Données quantitatives réutilisables

### 2.1 Tables professionnelles : réglages initiaux, pas « constantes physiques »

Source : Wilcom EmbroideryStudio 28, « Apply automatic pull compensation »
https://docs.wilcom.com/embroiderystudio/28/en/OnlineHelp/Quality/stabilizing/stabilizing-17.htm

| Application | Surpiqûre indicative |
| --- | ---: |
| Coton tissé, drill | 0,20 mm |
| T-shirt | 0,35 mm |
| Polaire, sweat | 0,40 mm |
| Lettrage | 0,20–0,30 mm |

Wilcom précise que la compensation varie aussi selon le cerclage, le support,
la largeur de colonne et le stabilisateur. Le réglage est fait par objet
et peut différer de chaque côté (dessus d'un remplissage vs tissu apparent).
NE PAS convertir la table « 0,35 mm pour jersey » en `gain=0.035` :
une surpiqûre n'est pas une déformation relative.

La documentation Wilcom « About auto fabrics » explique que la compensation,
la densité de points, les sous-couches et les longueurs doivent varier ensemble.
https://docs.wilcom.com/embroiderystudio/28/en/OnlineHelp/Digitizing/properties/properties-9.htm

### 2.2 Déformations observées sur différents supports

Radavičienė et al. (2014), *Analysis of Shape Nonconformity between
Embroidered Element*, Materials Science 20(1), DOI 10.5755/j01.ms.20.1.2911 :
https://ortus.rtu.lv/science/en/publications/18155

- Trois tissus tissés, directions différentes, motifs ronds : écarts entre
  géométrie numérisée et réalisée ; la direction des points compte.
- Dutta & Chatterjee (2020) citent des défauts géométriques de
  0,7–4,6 % dans la direction des points pour les configurations étudiées.
  **Ce n'est pas un coefficient applicable à tous les supports.**

Juchnevičienė et al. (2017), *The Research on the Width of the Closed-Circuit
Square-shaped Embroidery Element*, DOI 10.5755/j01.ms.23.2.16095 :
https://matsc.ktu.lt/index.php/MatSc/article/view/16095

- Trois tissus 65 % polyester / 35 % coton, carré 60 × 60 mm, trait large
  de 6 mm.
- Les erreurs changent selon l'orientation chaîne/trame et la localisation
  sur l'objet ; les coins ne se comportent pas comme les milieux de bord.
  Cela justifie un modèle spatial/local plutôt qu'un scalaire unique.

Daukantienė & Mikelionytė (2020), *Investigation of the Influence of
Technology Parameters and Thread Type on Embroidered Textile Element Quality*,
AUTEX Research Journal 20(4), DOI 10.2478/aut-2019-0046 :
https://www.degruyterbrill.com/document/doi/10.2478/aut-2019-0046/html?lang=en

- Influence mesurée du type de remplissage (satin/tatami), de la densité et
  du type de fil sur les largeurs et longueurs réellement brodées.
- Conditions expérimentales : tatami à 0,30/0,40/0,45 mm d'espacement,
  satin à densités relatives 70/80/90 %, longueur de tatami 4,2 mm.
- Les auteurs décrivent un protocole photo étalonné avec règle pour
  mesurer `largeur_reelle - largeur_nominale` et les longueurs.

### 2.3 Modèle publié de rigidité (ne prédit PAS le tirage en mm)

Dutta & Chatterjee (2020), *Development of prediction model through linear
multiple regression for the prediction of longitudinal stiffness of embroidered
fabric*, Fashion and Textiles 7:36, DOI 10.1186/s40691-020-00225-6 :
https://link.springer.com/article/10.1186/s40691-020-00225-6

49 échantillons initiaux et 25 pour validation. Modèle de rigidité en
direction chaîne, en dyne·cm :

```text
E_wp = 0.872 * BF_wp + 0.032 * P + 0.248 * D + 6.967 * S
       - 6.176 * L - 2.246 * theta_wp - 79.248
```

- `BF_wp` : rigidité en flexion du tissu initial, dyne·cm
- `P` : force de rupture du fil, gramme-force
- `D` : titre du fil, denier
- `S` : densité, points/cm²
- `L` : longueur moyenne, mm
- `theta_wp` : angle moyen des points par rapport à la chaîne, degrés

**Ne pas utiliser `E_wp` comme un déplacement géométrique** : dimensions
physiques et variable prédite différentes. Ce modèle indique toutefois
quelles caractéristiques mesurer, et fournit un candidat pour un futur
module de rigidité.

## 3. Cartographie des variables pour OpenStitch

| Variable | Source | Disponibilité actuelle | Intégration proposée |
| --- | --- | --- | --- |
| Matière (tissé/jersey/polaire/feutre) | projet | `project.fabric.material` (texte libre) | Profils par catégorie, explicitement non calibrés |
| Stabilisateur (nature, couches, g/m²) | projet + opérateur | `project.fabric.stabilizer` (texte libre) | Profil structuré + niveau de maintien |
| Densité locale, longueur, direction | points | `StitchSequence` | Calculer en grille spatiale, par passe, avec estimation de couverture |
| Type de point et sous-couches | objet + points | paramètres et `StitchPass` | Coefficients distincts satin/tatami et sous-couche |
| Chaîne/trame, anisotropie, rigidité | fiche tissu ou mesure | angle/grain seulement dans `FabricResponse` non persisté | Profil mesuré (angle, module ou proxy de rigidité) |
| Fil : composition, titre, ténacité | palette + fiche technique | palette mais propriétés mécaniques non normalisées | Enrichir seulement les fils documentés |
| Tension fil supérieur/canette, vitesse, cerclage | machine/opérateur | absent des paramètres de tirage | Métadonnées de calibration, pas de valeurs fictives |
| Effet de voisinage et ordre de couture | séquence | champ des segments | Pondérer par nombre de pénétrations, nature des passes et historique |

## 4. Plan d'intégration recommandé

1. **Banc de mesure déterministe** : satin de plusieurs largeurs et densités,
   rectangles tatami, cercles, contours voisins, directions chaîne/trame.
   Sauvegarder .osp/.dst, largeur cible, largeur finale, rétrécissement
   en mm, matériau, stabilisateur, fil, tension, nombre de couches.
2. **Profils initiaux issus des recommandations professionnelles** :
   utiliser la table Wilcom comme *indication visuelle/borne de départ* en
   mm, pas comme coefficients mécaniques présumés. Afficher clairement
   « non calibré ».
3. **Champ spatial amélioré** : accumuler les densités de pénétration,
   distinguer satin/tatami/sous-couche et appliquer des réponses locales
   orientées par l'angle de couture ; surveiller l'effet aux coins.
4. **Calibrage automatique** : ajuster `gain`, portées et anisotropie par
   optimisation de l'erreur entre contour prédit et contour réellement
   brodé. Séparer données d'entraînement et de validation.
5. **Inspection agent/UI** : exposer sur desktop et CLI/MCP la correction
   effective avant/après, maxima, zones de risque et origine des
   coefficients (`preset`, `published`, `measured`).
6. **Sécurité** : plafond par mouvement, anti-croisement aux trous,
   vérification de taille des points après compensation, déterminisme.
   Attention au cumul avec la compensation de base `Adaptive`.

## 5. Intégration effective dans la PR #170 (2026-10-28)

- `predicted_fabric_response` consomme les valeurs `project.fabric.material` et
  `project.fabric.stabilizer`, déjà éditables et persistées via « Coloris et tissu ».
  Les profils ne sont **pas** une transposition directe des nombres Wilcom :
  woven/neutre 1,00 ; jersey 1,20 ; terry 1,25 ; felt 0,75 ; denim 0,85
  (facteurs appliqués au `gain` du modèle), puis cut-away 0,80,
  tear-away 0,92, aucun entoilage 1,10. Ces valeurs sont entièrement
  **exploratoires**, sans calibration externe. Toute matière inconnue est neutre.
- Les sources de traction de `Predicted` prennent en compte les sous-couches
  (poids 0,55), les colonnes satin (facteur 1,15), et la longueur locale
  des segments (facteur borné 0,8–1,2). La densité se répercute par le nombre
  de segments et leur recouvrement ; les orientations par les vecteurs locaux.
- Les profils déjà marqués `calibrated` ne sont jamais modifiés par ces règles.
  Les poids et profils n'affectent aucun objet non prédit.
- L'aperçu standard du desktop lit le profil du projet, tandis que les
  presets de simulation « lin lâche » et « bien stabilisé » restent distincts.

La PR #170 améliore le comportement prédit et préserve Adaptive, mais
ne constitue pas encore un modèle textile calibré. Restent notamment
à mesurer le titre et la tension du fil, l'anisotropie des tissus, la
stabilisation réelle et l'effet du cerclage.
