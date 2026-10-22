# Retouches, contours, densité, routage, validation et livraison

Cette référence décrit la **seconde moitié du workflow** : corriger un vrai projet .osp sans perdre les détails de l'image. La qualité visuelle et la vérification de brodabilité doivent être traitées **séparément**. Les opérations citées sont documentées dans [cli-project.md](../../../../docs/source/cli-project.md) ; lire **project operations --json** de la version installée avant chaque lot.

## 1. Diagnostic de départ — trois échelles

Analyser systématiquement :

1. **Échelle motif entier** : proportions, détails distinctifs, couleurs de fil, contraste avec le tissu, silhouettes, distribution des grandes masses, équilibre des directions de remplissage.
2. **Échelle famille et instance** : pierre sous lierre, branchages distincts, joint de pierre, roseaux d'une touffe, reflets d'une même zone. Vérifier le choix de point et les bordures communes.
3. **Échelle région / point** : point court, trou local, pointe, satin mal couvert, dépassement de colonne, liaisons visibles, sous-couches excessives, sauts.

Ne pas réduire les détails en réponse à un problème de couture qui vient d'un **mauvais ordre**, d'une sous-couche ou d'un type de satin inadapté.

| Symptôme | Cause plausible | Intervention prioritaire |
|---|---|---|
| Arches lisses, feuilles effacées, eau monotone | reconstruction trop simplifiée | retour à l'original et re-segmentation fidèle |
| Fils colorés « bavent » sur les pierres | masques de familles se croisent, absence d'occlusion claire | corriger la séparation sémantique / la géométrie |
| Trop d'îlots | fragmentation de structures dans la segmentation | reconstruire les branches/touffes AVANT les objets |
| Beaucoup de running minuscules | petits remplissages déclassés en contours | audit running → satin/tatami/suppression |
| Gros éventails / satin incomplet | stratégie inadaptée ou topologie ramifiée | comparer les 3 stratégies, sinon découper |
| Tissu trop dense | sous-couches, couches superposées, compensation | mesurer hotspots puis retouche locale |
| Coupes élevées malgré fusions | objets encore disjoints et sauts | revoir continuité/routage ; une union n'est pas un parcours |
| Interstices blancs entre tatami | retrait, vectorisation, chevauchement | test local du inset ; vérifier détails protégés |
| Trop de fils quasi-identiques | palette couleur mal réduite | fusionner teintes tout en préservant les objets |

## 2. Audit complet des objets « Région en contour » / running

Un **running** n'est pas nécessairement une ligne intentionnelle : l'auto-numérisation peut transformer **une très petite région pleine** en « contour » quand sa surface tombe sous le seuil de remplissage. Il peut alors créer de nombreux objets et coupes sans vraie valeur graphique.

**À faire sur chaque objet running :**

1. Repérer son ID, sa famille, son parent sémantique, son aire et sa boîte en mm, son type, sa couleur, et ses voisins. **project select --where type=running** fournit les IDs pour l'inspection.
2. Chercher le **rôle initial** : joint de pierre voulu, brin d'herbe, bord de feuille, micro-tache d'anticrénelage ou doublon ?
3. Comparer la couverture réelle de la zone par les tatami/satins voisins ; identifier un fragment déjà masqué par le fil d'un objet parent.
4. Trois décisions : **garder**, **convertir**, **supprimer** :
   - **Garder** si c'est un trait structurant, même fin : joint, nervure, épaisseur de roseau, séparation utile.
   - **Convertir** si la forme est une vraie **bande fermée** et que son rôle devient plus lisible en satin ; tester skeleton, parallel_sweep et medial_axis si pertinent, contrôler succès et couverture.
   - **Supprimer** si fragment redondant qui n'apporte ni contraste, ni silhouette, ni couverture nécessaire ; ne jamais supprimer automatiquement par seul seuil d'aire.
5. Vérifier le résultat en zoom réaliste et **revenir en arrière** si perte visible. Conserver les formes vectorielles source si l'opération remove_object le permet et que l'on veut les récupérer.
6. Comparer nombre de running, satins, objets, coupes, densités **et zone non couverte** sur la même image source. Surtout : une suppression peut augmenter la fidélité perdue même si le rendu paraît plus propre.

