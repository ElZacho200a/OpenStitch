# Cas d'étude reproductible — ruines gothiques au bord de l'eau (V1–V9)

**Statut :** ces chiffres sont issus des **rapports de numérisation des itérations documentées** avec le CLI OpenStitch sur une illustration de ruines gothiques, lierre, lune, lac et roseaux, largeur demandée autour de 240 mm. Ce n'est **pas** un banc d'essai machine ni un benchmark universel. Les choix par objet ont été comparés sur le moteur (rendu réaliste, project check, density, fidelity) et parfois à une modélisation manuelle .osp de référence.

**Attention à l'interprétation** : les premières versions n'ont pas toujours la **même image préparée, la même hauteur effectivement brodée, la même palette ni la même structure d'objets**. Les comparaisons de points/coupes sont descriptives ; seules les variantes qui modifient **le même OSP et les mêmes formes** peuvent être utilisées pour isoler un effet technique.

## 1. Déroulé et décisions importantes

| Version | Entrée / méthode | Objets et fils | Mesures marquantes | Enseignement |
|---|---|---|---|---|
| V1 | numérisation initiale, tatami partout | 358 tatami, 10 fils | 96 870 points, 954 coupes après allègement | contours et petites bandes trop uniformes ; tatami ≠ solution universelle |
| V2 | conversion ciblée auto-satin sur V1 | 82 satin, 276 tatami, 10 fils | 97 307 points, 947 coupes | la combinaison est mieux, mais « satin sur quelques bandes » reste une vision trop pauvre |
| Modélisation manuelle | OSP fourni pour montrer l'intention | **356 auto-satin / 66 tatami**, 422 objets, **5 fils** | palette fortement regroupée et tissu non brodé | le **rôle des objets et la couleur du tissu** comptent plus que la couleur source |
| V3 interprétative | 5 fils, tissu apparent, première lecture sémantique | 245 satin, 124 tatami, 55 running | 64 674 points, 1 155 coupes, ~240 × 178 mm | une méthode peut être meilleure graphiquement sans chercher une majorité de tatami |
| V4 sémantique simplifiée | masques séparés par familles mais reconstruction trop stylisée | 115 satin, 220 tatami, 84 running, 5 fils | 56 102 points, 1 335 coupes, 240 × 176,8 mm | séparation propre mais **perte de détails** : trop simplifier en amont détruit l'image |
| V5 détaillée | retour à l'**original**, 28 classes techniques, 9 fils | **1 098** objets = 252 satin / 558 tatami / 288 running | **81 368 points, 2 810 coupes**, ≥4 couches : 266 mm², 240,1 × 158,9 mm | grande amélioration de détail mais fragmentation et densité plus fortes |
| V6 allégée | retouche de la V5, **géométrie des 1 098 objets conservée** | 252 satin, 558 tatami, 288 running, 9 fils | **69 773 points**, 2 786 coupes, ≥4 couches : **93 mm²** | agir sur la sous-couche/réglages peut améliorer densité **sans perdre les détails** |
| V7 satin hybride | tests A/B/C sur les 252 auto-satins de V6 | 142 skeleton / 96 parallel_sweep / 14 medial_axis | **67 986 points**, **2 772 coupes**, ≥4 couches : **67 mm²** | couverture ET rendu esthétique nécessaires ; stratégie unique insuffisante |
| V8 fusion expérimentale | 11 fusions/raccords sémantiques stricts sur V7 | **1 087 objets** ; 9 fils | **67 995 points**, **2 768 coupes**, ≥4 couches : 67 mm² | **fusion aval peu rentable** ; éviter de lier des objets réels distincts |
| V9 nettoyage des contours | 52 running → auto-satin et suppression ciblée de 38 running sur V8 | **1 049 objets**, 304 satin / 547 tatami / **198 running** | **67 479 points, 2 734 coupes**, ≥4 couches : 65 mm² | running de repli à auditer ; suppression modérée plutôt qu'automatique |

Les mesures initiales mentionnées ci-dessus proviennent du journal de ces essais : si un détail est absent d'une archive ou si le contexte de génération change, **re-mesurer** avec le CLI avant de comparer. Ne pas recopier ces nombres comme des seuils par défaut.

## 2. Ce que la référence manuelle enseignait

