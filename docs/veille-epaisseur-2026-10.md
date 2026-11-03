# Veille : épaisseur de la broderie par type de point (HP-STI-025)

Date : 2026-10-20. Origine : roadmap nocturne manuscrite du 2026-10-20 (« Veille point différente
épaisseur »). Alimente la carte d'épaisseur (HP-VIEW-014) et, plus tard, le rendu 3D facultatif
(HP-VIEW-002).

**Statut de ce document : une veille, pas une mesure.** Une recherche web courte (cinq requêtes
et deux pages lues) n'a trouvé **aucune mesure en millimètres** de l'épaisseur d'un point de
broderie. Les valeurs retenues plus bas sont donc soit *sourcées* (fils, mousse), soit *calculées* à
partir de données de fils, soit des *hypothèses d'ingénierie*. Chaque valeur dit laquelle des trois.
Rien ici n'est validé sur une machine réelle : la validation relève de HP-PHYS-001.

## 1. Question

Combien un motif de broderie s'épaissit-il, selon le type de point, la densité et les passes
superposées ? Pourquoi : prévenir les zones trop épaisses (le tissu se déforme, l'aiguille dévie) et
montrer à l'utilisateur où le fil s'accumule.

## 2. Ce que le code sait déjà (état d'OpenStitch)

- `stitch_analysis::compute_density_map` (HP-VIEW-005) mesure par case : pénétrations d'aiguille par
  mm² et **longueur de fil**. L'épaisseur cumulée s'exprime en « couches de remplissage dense » =
  longueur de fil × largeur du fil ÷ surface de la case. Un remplissage dont l'écart de rangées égale la
  largeur du fil vaut 1 couche.
- La règle d'analyse `couches-superposees` signale au-delà de 3 couches (`max_layer_thickness`).
  Son commentaire cite Liu et al., CGF 2023 (ne superposent que 2 couches) et estime « 2 remplissages
  + leurs sous-couches ≈ 2,4 couches (bords ≈ 2,6), trois ≈ 3,6 ». Ces chiffres sont des
  heuristiques d'ingénierie du dépôt, **non mesurées** (le commentaire le dit pour la densité).
- Toutes les passes sont dans la séquence (`StitchPass` : sous-couche, dessus) : la mesure compte donc
  déjà l'empilement sous-couche + dessus sans traitement par type.

Conséquence : **la mesure de fil par case est déjà indépendante du type de point.** Un satin, un
tatami, un contour se distinguent par la longueur de fil qu'ils déposent par mm², pas par un coefficient.
Ce qui manque est la conversion « couches » → « millimètres ».

## 3. Faits sourcés

| Fait | Source |
|---|---|
| Un fil 40 wt vaut 240 deniers, soit 25 tex (grammes par kilomètre). Le grammage « wt » est le nombre de mètres par gramme. | [Schmetz, guide des tailles de fil](https://www.schmetzneedles.com/pages/sewing-machine-thread-sizing-guide) |
| La plupart des fils de broderie sont en 120/2 (deux brins de 120 deniers). | même page |
| Plus le « wt » est petit, plus le fil est épais ; plus le denier ou le tex est grand, plus il est épais. La table de Wilcom va d'environ 14 tex (très fin) à environ 80 tex (extra épais). | [Wilcom, épaisseur de fil et aiguille](https://wilcom.com/resources/blog/thread-thickness-and-needle-size) |
| Conseil de Wilcom : réduire la densité pour un fil épais, l'augmenter pour un fil fin. Aucune valeur chiffrée. | même page |
| La page de Wilcom ne donne **aucun diamètre de fil en mm** (seulement des diamètres d'aiguille : Nm 75 = 0,75 mm). | même page |
| Broderie en relief (« puff ») : mousse EVA de 2 à 3 mm ; au-dessous de 2 mm le relief est insuffisant, au-dessus de 3 mm l'aiguille peut dévier (casse, fil effiloché, problème de synchronisation). Une mousse de 6 mm existe, avec une densité nettement supérieure. | [Embpunch, 3D puff](https://www.embpunch.com/blog/What-is-3D-Puff-embroidery) et [Ignition Drawing](https://www.ignitiondrawing.com/blog/working-with-3d-puff-embroidery-part-1) |
| Puff : uniquement du satin, jamais du remplissage (il aplatit la mousse), à environ le double de densité, avec davantage de sous-couche ; satin limité à environ 12 mm de large ; bouts de colonne fermés par un satin de capuchon. | mêmes pages (blogs de services de numérisation : consignes de praticiens, pas des normes) |
| Sous-couche de satin : inscrite à environ 0,4 mm du bord. Trop peu de sous-couche laisse le dessus instable, trop ajoute du volume. | [Z Digitizing, sous-couche](https://zdigitizing.com/underlay-stitch-for-embroidery-and-digitizing/) (blog, non normatif) |
| Une zone brodée est plus épaisse que le tissu nu ; le type de remplissage et la densité modifient la géométrie de l'élément brodé. Aucun chiffre en mm dans les extraits lus. | étude KTU sur maille brodée, citée par la recherche (`epubl.ktu.edu`, texte intégral non lu) |
| TrueView de Hatch montre « couleurs, textures et rendu sur un vêtement » ; la page ne parle ni de relief, ni de hauteur, ni de 3D. | [Hatch, TrueView](https://help.hatchembroidery.com/portal/en/kb/articles/what-is-trueview-in-hatch-embroidery-and-when-should-i-use-it) |
| Wilcom propose une « simulation 3D » dans son espace de travail ; la recherche ne trouve pas comment le relief y est modélisé. | [Wilcom, espace de travail](https://docs.wilcom.com/embroiderystudio/27/en/OnlineHelp/Basics/basics/basics-4.htm) |

## 4. Valeurs calculées (dérivées, non mesurées)

Diamètre d'un fil 40 wt à partir de ses 25 tex. Section pleine = masse linéique ÷ masse volumique :
25 mg/m ÷ 1 380 kg/m³ (masse volumique usuelle du polyester, valeur de mémoire non sourcée ici) ≈ 0,018 mm², soit un diamètre
d'environ **0,15 mm** pour un cylindre plein. Un fil réel contient de l'air entre ses fibres : en
supposant un facteur de remplissage de 0,6 à 0,8 (hypothèse), le diamètre apparent monte à
**0,17 à 0,20 mm**. Pour de la viscose (≈ 1 500 kg/m³) le calcul donne un peu moins.

Ce chiffre est un **ordre de grandeur théorique**. Il ne dit ni de combien un fil s'écrase sous la
tension, ni de combien il s'enfonce dans le tissu, ni comment des couches successives se tassent.

## 5. Hypothèses retenues pour la carte (à valider)

| Réglage | Valeur par défaut | Nature |
|---|---|---|
| Épaisseur d'une couche dense (`layer_thickness`) | 0,20 mm | hypothèse, bornée par le calcul du §4 (0,15 à 0,20 mm) |
| Largeur du fil retenue pour la mesure | 0,40 mm | héritée de HP-VIEW-005 |
| Empilement des couches | linéaire (n couches = n × 0,20 mm) | hypothèse **optimiste** : en réalité les couches se tassent |
| Seuil « trop épais » | 3 couches (soit 0,6 mm) | hérité de la règle `couches-superposees`, lui-même heuristique |
| Mousse (puff) | non modélisée | la hauteur de mousse (2 à 3 mm) est une donnée de l'objet, pas du fil |

La conversion est donc : `épaisseur_mm = couches × layer_thickness`. Le défaut est un **point de
départ réglable**, affiché comme une *estimation* dans l'interface, jamais comme une mesure.

## 6. Ce que la veille n'a pas trouvé

- Aucun relevé en mm de l'épaisseur d'un remplissage, d'un satin ou d'une sous-couche.
- Aucune loi de tassement des couches successives.
- Aucune description de la façon dont Hatch ou Wilcom calculent le relief de leur vue 3D.
- Le texte intégral des deux études (KTU, JTATM) n'a pas été lu : elles peuvent contenir des tableaux
  d'épaisseur par densité de points. **C'est la première piste à suivre.**

## 7. Mesures physiques à faire (pour valider la table, HP-PHYS-001)

1. Broder un échantillon par type de point (tatami, satin, directionnel, contour répété) sur le même
   tissu et le même fil 40 wt, et mesurer l'épaisseur au pied à coulisse ou au comparateur
   (par exemple à pression constante) en trois points par échantillon.
2. Faire varier le nombre de passes (1, 2, 3, 4) pour obtenir la courbe de tassement : c'est ce qui
   remplacera l'empilement linéaire.
3. Comparer à la carte d'épaisseur d'OpenStitch pour les mêmes fichiers et ajuster `layer_thickness`.
4. Répéter avec un fil 60 wt et un fil 30 wt (HP-THR-009) : la valeur par défaut dépendra du fil.

## 8. Suite

- HP-VIEW-014 : carte d'épaisseur livrée avec les valeurs du §5, réglables, présentées comme une
  estimation.
- HP-VIEW-002 (rendu 3D, facultatif) : repose sur la même carte ; non commencé.
- Ouvrir HP-THR-009 (poids du fil) quand l'épaisseur par défaut devra suivre le fil choisi.
