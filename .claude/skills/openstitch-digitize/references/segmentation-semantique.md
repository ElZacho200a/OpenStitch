# Segmentation et fusion sémantiques — du dessin à l'objet brodable

Ce chapitre est la **phase amont** du skill. Il décrit le travail que ni la seule réduction de palette ni l'auto-numérisation fondée sur les couleurs ne peuvent inférer : la reconnaissance de ce qui constitue un objet textile cohérent. Il ne prétend **pas** qu'une commande native du CLI fait déjà la segmentation sémantique.

## 1. Quatre niveaux d'identité à ne jamais confondre

| Niveau | Exemple des ruines gothiques | Décision permise |
|---|---|---|
| Famille | végétation, architecture, ciel, eau | contraintes par classe, jamais fusion globale |
| Instance | lierre *du pilier de gauche*, nénuphar *n° 3* | identifier le même objet réel malgré plusieurs teintes |
| Composant logique | tige d'une liane, feuille, joint de bloc, lumière d'un nénuphar | former la structure porteuse et les détails indépendants |
| Objet technique de broderie | bande satin n° A, tatami n° B, running n° C | coupures aux embranchements et variations de direction/densité |

Un identifiant RGB décrit une **couleur**, pas une de ces identités. Deux régions de lune et pierre beige ne sont pas parentes ; deux feuilles vertes de nénuphars différents ne sont pas la même instance.

Une palette de 5 fils n'implique **ni 5 familles ni 5 masques**. L'expérience ruines V5 employait 28 classes techniques pour 9 fils. L'approche peut évoluer selon le motif, et le nombre de teintes réelles doit servir le rendu et la machine.

## 2. Structures sémantiques et occlusions

Décomposer la scène dans un arbre, par exemple :

~~~text
scene/
  architecture/
    ruine_1/
      arcade_gauche/
        intrados/
        extrados/
        pierres_eclairees/
        joints/
      pilier_central/
      pierre_ombre/
  vegetation/
    lierre_sur_ruine_1/
      branches_maitresses/
      tiges_laterales/
      feuillage/
    buisson_1/
    touffe_roseaux_1/
      base_commune/
      tiges_individuelles/
      epis/
  ciel/
    lune_1/
    nuage_1/
    nuage_2/
  eau/
    masse_eau/
    reflets_lune/
    reflets_architecture/
    nenuphar_1/
    nenuphar_2/
  fond_tissu_non_brode/
~~~

Chaque composant doit avoir une **priorité graphique** : indispensable (arche, silhouette), importante (joint, reflet dominant, épi), secondaire (petit feuillage) ou parasite. Un détail difficile à broder mais indispensable peut nécessiter un changement de type de point au lieu d'une suppression.

Renseigner les relations : « au-dessus », « derrière », « même tige », « même arche », « non contigu mais même instance », « deux instances distinctes ». Ces relations préviennent les inversions d'ordre de couture et les fusions absurdes.

## 3. Construction de l'image de travail

1. Analyser **l'original à pleine résolution** : alpha/transparence, contours, gradients, grandes masses, détails à 240 mm (ou taille réelle).
2. Conserver une copie de l'original et un masque des détails protégés (joints, contours d'arche, petits reflets, épis).
3. Nettoyer localement l'antialias et les pixels isolés. Préférer la correction des **bavures entre deux familles** à l'érosion globale.
4. Produire une image à aplats cohérents ; **ne pas inventer de contours noirs** et ne pas faire de halo blanc autour des régions adjacentes.
5. Comparer sur les *mêmes cadrages* une variante « fidèle » et une variante « allégée ». Rejeter l'allégée si les traits distinctifs ont disparu.
6. Attribuer une **fausse couleur par identifiant de région** uniquement pour les traitements techniques ; mémoriser la couleur réelle dans une autre table.
7. Détecter et rétablir les occlusions. Par exemple, la pierre doit rester derrière la liane, et non fusionner dans le même masque par proximité RGB.
8. Vérifier les détails à leur taille physique ; noter les longueurs/largeurs en millimètres, pas seulement les pixels.

**Retour d'expérience :** la V4 reconstruite était propre mais effaçait trop de pierres, feuillages et reflets. La V5 repartant de l'original a conservé les détails, au prix de davantage d'objets et de coupes. On ne gagne pas en qualité en simplifiant uniformément.

## 4. Schéma de données conseillé

Le format suivant est **un format de travail externe**, pas un schéma OSP ni un format CLI officiel.

~~~json
{
  "semantic_id": "veg.ivy.arch1.branch3",
  "family": "lierre",
  "instance_id": "ivy_arch_1",
  "parent_id": "veg.ivy.arch1",
  "role": "tige_laterale",
  "mask_path": "masks/ivy_arch1_branch3.png",
  "priority": "important",
  "protected_detail": true,
  "visible_on_fabric": false,
  "thread_rgb": "#68723C",
  "stitch_intent": "auto_satin",
  "orientation_hint_deg": null,
  "merge_group": "ivy_arch_1",
  "under": [],
  "over": ["architecture.arch_1"],
  "vector_ids": [],
  "embroidery_ids": [],
  "decision": "keep",
  "evidence": "branch attached to same vine"
}
~~~

**Export conseillé** : une ligne CSV ou entrée JSON par région avec masque, aire mm², largeur locale, périmètre, allongement, nombre de composantes, parent/instance, voisinages, couleur de fil, type de point, orientation et motif de décision. Conserver les identifiants d'une version à l'autre ; une nouvelle segmentation doit consigner les remplacements (ancien ID → nouveau ID).