L'utilisateur a produit son propre .osp (422 objets : 356 auto-satin, 66 tatami) en comprimant la palette à **5 fils**. Il utilisait le satin comme langage visuel structurant et laissait des ombres représentées par le **tissu**. Le projet avait ses limites ; il a servi de **référence stylistique** et non de vérité technique unique.

L'apprentissage majeur : ne pas commencer par « quantifier en N teintes puis remplir ». Commencer par « quelles structures, faces de pierres, ramifications de lierre, bandes et vides voulons-nous réellement coudre ? ». L'objectif n'est pas d'obtenir 85 % de satin partout, mais de juger **chaque objet selon sa fonction textile**.

## 3. Pourquoi V4 → V5 a été indispensable

Une image reconstruite trop simple a effacé des **joints de pierre, touches végétales, épis de roseaux et petits reflets** avant l'entrée dans OpenStitch. Même une carte sémantique propre n'a pas pu restaurer ces détails. Le retour à une préparation de l'illustration originale plus fidèle a rendu ces éléments, au prix de :

- 419 → 1 098 objets ;
- 5 → 9 fils ;
- 56 102 → 81 368 points ;
- 1 335 → 2 810 coupes ;
- 124 → 266 mm² estimés à au moins 4 couches.

Ce changement de **source technique et de cadrage** interdit de présenter un taux de fidélité relatif à la nouvelle source comme un gain universel. Le bon objectif est **conserver les détails significatifs puis optimiser les points**, pas « réduire l'image et déclarer le projet propre ».

## 4. Pourquoi V5 → V6 a été utile

En **conservant les 1 098 géométries**, une passe de réduction de sous-couches et réglages des points (dont certaines stratégies de verrouillage) a réduit :

- points : 81 368 → **69 773** ;
- coupes : 2 810 → **2 786** ;
- fil estimé : environ 92,98 → **83,56 m** ;
- zones ≥4 couches : 266 → **93 mm²** ;
- maximum estimé : 7,68 → **5,68 couches**.

C'est un exemple de gain techniquement interprétable sur **source et objets constants**. Les coupes sont restées très élevées ; l'allègement de densité **ne règle pas** la fragmentation structurelle.

## 5. Benchmark des trois stratégies V6 → V7

252 auto-satins testés sur les mêmes formes et paramètres de base :

| Stratégie appliquée à tous | Couverture moyenne des objets | Points projet | Coupes | Zone ≥4 couches |
|---|---:|---:|---:|---:|
| skeleton | **97,49 %** (252 objets) | 69 773 | 2 786 | 93 mm² |
| parallel_sweep | **96,25 %** (252 objets) | 63 835 | 2 779 | **1 mm²** |
| medial_axis | **89,48 %** (241 objets) | 64 107 | 2 685 | 34 mm² |
| **hybride choisi** | sélection objet par objet | **67 986** | **2 772** | **67 mm²** |

**Onze objets medial_axis n'ont produit aucun point**, et 39 autres couvraient moins de 80 % de leur surface. Le balayage global évitait presque toutes les surépaisseurs simulées, mais causait des **lacunes et des éventails** sur certaines ramifications. Exemple relevé : un objet de buisson, ID 1670, couvert à 96,65 % en skeleton contre 69,58 % en balayage.

Sélection par famille réalisée (pas règle universelle) :

| Famille | Skeleton | Balayage | Axe médian |
|---|---:|---:|---:|
| Buissons | 32 | 18 | 4 |
| Lierre | 50 | 42 | 5 |
| Pierres | 34 | 23 | 1 |
| Roseaux | 26 | 13 | 4 |
| **Total** | **142** | **96** | **14** |

Ces statistiques sont des **moyennes non pondérées par aire** et n'évaluent pas automatiquement l'esthétique. Le résultat a été choisi par **contrôle visuel + couverture**, et non par un optimum universel de points/coupes. Voir [selection-satins.md](selection-satins.md).

## 6. V8 : test négatif de la fusion après coup

Le graphe des régions candidates a été filtré par famille, instance supposée, fil, point et proximité. Vingt-trois rapprochements ont été essayés avec fusion native et raccords ; plusieurs auraient uni des **nénuphars distincts**. Onze rapprochements stricts ont été conservés (5 lierre, 4 buissons, 2 roseaux). Les raccords ajoutaient environ **0,671 mm²** de surface.

