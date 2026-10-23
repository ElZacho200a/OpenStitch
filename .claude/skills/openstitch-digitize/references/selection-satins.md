# Sélection comparative des trois auto-satins — protocole reproductible

Cette procédure a été **exécutée** sur les ruines gothiques V6 → V7 ; voir [cas-ruines-v1-v9.md](cas-ruines-v1-v9.md). Elle est destinée aux **objets déjà identifiés comme candidats au satin**, après segmentation sémantique et fusion des structures porteuses. Les trois moteurs doivent être considérés comme des **variantes de fabrication**, pas trois filtres de beauté interchangeables.

## 1. Stratégies réellement exposées par OpenStitch

| Valeur CLI | Méthode | Forces typiques | Échecs possibles |
|---|---|---|---|
| **skeleton** | squelette de la région, graphe de branches et colonnes | bonne couverture de formes organiques ramifiées, si les jonctions restent cohérentes | longueurs/retours, trop de couches, petites branches irrégulières |
| **parallel_sweep** | balayage parallèle, lignes successives | remplissage léger et régulier de bandes / petites formes compactes | lacunes, discontinuité aux embranchements, éventail disgracieux, mauvaise direction |
| **medial_axis** | axe médian vectoriel ; stratégie distincte du squelette raster | certaines bandes et géométries simples | axe instable, défaut de couverture, aucun point produit sur certains objets |

Les paramètres se configurent via **auto_satin** (conversion/création) et **set_params** (objets déjà auto-satin). Vérifier toujours les **noms, unités et schémas actuels** avec :

~~~bash
openstitch-cli --version
openstitch-cli project operations --json
openstitch-cli project inspect projet.osp --json > inspect.json
~~~

Remarque majeure : **set_stitch_type ne convertit pas en auto-satin**. Pour une région en point droit ou tatami, utiliser l'opération **auto_satin**, et contrôler ses diagnostics. La conversion peut refuser un objet ; dans ce cas elle ne doit pas être considérée comme un succès.

Voir aussi la [documentation des stratégies](../../../../docs/source/satin-squelette.md) et le [catalogue CLI](../../../../docs/source/cli-project.md).

## 2. Constituer un corpus de candidats pertinent

- Inclure des objets appartenant à plusieurs familles : pierre, lierre, touffe de roseaux, buisson, reflets étroits.
- Protéger les grandes surfaces intrinsèquement tatami (lune, ciel, eau, feuilles larges). Ne pas tester du satin **juste parce qu'un nuage est allongé**.
- Regarder les jonctions, trous, changements brutaux de largeur, étirement et largeur maximale par rapport aux limites du moteur.
- Ne pas lancer un essai coûteux sur tous les contours minuscules : trier d'abord les objets à forte valeur visuelle et ceux dont la géométrie permet raisonnablement une colonne.
- Conserver le même **vecteur source, fils, sous-couche, espacement, ordre et échelle** dans A, B, C ; la stratégie doit être la seule variable durant cette comparaison.
- Noter si le moteur classe l'objet comme adapté, adapté avec avertissements, à décomposer, ou inadapté. Une conversion « acceptée » avec trous reste un mauvais candidat.

La première décision reste toujours **satin ou pas satin**. Ne pas forcer un satin dans une forme sémantiquement plus pertinente en tatami ou en running.

**Test de colonne avant les trois stratégies :** renseigner la largeur **locale** (et pas seulement `geometry_bounds`), la présence d'embranchements, les changements brusques d'épaisseur et les éventuels rails/traverses de direction. Préférer une ligne running si trop fine, ou plusieurs colonnes distinctes si la forme se divise ; le moteur ne doit pas être forcé à « inventer » une colonne sur un nuage entier. Si `auto_satin` renvoie `RequiresDecomposition`, enregistrer ce verdict puis re-segmenter au lieu d'accepter automatiquement parce que la couverture dépasse un seuil.

**Retour du navire :** une passe heuristique fondée sur l'allongement de la boîte avait créé 23 auto-satins `skeleton` et **aucun guide explicite**. Les tests A/B/C n'avaient pas été effectués sur ce projet. Ce résultat prouve une conversion réelle, mais **pas** la qualité ni le choix optimal d'un satin. Voir [planification textile](directions-et-planification-textile.md).