## 5. Algorithme pratique : graphe de fusion contrôlée

Construire un graphe sur les **composants d'une même instance** :

- **Nœuds** : région, classe, instance, géométrie, couleur de fil, priorité, rôle, largeur locale, aire mm², direction et contraintes de point.
- **Arêtes candidates** : contact, distance minimale en mm, direction tangentielle, continuité de la structure porteuse, surface d'un raccord possible, autre région traversée ou masquée, potentiel de réduction de coupes.
- **Contraintes dures** : instance différente = *refus* sauf preuve artistique explicite ; protection d'un joint/épi/reflet = ne pas absorber ; traversée d'une autre classe = *refus* ; incompatibilité majeure de point / direction = re-segmenter, pas fusionner aveuglément.

Pour chaque petit îlot, essayer dans l'ordre :

1. **Rattaché naturellement** à une même structure (ex. feuille incluse dans masse de lierre) → absorbable si la nouvelle frontière reste lisible et les points adaptés.
2. **Même instance mais fragmentée** par anticrénelage (ex. branche interrompue) → reconstruire une forme continue **en amont** avec un pont justifié, puis redessiner les masques.
3. **Détail indépendant protégé** (joint de pierre, épi, feuille distincte) → garder en objet ou remplacer par un point fin, jamais supprimer au seul motif qu'il est petit.
4. **Satellite redondant** (tache très proche d'une masse de même rôle) → supprimer seulement après comparaison visuelle, avec justification.
5. **Forme ramifiée** devenue trop large pour le satin après fusion → subdiviser en colonnes, ou utiliser tatami/point droit.
6. **Ambigu** → ne pas fusionner ; journaliser pour une passe d'analyse supplémentaire.

Ordre de priorité : même instance **et** même branche/touffe → continuité de la tangente → complémentarité des masques → réduction des coupes. Le gain métrique n'est **jamais** une autorisation de franchir les contraintes dures.

### Raccords : validation obligatoire

Si un pont est envisagé entre deux morceaux d'une même liane :

- limiter sa longueur et sa largeur selon **le tissu, le fil, le motif et l'échelle** (pas de seuil universel) ;
- vérifier que toute la zone ajoutée appartient à la structure à représenter ;
- vérifier qu'aucune autre famille, ombre protégée ou trou nécessaire n'est recouverte ;
- exécuter la numérisation et comparer la zone dans le rendu ;
- mesurer la surface ajoutée, les coupes, les épaisseurs et la couverture ;
- revenir à l'état initial si le raccord est visible comme une excroissance, crée une nouvelle jonction ingérable ou supprime un détail.

Une **fusion native** d'objets dans le .osp peut ne changer que leur regroupement. Si les polygones restent physiquement disjoints, la broderie peut toujours comporter des déplacements et coupes. Cela a été observé sur les ruines V8.

## 6. Matrice par famille (point de départ, pas règle universelle)

| Famille | Fusionner / reconstruire | Éviter | Intentions probables |
|---|---|---|---|
| Lierre | branche maîtresse + fragments appartenant à **la même liane** | lier deux lianes voisines par couleur | satin pour tiges, tatami petites masses si trop larges |
| Buissons | masse porteuse et îlots satellites appartenant au même buisson | perdre toute variation de feuillage | petites bandes satin, masses tatami |
| Roseaux | bases d'une touffe, feuilles d'une même tige | fondre tous les épis / créer une lame épaisse | satin tiges, running détails |
| Pierre | facettes de la **même partie de pilier** si relief conservé | effacer joints et séparation entre arches | tatami blocs, satin reliefs, running joints |
| Nuages | segments continus du **même nuage** | fusionner nuage et lune de même teinte | tatami / remplissage horizontal, rarement satin |
| Eau/reflets | bandes appartenant au **même système de reflet** | relier des reflets indépendants en colonne lourde | tatami horizontal, running petites stries |
| Nénuphars | sous-parties d'une **même feuille** | fusionner deux feuilles distinctes | tatami, petite nervure running |

## 7. Contrôles de la segmentation avant OpenStitch

Checklist minimale :

- Les familles importantes et leurs **instances** sont toutes identifiées.
- Aucun pixel de lierre n'est devenu « pierre » par simple proximité RGB.
- Chaque objet a un rôle et une décision : garder, fusionner, découper, running, satin, tatami, vide.
- Les feuilles distinctes ne sont pas fusionnées ; les vides voulus existent.
- Les détails protégés sont encore présents en comparant à l'original à la taille finale.
- Pas de halo blanc / trou ou chevauchement accidentel entre aplats.
- Couleurs techniques et couleurs de fils dans deux tables distinctes.
- Les objets techniques sont des régions géométriquement exploitables (contours fermés pour les remplissages).
- Les variantes sont versionnées ; les modifications de forme peuvent être attribuées à un objectif et mesurées.

## 8. Mesurer les conséquences, pas seulement les intentions

Pour chaque essai de regroupement, comparer **à source identique** : objets, points, coupes, largeur, trous de couverture, surface de densité ≥ 4 couches, rendu des détails protégés et nouveaux raccords visibles. Évaluer aussi le coût d'un **objet trop étendu** : l'économie d'une coupe n'autorise pas un satin avec franchissement de branches.

L'expérience V8 : 11 fusions géométriques strictes ont fait passer 1 098 → 1 087 objets et 2 772 → 2 768 coupes, sans changer la densité ≥4 couches (67 mm²). Le résultat **n'autorise pas** à promettre de gros gains en fusionnant après la numérisation. Une structure continue correctement interprétée dès la segmentation est la piste prioritaire.
