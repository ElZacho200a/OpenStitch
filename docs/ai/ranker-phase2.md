# IA-AUTO-01 — Phase 2 : données et ranker de candidats (V0)

> État de ce lot : **prototype ML hors moteur** ; ni dataset de broderies réelles,
> ni modèle validé, ni interface Studio. Les fichiers de cette phase n'exécutent
> pas OpenStitch et ne créent aucun objet de broderie.

## Pourquoi classer les candidats au lieu de générer directement un DST ?

La génération de points reste dans le moteur C++ déterministe d'OpenStitch.
Un futur orchestrateur (phase L1, dépendance non présumée livrée) produira
plusieurs plans techniques admissibles pour une **même région sémantique**.
Cette phase fournit un protocole supervisé pour **apprendre une préférence**
entre ces plans plutôt que de coder tous les arbitrages à la main.

Un exemple constitue un **choix humain explicite entre 2 à 16 propositions**.
Les données ne sont **pas** dérivées implicitement d'un fichier DST ni d'un
journal d'actions de l'utilisateur. Aucune collecte automatique n'est incluse.

## Contrat de données JSONL

Fichier UTF-8, un objet JSON par ligne, schéma `openstitch.ai.ranker-sample/1`.
Tous les candidats sont calculés *sur le même objet, au même instant*, avec les
mêmes contraintes de tissu/échelle/palette. Une différence de matériau ou de
taille exige un autre groupe d'expérience ; ne pas la masquer dans les features.

~~~json
{
  "schema": "openstitch.ai.ranker-sample/1",
  "sample_id": "demo-001",
  "motif_group": "motif-001",
  "semantic_id": "bateau.voile.1",
  "provenance": {
    "source_kind": "human_confirmed",
    "rights": "original artwork owned and consented",
    "opt_in": true
  },
  "preferred_id": "satin-1",
  "candidates": [
    {
      "id": "satin-1",
      "technique": "auto_satin",
      "features": {
        "coverage_ratio": 0.95,
        "detail_retention_ratio": 0.97,
        "angle_coherence_ratio": 0.89,
        "short_stitch_ratio": 0.02,
        "overdensity_ratio": 0.02,
        "uncovered_ratio": 0.03,
        "travel_mm": 33.0,
        "trim_count": 2,
        "stitch_count": 650
      }
    },
    {
      "id": "tatami-1",
      "technique": "tatami",
      "features": {
        "coverage_ratio": 0.91,
        "detail_retention_ratio": 0.76,
        "angle_coherence_ratio": 0.40,
        "short_stitch_ratio": 0.07,
        "overdensity_ratio": 0.08,
        "uncovered_ratio": 0.06,
        "travel_mm": 49.0,
        "trim_count": 3,
        "stitch_count": 720
      }
    }
  ]
}
~~~

Les valeurs ci-dessus sont **purement illustratives**, pas des mesures réalisées
sur un projet. Toutes les features sont **obligatoires**, finies, non négatives
et bornées. Les trois premières sont comprises entre 0 et 1, tout comme les
ratios `short_stitch_ratio`, `overdensity_ratio`, `uncovered_ratio`.
Le modèle n'accepte pas une valeur manquante en la remplaçant silencieusement
par zéro.

**Vocabulaire des techniques :** `tatami`, `auto_satin`, `satin`,
`running`, `directional`, `spiral`, `radial`, `contour`.
Ne pas étiqueter une méthode comme disponible pour un objet avant d'avoir
vérifié la capacité du moteur à la générer correctement.

### Provenance et sécurité des données

- `source_kind: human_confirmed` requiert `opt_in: true`. Il décrit un
  consentement enregistré dans l'exemple ; l'outil ne peut pas vérifier un
  consentement externe, qui reste une responsabilité du producteur de données.
- `source_kind: synthetic` est autorisé **pour tests**, et marqué
  `synthetic_only` dans le modèle lorsqu'il n'y a aucune donnée humaine.
- `rights` obligatoire : ne jamais présumer que des motifs de catalogue
  propriétaire ou des fichiers tiers sont réutilisables pour l'entraînement.
- Aucun upload, collecte, extraction de pixels, lecture de dossier OSP, appel
  réseau ou télémétrie. Les modèles sérialisent uniquement des poids et des
  **métriques agrégées**, pas les IDs, commentaires ou images source.

## Commandes sans dépendances

~~~bash
python3 scripts/auto_digitize_ranker.py validate ./local_samples.jsonl
python3 scripts/auto_digitize_ranker.py train ./local_samples.jsonl ./local_model.json
python3 scripts/auto_digitize_ranker.py evaluate ./independent_holdout.jsonl ./local_model.json
python3 scripts/auto_digitize_ranker.py rank ./local_model.json ./query.json
python3 -m unittest discover -s tests/scripts -p 'test_auto_digitize_ranker.py' -v
~~~

`rank` accepte un seul exemple JSON avec les mêmes candidats sans exiger
`preferred_id` ni `provenance`. Le résultat donne des **scores de classement**
sans interprétation probabiliste. Les scores ne constituent pas une validation
physique, une mesure de tension du tissu ou une garantie de brodabilité.