**Cas V9 vérifié :** 288 running initiaux ; 100 bandes candidates testées avec squelette/balayage ; 52 converties en auto-satin, 38 supprimées ; 198 running restants. La couverture moyenne préliminaire des conversions était environ 97 %. Les coupes ne sont passées que de 2 768 à 2 734, tandis que le taux de zone source non couverte montait de 11,776 à 12,203 % (incluant tissu intentionnel). Ce n'est **pas** une politique universelle de supprimer 38 contours sur 288.

## 3. Diagnostic des îlots : deux problèmes distincts

**A. Trop de régions sémantiques** : lierre cassé, touffe fragmentée, pierre décomposée en facettes parasites. Solution **amont** : réinterpréter un objet porteur, conserver les détails, corriger les masques et refaire la numérisation.

**B. Objets correctement séparés mais routage maladroit** : bons objets, sauts et coupes élevés. Solution **aval** : recouvrement ordonné, trajet sous couture, regroupement de couleurs, sélection d'un chemin continu lorsque possible. Ne pas forcer la fusion physique.

Pour les fusions expérimentales sur OSP :

- Faire correspondre **instance_id + parent sémantique**, fil, type de point et géométrie.
- Refuser une union à travers un autre masque ; les « mêmes couleurs » ne suffisent pas.
- Tester **merge** en union sans pont, puis si légitime un raccord court. Noter les coupures réellement supprimées et les surfaces ajoutées.
- Une union de pièces qui restent **disjointes** peut ne pas diminuer les sauts/coupes. Un raccord peut rendre le contour visuellement pire, ou provoquer un satin avec mauvaise jonction.
- Tester toute optimisation d'ordre sur une copie : elle peut réduire la distance et **augmenter** les coupes ou le nombre de changements de couleur selon le projet.

**Cas V8 vérifié :** 11 fusions strictes lierre/buissons/roseaux, 1 098 → 1 087 objets, 2 772 → 2 768 coupes ; 67 mm² de densité ≥4 couches inchangés. Les rapprochements plus larges ont été écartés car ils fusionnaient notamment des nénuphars différents. Il n'y a **pas** de loi « fusion = moins de coupes ».

## 4. Réglage des surépaisseurs

Faire une **carte de densité** et localiser les hotspots. Examiner dans l'ordre :

- Objets contributeurs et leurs superpositions intentionnelles, sous-couches cumulées, zones de tatami + satin sur le même espace.
- Sous-couches qui peuvent être réduites sans perte de tenue (un satin central épais sur un pull ne doit pas être désactivé aveuglément).
- Espacement et densité locale, retraits tatami, compensation de tirage, effet des raccords.
- Variantes de stratégie satin sur les mêmes objets, à couverture comparable.
- Cohérence de l'ordre de couture : grande base sombre en premier quand elle est pertinente ; petits détails après si le fil ne doit pas les masquer.

Ne pas « corriger » une surépaisseur en trouant une zone essentielle du motif. **Density** estime des couches de fil, pas la pression, la traction ni la stabilité du tissu ; un projet valide peut quand même déformer un jersey.

**Cas V5 → V6 :** conserver les 1 098 objets et alléger réglages/sous-couches a réduit les points de 81 368 → 69 773 et les zones ≥4 couches de 266 → 93 mm² ; les coupes restaient 2 786. Cette étape démontre qu'on peut alléger la broderie **sans** supprimer les détails géométriques, mais pas résoudre le problème des îlots.

## 5. Opérations CLI et versions : méthode sûre

~~~bash
# Préparation : version réelle et catalogue
openstitch-cli --version
openstitch-cli project operations --json > operations.json
openstitch-cli project inspect projet.osp --json > inspect.json
openstitch-cli project select projet.osp --where type=running --json > running.json
openstitch-cli project select projet.osp --where type=auto_satin --json > satin.json

# Diagnostic complet de la V1
openstitch-cli project check projet.osp --json > check.json
openstitch-cli project diagnose projet.osp --json > diagnose.json
openstitch-cli project fidelity projet.osp --map fidelite.png --json > fidelity.json
openstitch-cli project density projet.osp --heatmap densite.png --json > density.json
openstitch-cli project render projet.osp -o preview.png --px-per-mm 8
openstitch-cli project at projet.osp --px 600,420

# Quand une V2 existe :
openstitch-cli project diff projet.osp projet_v2.osp --json > diff.json
~~~

Les lots d'opérations utilisent le schéma **openstitch.batch/1** :

~~~json
{
  "schema": "openstitch.batch/1",
  "operations": [
    {"op": "remove_object", "object": 100},
    {"op": "set_params", "object": 200, "params": {"strategy": "parallel_sweep"}}
  ]
}
~~~