| Essai | Objets | Points | Coupes |
|---|---:|---:|---:|
| V7 témoin | 1 098 | 67 986 | 2 772 |
| Union native de 23 rapprochements | — | 67 945 | 2 763 |
| Raccords des 23 | — | 67 987 | 2 768 |
| **V8 conservée** (11 stricts + trajets cachés) | **1 087** | **67 995** | **2 768** |

La meilleure réduction naïve de coupes n'a pas été retenue car elle **sacrifiait la cohérence sémantique**. Les 11 fusions finales ont supprimé 11 entrées d'objet mais seulement 4 coupes. Un rapprochement ne prouve pas que les pièces pourront être cousues en une seule traversée. Il faut préparer en amont **des branches et touffes continues**. Voir [segmentation-semantique.md](segmentation-semantique.md).

## 7. V9 : les « régions en contour » comme source d'îlots

La V8 contenait **288 running**, dont nombre de petits fragments issus de la segmentation et déclassés en contours, et non de lignes intentionnellement dessinées. L'audit a choisi :

- **52 conversions en auto-satin** : 11 pierres, 14 roseaux, 13 lierre, 14 buissons ;
- **38 suppressions** de contours satellites : 8 nénuphars, 1 eau, 12 lierre, 7 buissons, 8 pierres, 2 roseaux ;
- 198 running restants ; total **1 049 objets**.

Les deux stratégies satin testées sur 100 bandes candidates étaient skeleton et parallel_sweep. Les 52 conversions ont eu une couverture préliminaire moyenne de ~97 % selon les mesures de la variante ; les objets trop irréguliers sont restés en contour ou ont été traités selon leur valeur graphique. V9 : **67 479 points, 2 734 coupes, 65 mm² de forte surépaisseur**, zéro erreur bloquante signalée par le contrôle.

La zone de source technique non couverte est passée de **11,776 à 12,203 %** : la suppression n'est donc **pas** gratuite, même si elle retire des artefacts. Cette métrique inclut notamment les vides de tissu intentionnels et **ne peut pas être assimilée à un taux de défaillance textile**. La variante la plus agressive a été écartée pour sa perte supplémentaire de couverture.

## 8. Principes confirmés et hypothèses restantes

**Observé dans ces essais :**
1. Une segmentation sémantique **sans détails de source** reste pauvre.
2. Les petits fragments peuvent être sémantiquement réels tout en étant de mauvais objets brodables.
3. Le satin doit être comparé en trois variantes **localement**, en vérifiant qu'il produit des points et couvre la forme.
4. Modifier sous-couches et stratégie peut réduire densité **sans changer la géométrie**.
5. Fusion d'objets disjoints **après** numérisation réduit très peu les coupes.
6. Un contour « running » est parfois une région remplie déclassée, qui mérite une autre intention.

**Reste à tester / ne pas présenter comme prouvé :**
- une segmentation apprenant la continuité des tiges et des structures porterait-elle un gain majeur de coupes à détail constant ?
- quel routage sous couture minimise les coupes sur lierre ramifié sans ajouter de brins visibles ?
- la meilleure stratégie satin en simulation reste-t-elle supérieure **sur une vraie maille** avec stabilisateur ?
- combien de coupes/points accepte effectivement la machine et quel impact des tensions sur le pull ?
- comment quantifier un score esthétique sans sacrifier les détails protégés ?

**Aucune de ces versions n'est déclarée validée sur machine.** Un test d'échantillon stabilisé, la vérification de déformation et des ajustements locaux demeurent nécessaires.

## 9. Consignes de reproductibilité

- Toujours noter dimensions, DPI, fichier de source technique, palette, version OpenStitch, fond/tissu et options.
- Décrire les changements d'une version à la suivante ; séparer les tests contrôlés *même OSP* des reconstructions différentes.
- Générer les statistiques du DST et les diagnostics depuis le **même OSP final**.
- Conserver les décisions par objet (satin), les fusions et les running supprimés, avec IDs et motifs.
- Évaluer les images à même cadrage et via **project render**, pas un rendu simplifié fait maison.
- Ajouter une nouvelle expérience avec données avant/après plutôt que transformer la V9 en réglages forcés pour d'autres motifs.

**Conclusion opérationnelle :** interpréter les structures d'abord, conserver la richesse graphique, faire des objets techniques cohérents, essayer plusieurs points et stratégies satin, puis optimiser la densité et enfin les trajets. Le skill sert à guider ce jugement, **pas** à prétendre qu'une numérisation automatique brute résout ces problèmes.