## 3. Générer trois variantes contrôlées

Pour chaque candidat, relever son ID dans **project inspect** (ou **project select** selon le type), puis dupliquer le projet OSP **avant** modification :

- Variante A : strategy = skeleton ;
- Variante B : strategy = parallel_sweep, angle éventuel choisi et documenté ;
- Variante C : strategy = medial_axis.

Les copies peuvent être générées en lot de façon transactionnelle avec **project apply**. Exemple illustratif d'un objet **déjà auto-satin** d'ID 123, et uniquement après validation des arguments renvoyés par le catalogue :

~~~json
{
  "schema": "openstitch.batch/1",
  "operations": [
    {
      "op": "set_params",
      "object": 123,
      "params": {"strategy": "parallel_sweep"}
    }
  ]
}
~~~

~~~bash
openstitch-cli project apply source.osp changement.json --dry-run --json
openstitch-cli project apply source.osp changement.json -o balayage.osp --json
openstitch-cli project check balayage.osp --json > balayage_check.json
openstitch-cli project render balayage.osp -o balayage.png --px-per-mm 8
~~~

Pour un objet non satin, utiliser **auto_satin** et noter le résultat de l'opération (ne jamais supposer l'ID ou le fait qu'une colonne a été produite). Exécuter le même protocole avec **skeleton** et **medial_axis**. Ne pas modifier en place le projet maître : **project apply sans -o écrase le fichier source**.

Dans un test exhaustif, on peut construire A/B/C à partir de copies du même OSP en paramétrant **tous les auto-satins** selon la variante. Dans un second temps, appliquer les stratégies retenues **objet par objet** dans une V hybride. Les mesures globales A/B/C servent de référence, mais les décisions restent locales.

## 4. Mesurer chaque objet, et pas seulement le DST global

Pour chaque variante, collecter :

1. **Succès du moteur :** aucun refus, au moins un point généré, objet toujours présent.
2. **Couverture de l'objet** calculée par OpenStitch, si disponible, et zones non couvertes.
3. **Débordement et lisibilité des contours** : ne pas ajouter une aile d'éventail qui traverse un objet voisin.
4. **Continuité :** coupure brutale de l'axe, jonction bancale, saut, retournement ou vrillage apparent.
5. **Direction des fils :** suit-elle la tige, contourne-t-elle l'arche, respecte-t-elle la perspective ?
6. **Surépaisseurs locales :** sous-couches + couches déjà présentes dans les zones de recouvrement.
7. **Coût technique :** points, fil estimé, sauts, coupes, points très courts.
8. **Rendu artistique** sous le même tissu de fond, au même cadrage ; contrôler en zoomant aux échelles pertinentes.

Outils documentés :

~~~bash
openstitch-cli project inspect variante.osp --object 123 --geometry --json
openstitch-cli project check variante.osp --json > check.json
openstitch-cli project density variante.osp --heatmap densite.png --json > density.json
openstitch-cli project fidelity variante.osp --map fidelity.png --json > fidelity.json
openstitch-cli project render variante.osp -o preview.png --px-per-mm 14 \
  --crop 0.18,0.22,0.48,0.64
~~~

**Interprétation prudente :** la couverture d'une région est un critère *local*, les trous et pertes de détail sont aussi influencés par d'autres objets et par l'image de référence. Un saut de couleur sur l'aperçu n'est pas nécessairement un trou : vérifier les fils réels. Les chiffres de densité sont des estimations logicielles, pas la tension de tissu.

## 5. Politique de sélection — filtres durs puis classement

### Étape A : refus obligatoire

Rejeter une variante si :

- aucune commande de couture produite ;
- retour d'erreur ou forme classée inadaptée / nécessite décomposition sans plan correctif ;
- couverture nettement inférieure à la meilleure stratégie **sans bénéfice artistique démontré** ;
- défaut visuel fort (éventail, rupture, lacune sur un élément protégé) ;
- recouvrement important d'une autre instance, mauvaise direction ou pic de densité problématique.

La valeur de couverture de 90 ou 95 % peut servir de **seuil de présélection spécifique au motif**, mais **pas de norme de brodabilité**. Un axe médian couvrant 95 % peut avoir un trou au mauvais endroit. Un squelette à 96 % sur une branche peut être préférable à un balayage à 98 % avec une jonction affreuse.