`train` crée exclusivement un **nouveau** fichier modèle (refuse l'écrasement).
L'exemple d'entraînement est découpé par `motif_group` selon un hachage stable :
un même motif, même avec plusieurs régions ou variantes, ne peut jamais être
à la fois dans entraînement et validation. L'entraînement optimise une perte
logistique **pairwise** par préférence approuvée contre chaque autre candidat,
avec régularisation L2, sans ordre ni hasard dépendant du runtime.

Le JSON modèle `openstitch.ai.ranker-model/1` inclut : liste exacte des
features, poids, SHA-256 du dataset canonique, nombre de motifs et exemples
par sous-ensemble, taux `top1_accuracy` de validation et baseline de
règles simples. La validation initiale est un **holdout interne**, à compléter
par une **évaluation externe sur de nouveaux motifs** et des tests textiles.

## Comparaison expérimentale de candidats

Après avoir entraîné un modèle, fournir un exemple JSON contenant **plusieurs
candidats du même objet** et leurs métriques **réellement mesurées** :

~~~bash
python3 scripts/auto_digitize_ranker.py compare ./local_model.json ./query.json
~~~

Le résultat `openstitch.ai.candidate-comparison/1` indique
`model_choice`, `heuristic_choice`, `agree`, les deux scores par
candidat et `applied: false`. Les scores ne sont pas des probabilités.
**Ce lot n'invoque pas encore le moteur C++ et ne produit pas les mesures** :
le producteur C++ doit encore exposer des alternatives et les métriques
effectives (couverture, détails, points, trajets et coupes) sans valeur
synthétique. Ne pas utiliser une décision du modèle pour modifier un OSP/DST
avant l'intégration et les garde-fous de brodabilité.

## Garde-fous de décision (prototype hors moteur)

~~~bash
python3 scripts/auto_digitize_ranker.py assess ./local_model.json ./query.json --min-margin 0.1
~~~

L'évaluation applique cinq améliorations expérimentales : (1) filtrage des
candidats sur couverture, zones non couvertes, points courts et surdensité ;
(2) abstention explicite si aucun candidat n'est admissible ; (3) repli
heuristique lorsqu'un seul candidat passe ou lorsque la marge est faible ;
(4) marge de score configurable entre les deux meilleurs candidats
admissibles ; (5) métriques top-2 et rang réciproque moyen (MRR) lors de
l'entraînement et de l'évaluation. Les seuils sont des **valeurs de
recherche**, pas des limites textiles validées. Le résultat est uniquement
consultatif : `applied: false`, sans mutation OSP/DST.

Un garde-fou supplémentaire rejette toute recommandation du modèle
**strictement dominée au sens de Pareto** par un autre candidat admissible :
couverture, détails et cohérence d'angle au moins aussi bons, et points
courts, surdensité, découvert, trajets, coupes et points au plus aussi
coûteux, avec au moins une amélioration stricte. Un compromis réel entre
deux métriques n'est pas rejeté. En cas de domination, le choix consultatif
revient à la baseline heuristique, avec `reason: pareto_dominated` et
`dominated_by`. Cette règle ne remplace pas un essai textile.

Les métriques de couverture et de brodabilité doivent être **mesurées** sur
des candidats générés par le moteur C++ ; elles ne sont pas produites par ce
script. Aucun gain de retouche n'est démontré à ce stade.

## Audit apparié par technique (évaluation)

`evaluate` et le holdout de `train` incluent désormais `paired_audit` /
`heldout_paired_audit` : nombres de cas gagnés uniquement par le modèle,
uniquement par la baseline, désaccords de choix, et exactitude top-1
**par technique préférée par l'annotateur** (et non technique prédite).
Les mêmes exemples servent aux deux classements, sans répartition
aléatoire supplémentaire. Un modèle qui progresse globalement mais régresse
sur les satins doit ainsi être identifiable. Ce rapport reste descriptif :
les effectifs faibles, les données synthétiques et l'absence de tests sur
tissu empêchent toute revendication de performance réelle.

## Intégration future : contrats et dépendances

1. **Producteur de candidats (L1)** : intégrer un adaptateur C++/CLI de
   propositions garantissant même région, options de broderie et niveau de
   confiance des métriques. Les features doivent être calculées réellement :
   ne jamais inventer les champs « detail_retention » ou « angle_coherence ».
2. **Collecteur d'avis opt-in** : formulaire Studio permettant de comparer
   A/B les candidats et d'enregistrer le choix, le matériau, les paramètres
   et l'identité sémantique. Aucun apprentissage automatique en tâche de fond.
3. **Jeu de données robuste** : plusieurs familles de motifs, styles et tissus ;
   regrouper les variantes/crops par motif d'origine ; annotation de qualité et
   revue des biais. Garder un jeu test verrouillé non consulté à l'entraînement.
4. **Mesure métier E14** : comparer temps réel de retouche, nombre d'actions,
   couvertures et coupes, perte de détails protégés, stabilité de l'OSP/DST.
5. **Intégration Studio CX05** : seulement une fois sa PR et son contrat stabilisés ;
   ne pas dupliquer le moteur de numérisation ni écrire dans ses fichiers réservés.
6. **Fallback sûr** : des poids incompatibles, une feature manquante ou un
   objet hors distribution doivent conduire à **aucune sélection automatisée**,
   avec retour à la stratégie C++ existante (non implémenté dans ce prototype).
7. **Calibration réelle** : la cible pilote de -30 % de temps médian de retouche
   est une hypothèse de projet, **pas une performance atteinte**.

## Définition du lot livré dans cette PR

- [x] Schéma explicite, validation stricte, consentement/provenance
- [x] Baseline heuristique et premier ranker pairwise déterministe
- [x] Split anti-fuite par motif et métriques holdout
- [x] Versionnement du modèle et refus des entrées incompatibles
- [x] Tests unitaires sans téléchargement de modèle
- [ ] Entraînement sur corpus humain réel autorisé
- [ ] Comparaison objective sur vrais OSP + photos de broderie
- [ ] Adaptateur automatique depuis les mesures du moteur
- [ ] Comparaison dans Studio / choix utilisateur
- [ ] Validation textile / promotion du modèle par défaut

**Aucun gain de retouches ni amélioration physique n'est revendiqué sans ces
quatre derniers travaux.**
