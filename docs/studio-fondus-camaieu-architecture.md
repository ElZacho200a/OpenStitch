# Studio — Fondus de fils, camaïeux et dégradés de densité
Date : 12 novembre 2026 — implémentation candidate dans la PR #336 (CI et validation textile non acquises).

## Mise à jour 2026-11-14 — fondu multicolore devenu un type autonome (PR #341)

L'« Architecture cible » décrite plus bas (section *Architecture cible / évolutions pour 3+ fils
et camaïeux*) est en grande partie **réalisée** par la PR #341 (branche
`feat/studio-gradient-as-stitch-type`), avec une différence de vocabulaire : ce que ce document
appelle `GradientBlendGroup` est implémenté comme une **ancre** (`document::MulticolorGradientParams`,
un `EmbroideryObject` réel sans point propre, exclu de la génération) plutôt qu'un objet de
métadonnées séparé. Les couches restent des `EmbroideryObject` pour la couture et l'export, mais
Studio les masque de l'arbre d'objets et redirige tout clic sur une couche vers l'ancre logique. Le
cadre de sélection de l'ancre agrège les bornes de toutes les couches : aucune couche interne n'est
donc éditable indépendamment depuis le desktop.

Réalisé : type `MulticolorGradientParams` autonome dans `StitchParams` (`libs/document`), nouveau
schéma OSP avec refus explicite d'un groupe `multiBlend` ancien sans ancre (`libs/project_io`), API
`make_create/edit/remove_multicolor_gradient_command` à méthode EXPLICITE (`libs/project_ops`,
`GradientMethod` : Directional, SatinInterlaced, LongAndShort, ControlledScatter, CrossHatching —
les quatre premières réutilisent les générateurs de densité complémentaire de la PR #336,
CrossHatching est nouveau), dérivation automatique des rails satin entrelacés depuis le contour
quand aucun n'existe (`stitch_generation::rails_from_contour`), et une vraie entrée dans le
sélecteur de type de Studio (`inspector_model.cpp`/`Inspector.qml`, remplace l'adaptateur virtuel
transitoire de la PR #341 initiale).

Le panneau partagé `FadeProperties.qml` expose désormais les cinq méthodes en premier, puis la
palette, la géométrie, le mélange et les réglages propres à la méthode : densité du satin
entrelacé, nombre de directions et angle initial des hachures croisées. Le service desktop relit et
édite la spec autonome complète ; l'angle de transition reste modifiable pour les méthodes non
radiales. Le cas de la PR #340 est absorbé par une correction plus complète : un satin sans rails
manuels est réellement créé grâce aux rails dérivés du contour, au lieu de seulement afficher un
diagnostic plus précis.

Non fait (sections encore valables ci-dessous) : édition manuelle des rails satin entrelacés depuis
l'UI ; assistant de camaïeu basé sur une bibliothèque de fils réels (toujours couleurs libres) ;
mesures et recette textile.

## État de la branche PR #336

La branche implémente désormais **un moteur n-aire candidat** (2 à 8 fils) pour
les remplissages Tatami/directionnels, avec deux familles de couverture
(complementaire / fond constant + surcouches légères), et pour les colonnes
**satin** avec répartition déterministe de leurs traversées. Les méthodes
linéaire / radiale / courbe, bords irréguliers, tramage et passé empiétant
sont des options de calcul pour les remplissages, pas des filtres RGB.

- Source de vérité : un `MultiBlendLink{group,index,count,complementary}`
  enregistré dans chaque objet physique ; la fonction
  `find_multi_blend` vérifie la complétude de la relation.
- Création, modification du nombre de fils, suppression, migration des paires
  legacy : **une commande composite undoable**, avec un rang de couture par
  couche. Le moteur de points consomme des `EmbroideryObject` réels.
- Profil couleur du remplissage : poids linéaires par morceaux
  `DensityGradient.density_stops` ; en complémentaire la somme des
  poids est égale à 1 à chaque arrêt et entre les arrêts.
- Mode satin : sous-couche du premier fil seulement ; sélection de
  traversées complètes dans chaque bloc par hachage spatial stable pour
  limiter les superpositions, même avec points de division.
- Le format OSP garde les couples bicolores anciens ; les champs n-aires
  et les arrêts sont écrits uniquement lorsqu'ils sont utilisés.
- Studio : liste des fils avec couleurs éditables, ajout/retrait jusqu'à 8,
  géométrie et technique, inverse, courbes pour les remplissages, sauvegarde.
- Performance : aucun RNG global, pas d'allocation à l'évaluation d'une
  densité, tramage déterministe ; calcul du canevas réduit lors du
  déplacement de poignées (aperçu géométrique jusqu'au commit).

### Limitations importantes

- Les nouveaux fils sont saisis comme **couleurs libres** et non encore choisis
  automatiquement dans une **bibliothèque de références de fils réels** ;
  l'assistant camaïeu basé sur le stock et les recettes de couleur reste à faire.
- Le mode satin partage les traversées entre des fils, il ne prédit pas
  la physique d'une colonne satin interpénétrée ni la tenue après lavage.
- L'axe de géométrie et le profil de densité d'un **remplissage monochrome
  préexistant** ne sont pas combinés avec le fondu multicolore :
  un refus explicite évite une conversion destructive.
- Les curseurs physiques avancés de chaque fil (seuils de densité,
  contraintes de tissu, positions individuelles d'arrêt, mélange de techniques
  par couche, masque peint) et les **benchmarks instrumentés** restent
  à implémenter ou à étendre.
- Les générateurs multi-couches coûtent jusqu'à N passes sur la géométrie ;
  le gain de latence/mémoire **n'est pas encore mesuré** et la validation
  sur tissu réel est indispensable. Les tests automatiques valident des
  invariants structurels, pas la qualité esthétique du tissu.

## Deux notions, deux emplacements

1. **Dégradé de densité du remplissage** : une unique couleur de fil peut passer
   de points serrés à des points ouverts. Réglage de type de points (Tatami ou
   Directionnel, selon les générateurs effectivement supportés). Le champ
   `DensityGradient` modifie l'écartement des lignes. Il ne crée pas de nouveau
   fil ni de bloc de couleur.
2. **Fondu de couleurs (fils)** : une transition chromatique entre *des fils
   réels différents*, qui suppose plusieurs blocs de points et un ordre de
   broderie. Le profil de densité peut être utilisé **par couche** comme une
   technique de mélange, mais ne définit pas à lui seul le fondu logique.

Ces réglages sont orthogonaux : un remplissage directionnel peut avoir un
dégradé de densité et / ou porter une transition entre plusieurs fils. L'UI
ne doit pas confondre ces deux intentions ni convertir implicitement l'une
en l'autre.

## Première tranche UI implémentée dans cette branche

- L'éditeur de fondu est accessible directement dans les propriétés de
  l'objet, lorsqu'un remplissage compatible unique est sélectionné.
- Le tiroir Points/Fondu et l'inspecteur partagent **FadeProperties.qml**.
- Lorsqu'une colonne satin est sélectionnée, le même panneau expose explicitement
  **Satin entrelacé** : la création n-aire répartit les traversées entre les fils
  au lieu d'empiler des colonnes satin complètes.
- Les neuf profils de transition persistés par le moteur sont tous proposés dans
  Studio ; les profils individuels restent réservés aux remplissages, car le satin
  entrelacé est piloté par ses traversées et non par un champ de densité.
- L'interface distingue visuellement le fil de départ et le fil d'arrivée,
  donne accès à un vrai sélecteur de couleur et vérifie la saisie hexadécimale.
- Elle présente explicitement les deux répartitions de couches, **densités
  complémentaires** et **superposition sur fond constant**, ainsi que les
  quatre aspects, trois géométries, cinq profils et l'action inverser.
- L'orientation en degrés est réglable pour les champs linéaires et courbes,
  de deux à huit fils. Elle tourne l'axe commun autour de son centre sans
  changer sa longueur, en une commande annulable. Le radial n'affiche pas un
  faux angle. Le réglage manuel des coordonnées (mm) reste disponible pour
  l'axe bicolore linéaire et les poignées du canevas.
- La bande colorée n'est explicitement **pas** une simulation des points
  ou une prédiction de la couleur du textile.
- Les fonctions réellement exécutables continuent de passer par
  FadeController / fade_service / project_ops : une action = une commande
  annulable, sans recalcul complet pendant le déplacement de la poignée.

## Architecture cible / évolutions pour 3+ fils et camaïeux

Ne pas chaîner des `ColorBlendLink` binaires : ces liens réciproques codent
un couple exact, et plusieurs paires indépendantes auraient des erreurs de
sélection, conversion, suppression, ordre de couture et sauvegarde.

Proposer un **GradientBlendGroup** persistant, identifié de manière stable :

- forme source commune, type de point, axe/champ de progression ;
- liste ordonnée de `ThreadStop` (`ThreadKey` optionnel, RGB de repli,
  position [0,1], rôle et épaisseur/densité cible) ;
- couches de broderie réelles, indices de couture et références
  appartenant toutes au même groupe logique ;
- méthode de mélange, courbe/largeur de chaque transition, limite
  d'accumulation des points, graine déterministe pour l'irrégularité ;
- interopérabilité OSP versionnée et règle de dégradation contrôlée
  vers deux fils / anciens projets.

La sélection d'une couche doit sélectionner le groupe logique. Créer,
éditer, inverser, réordonner, supprimer, copier-coller et restaurer
undo/redo doivent fonctionner **atomiquement**. Le logiciel doit garder
les références des vrais fils (bibliothèque/stock), non interpoler des
couleurs RGB inventées en guise de fils.

### Construction du camaïeu

Mode assisté : couleur de base → 3/4/5/6 nuances recommandées depuis les
fils réellement disponibles ; ordre par clarté et/ou paramètre choisi.
Une proposition ne doit jamais être assignée à un fil de nuancier inconnu.
Mode manuel : ajouter/supprimer/déplacer chaque arrêt de couleur,
changer le nuancier, la position et la longueur de sa transition.

Chaque point d'abscisse curviligne `t` obtient des poids de couche
`w_i(t)`, positifs, de somme 1 en mode complémentaire, après traitement
du profil de transition. Les générateurs convertissent ces poids en
densités de couture bornées et déterministes. **Le modèle ne doit pas
interpoler des couleurs RGB comme s'il s'agissait de nouveaux fils.**

## Méthodes de transition (séparer technique de couleurs)

1. **Rampes complémentaires de densité** : méthode existante à deux fils,
   généraliser en poids `w_i(t)` pour n fils.
2. **Fond constant + surcouches légères** : méthode existante à deux
   fils, généraliser avec contrôle de surcharge.
3. **Bords effilochés / feathering** : pénétrations décalées de manière
   déterministe sur la bordure, limites irrégulières entre zones.
4. **Passé empiétant / long-and-short** : lignes de points de longueurs
   alternées, transitions imbriquées, orientation guidée.
5. **Points dispersés / tramage textile** : répartir les contributions de
   fils par points ou courtes lignes espacées, contrôlées par une densité
   globale et une graine.
6. **Transitions courbes / radiales** : champ de progression géométrique
   autre qu'un segment unique ; fonctionnalité séparée des méthodes
   ci-dessus.

Les méthodes 1–6 ont maintenant des **implémentations candidates** pour
une partie des objets supportés. L'atelier de références de fils, l'édition
de profils personnalisés et la confirmation d'un rendu cousu comparable aux
applications spécialisées restent des travaux distincts ; ces algorithmes ne
constituent pas une validation physique.

## Plan de vérification

- Deux fils : format OSP ancien, charge/sauvegarde, undo/redo, courbes,
  inversion, édition de la couleur, déplacement de l'axe.
- UI : sélection simple, champ hexadécimal invalide, couleur identique,
  dégradé de densité sans fondu, apparence du fondu dans l'inspecteur,
  champ numérique français `0,5`, dimensions petites.
- Multicolore à venir : 3–6 fils, ordre et références, permutation,
  suppression d'une couche, copie, export DST et réouverture OSP,
  densités max et comportement sur petites zones, reproductibilité de
  chaque graine, comparaison de la simulation avec des échantillons
  brodés réels.
- La CI ne valide pas à elle seule l'apparence des échantillons cousus.

## Références de conception

- Hatch Embroidery — Gradient Fill et Color Blending (effets distincts).
- Wilcom EmbroideryStudio 2026 / v28 — Multi Blend : mélange de deux
  couleurs ou plus par espaces de points variables.
- Embrilliance StitchArtist — Gradient et Feathering.
