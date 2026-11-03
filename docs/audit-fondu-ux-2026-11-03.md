# Fondu de couleurs — audit UX/UI et évolution du parcours

Date : 3 novembre 2026. Base : PR #230 (remplissage logique Uni/Dégradé).

## Objectif

Éditer le fondu comme un **remplissage créatif unique**, tout en conservant deux
couches de broderie liées et une vraie référence de fil pour chaque couche.
Les réglages avancés doivent rester accessibles sans être obligatoires.

## Livré dans la PR

- **Références de fils** : bouton par couleur ouvrant un sélecteur filtrable de
  nuanciers, recherche par code/nom/marque et proposition de fils proches
  (via la bibliothèque CIEDE2000 existante). La sélection conserve
  `ThreadKey` + RGB dans les deux couches ; une couleur RGB libre reste
  possible, explicitement indiquée.
- **Lecture du résultat** : profil théorique visible sans dérouler « Avancé »,
  avec limites de transition et profils distincts. Il ne simule ni les points
  ni le comportement du tissu. Bouton direct vers l'aperçu des points calculés
  dans le dialogue existant.
- **Risque de superposition** : explication du profil fond uniforme, accès
  en un clic à la carte de densité réellement calculée depuis la séquence,
  avec zones de vigilance dans Analyse. Les seuils restent heuristiques,
  et la validation sur tissu demeure nécessaire.
- **Geste sur le canevas** : les lignes de début/fin suivent désormais l'axe
  pendant le glisser ; la génération des points reste déclenchée au relâchement
  pour éviter une mutation par événement souris et préserver l'undo.
- **Expression visuelle** : ajout des courbes monotones « Départ lent »
  (t²) et « Départ rapide » (2t-t²), en plus des trois profils historiques.
  Les valeurs de `DensityCurve` sont append-only (0 à 4) ; toute valeur
  inconnue est relue en linéaire. Les anciens `.osp` restent inchangés.
  Le schéma des courbes utilise les mêmes formules que le moteur, en particulier
  la quintique « Marquée ».

## Scénarios de vérification

1. Remplissage tatami → Dégradé → sélectionner un fil de nuancier pour chaque
   couche → quitter et retrouver les deux `ThreadKey` à la réouverture.
2. Revenir à une couleur libre RGB : la référence de nuancier est retirée,
   sans affecter le fil de l'autre couche.
3. Inverser les couleurs : les références suivent leur couleur.
4. Rééditer un ancien fondu, modifier les poignées, constater que les limites
   suivent pendant le glissement. Une seule opération dans l'historique.
5. Ouvrir l'aperçu des points réels et la carte de densité depuis l'inspecteur.
   Ne pas prendre le profil théorique pour une prévisualisation des points.
6. `.osp` : courbes 0 à 4, valeurs inconnues, projets sans la clé `curve`.
7. Contrôler les cas de forme concave, trouée, petite, et les valeurs de densité
   extrêmes avant toute validation sur machine.

## Hors du lot actuel — décisions d'architecture requises

- **Fondu multicolore** : le modèle actuel utilise un `ColorBlendLink`
  réciproque entre exactement deux objets. Une rampe à trois fils ou plus
  demande un objet logique n-aire avec références, ordre, undo, copy/paste,
  persistance et génération pour chaque couche ; ne pas chaîner silencieusement
  plusieurs couples indépendants.
- **Radial / courbe / masque peint** : `DensityGradient` décrit actuellement
  un segment de projection linéaire. Il faut un véritable champ scalaire de
  transition indépendant des directions de points, des types géométriques
  persistants, une dégradation contrôlée des anciennes versions et des
  références de test.
- **Courbe libre à points de contrôle** : il faut garantir monotonie,
  bornage, suivi de la forme et lecture/écriture `.osp`. Ne pas détourner
  les valeurs enum existantes.
- **Aperçu des points en continu au glisser** : construire une voie de
  prévisualisation incrémentale/cancellable sur instantané des deux couches,
  séparée des commandes d'annulation et de l'export. Les calculs lourds ne
  doivent pas bloquer le fil UI.
- **Calibrage sur tissu** : mesurer couverture, variations de densité,
  rugosité et surcharge avec plusieurs fils/tissus/entoilages ; ne pas
  promettre de prédire la couleur textile par simple mélange RGB.

Ces chantiers de moteur ne doivent pas être présentés comme « corrigés »
par la seule refonte du panneau. Les options non implémentées doivent rester
absentes de l'UI jusqu'à obtenir les modèles, tests et métriques nécessaires.

## Validation

Tests automatiques ajoutés pour la visibilité des références, les commandes
de réédition et les nouvelles valeurs de courbe. La CI est l'autorité pour
le build Qt / MSVC ; aucune validation sur machine réelle n'est revendiquée.