Les IDs dans cet exemple sont **fictifs**. Ils doivent venir d'un inspect du vrai fichier, jamais d'une supposition. Tester :

~~~bash
openstitch-cli project apply projet.osp retouches.json --dry-run --json > dry_run.json
openstitch-cli project apply projet.osp retouches.json -o projet_v2.osp --json > apply.json
~~~

Vérifier que le dry-run a réussi (une erreur ne doit modifier aucun fichier). Le moteur applique les lots de façon **transactionnelle** : si une opération échoue, le projet d'entrée reste inchangé. La création de nouveaux objets peut changer les IDs : tenir à jour la table de correspondance sémantique et les journaux.

**Important :** l'interface de cette version n'a pas de commande officielle « segmenter sémantiquement toute l'image » ; ne pas attribuer à **project apply** le travail réalisé en Python/OpenCV.

## 6. Mesurer les gains avec leurs contreparties

Journal par variante :

| Catégorie | Mesures indispensables |
|---|---|
| Visuel | détails protégés présents, lisibilité à 240 mm, contours, direction du fil, couleurs, rendu global et zooms |
| Structure | instances correctes, nombre d'objets par type, îlots satellite, objets supprimés/convertis/fusionnés |
| Technique | points, sauts, **coupes**, fil, couleurs/changements, dimensions, cadre, points trop courts |
| Couverture | trous, détails perdus, couverture **par objet satin**, mise à jour du tissu volontairement laissé visible |
| Densité | surface ≥ 4 couches, maximum estimé, hotspots et contributeurs |
| Opérations | IDs, commande CLI exacte, raison de la modification, fichiers source et résultant |
| Risques | ce qui reste non mesuré, avertissements, état du test physique |

**Ne pas confondre :**
- La fidélité à l'image *préparée* avec la fidélité artistique à l'original. Changer la source change les métriques.
- Surface sans fil *intentionnelle* (tissu) avec trou accidentel ; les rapports peuvent regrouper ces pixels selon la définition de la région source.
- Nombre d'objets ≠ nombre de coupes : un objet multipièce peut demander plusieurs déplacements.
- Plus de satin ≠ meilleure broderie ; en satin, l'esthétique peut primer sur le minimum de points.
- « Avertissements affichés : 50 » ≠ exactement 50 avertissements au total : certaines sorties limitent l'affichage.
- Erreurs bloquantes à zéro ≠ broderie validée physiquement.

## 7. Comparaison visuelle contrôlée

Pour chaque passe :

1. Fixer **source, échelle, couleur de tissu, position et cadrage**.
2. Produire **project render** en plein motif et à 2–4 zooms sur les zones modifiées.
3. Comparer à l'original, à une référence manuelle si fournie et au meilleur essai précédent.
4. Conserver ou revenir en arrière en justifiant par une différence **visible ou mesurée**.
5. Ne jamais présenter une visualisation artisanale de segments DST comme le rendu réaliste OpenStitch. Ne pas qualifier de satin une simple bande de couleur d'image.

Les « scores esthétiques » et les familles sémantiques ne sont **pas** des mesures directes du moteur : les déclarer comme évaluations visuelles ou annotations manuelles.

## 8. Livraison et arrêt

Créer les fichiers suivants, avec leur provenance :

~~~text
final/
  broderie.osp
  broderie.dst
  preview.png
  carte_semantique.png
  segmentation_technique.png
  palette.json
  regions.csv
  selection_satins.csv
  fusions_semantiques.csv
  decisions_contours.csv
  rapport.md
broderie_complete.zip
~~~

Le .osp final doit s'ouvrir et être inspecté ; le DST doit être exporté **à partir de ce .osp**, pas depuis une vieille variante. Vérifier la taille brodée, le cadre avec marge, les points, les coupes, les couleurs et la correspondance avec le rendu. Si possible comparer les statistiques DST et celles du contrôle .osp.

**Critère d'arrêt :** détails identitaires présents, aucun défaut bloquant, îlots restants justifiés ou documentés, satins couverts et visuellement appropriés, surépaisseurs majeures examinées, dernières retouches marginales. Rappeler **explicitement** qu'un motif dense et complexe sur pull extensible nécessite stabilisation, essai réduit sur tissu similaire, observation des déformations et ajustement physique.

Ne pas modifier le code du dépôt pour contourner un échec du workflow, sauf si l'utilisateur a demandé un développement/une PR.