### Étape B : classement multi-critères

Entre variantes techniquement **admissibles**, utiliser un classement explicite, ou une frontière de Pareto sur :

- fidélité géométrique et lisibilité de la forme ;
- continuité / orientation du fil et cohérence des voisins ;
- trous et couverture réels ;
- épaisseurs et densité ;
- nombre de points, coupes et longueur de fil.

Ne pas publier des pondérations arbitraires comme des règles universelles. Exemple de priorités : détail protégé et rendu correct sont des contraintes **dures** ; réduction des couches et des coupes départage **ensuite** les variantes valides.

Si plusieurs choix sont équivalents, conserver le satin d'origine évite des modifications sans gain identifiable. En cas d'incompatibilité des trois, découper la structure et **retester les sous-objets**, plutôt qu'accepter la meilleure d'un mauvais lot.

### Étape C : validation de la version hybride

Après regroupement des décisions locales, **régénérer le projet entier** : les fils posés à des endroits proches ne sont pas la somme indépendante de leurs simulations. Comparer au témoin les pics de densité et les liaisons. **Toujours faire un rendu complet final**.

## 6. Journal obligatoire par objet

Tenir **selection_satins.csv** (format de travail recommandé, non schéma officiel CLI) avec au moins :

| Champ | Exemple / finalité |
|---|---|
| object_id, semantic_id, family, instance_id | traçabilité |
| candidate_reason | bande de pilier, liane, tige, etc. |
| strategy | skeleton / parallel_sweep / medial_axis |
| accepted, failure_code | valeur booléenne et refus nommé |
| stitch_count, coverage_pct, overflow_pct | résultats de chaque méthode si mesurés |
| density_hotspot, trims, jumps | métriques locales ou limites de mesure |
| visual_issue, aesthetic_grade | lacune, éventail, vrillage, beauté du fil (jugement explicite) |
| chosen_strategy, decision_reason | meilleur compromis et justification |
| source_version, result_version | provenance et comparaison |

Pour les comparaisons de métriques, indiquer les versions OpenStitch et l'image source technique ; ce sont des expériences contrôlées, pas des chiffres généraux.

## 7. Cas réel V6 → V7 : résultat et anti-règles

Les **252 auto-satins** du projet ruines V6 ont été comparés en A/B/C (même géométrie, 9 fils, mêmes réglages par défaut).

| Mesure | Squelette global | Balayage global | Axe médian global | Hybride retenu |
|---|---:|---:|---:|---:|
| Points totaux | 69 773 | 63 835 | 64 107 | 67 986 |
| Coupes | 2 786 | 2 779 | 2 685 | 2 772 |
| Surface ≥4 couches (mm²) | 93 | 1 | 34 | 67 |

Couverture moyenne **non pondérée** des satins : squelette **97,49 %**, balayage **96,25 %**, axe médian **89,48 % sur 241 objets**, avec **11 objets sans point** et 39 autres sous 80 % de couverture. Le balayage global était extrêmement léger selon la densité, **mais détériorait visiblement certaines masses de lierre et de buissons**.

Hybride : **142 skeleton, 96 parallel_sweep, 14 medial_axis**. Il a réduit les zones ≥4 couches de **93 à 67 mm²**, sans chute visuelle majeure constatée, mais n'a réduit que **14 coupes**. Les familles ne constituent pas un classifieur fixe : sur des buissons on observe aussi des choix de balayage et d'axe médian.

**Règles vérifiées :**
- Plus léger globalement ≠ meilleur localement.
- Axe médian échoue complètement sur certains objets ; il faut vérifier des points réellement produits.
- Squelette peut mieux couvrir une forme ramifiée qu'un balayage pourtant plus économique.
- Changer de méthode ne résout **pas** les îlots et les coupes issus des fragments isolés.
- Les trois méthodes doivent être **retestées** après toute fusion ou redécoupe changeant la topologie.

Voir [cas-ruines-v1-v9.md](cas-ruines-v1-v9.md) pour les autres versions. Les chiffres sont des **mesures de simulation sur un motif**, non des preuves de brodabilité en machine.
