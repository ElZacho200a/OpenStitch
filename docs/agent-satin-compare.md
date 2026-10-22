# Comparaison auto-satin par objet — C02 (agent)

État : script d'orchestration du moteur OpenStitch réel, sans modification
du format OSP. Le client CLI fournit déjà les trois stratégies de satin et
les analyses de couverture. Le script évite les opérations répétitives aux
agents et conserve toutes les variantes pour comparaison.

## Exécution

~~~bash
python3 scripts/openstitch_satin_compare.py motif.osp \
    --object 48 --out-dir essais/satin_48 --cli /chemin/openstitch-cli
~~~

Options :
- --spacing-mm 0.4 : écartement identique pour les trois variantes.
- --no-render : désactiver les images réalistes pour un test rapide.
- --out-dir : répertoire de nouveaux essais ; tout fichier de variante
  existant entraîne un refus, aucun écrasement silencieux.

Trois variantes, produites à partir du **même OSP source** :
skeleton, parallel_sweep, medial_axis.

Le script utilise successivement :
1. project inspect --object : identifier l'objet et son type.
2. project apply --dry-run : tester la conversion native avant modification.
3. project apply -o variante.osp : produire une copie, jamais toucher la source.
4. project inspect --object et project check --only : mesurer points,
   sauts, couverture et débordement.
5. project render : prévisualiser le résultat avec OpenStitch.
6. Écrire comparison.json (schéma openstitch.satin-compare/1), avec les essais,
   les rejets motivés et les variantes non dominées.

Le choix du type de conversion est adapté : un tatami/running/directional
passe par l'opération native auto_satin ; un auto_satin existant se teste
en changeant ses paramètres de stratégie. Un refus moteur est conservé
dans le rapport au lieu d'être ignoré.

## Exemple mesuré sur les ruines

Sur un objet étroit de l'OSP des ruines (ID 48), avec l'artefact CLI
officiel Linux et 0,4 mm d'espacement :

| Stratégie | Couverture mesurée | Débordement | Points | Sauts |
|---|---:|---:|---:|---:|
| skeleton | 100,00 % | 23,08 % | 120 | 2 |
| parallel_sweep | 99,71 % | 22,55 % | 92 | 2 |
| medial_axis | 81,18 % | 21,59 % | 80 | 2 |

Les trois projets et trois rendus ont été générés, en préservant l'OSP original.
Ce benchmark n'est représentatif que de cette **forme précise**.

## Précautions

Le front de Pareto considère quatre objectifs : couverture maximale,
débordement minimal, sauts et points minimaux. Ce n'est **pas** une décision
artistique ni une validation textile. Le satin qui présente moins de points
peut avoir une mauvaise couverture. Un contrôle des PNG produits et de
la brodabilité sur le tissu réel est indispensable.

La CLI OpenStitch ne doit pas être doublée par un moteur indépendant :
le script n'implémente ni satin, ni rendu, ni géométrie, seulement des
appels aux commandes officielles. Les échecs restent traçables.

Tests rapides sans Qt ni réseau :

~~~bash
python3 -m unittest discover -s tests/scripts -p test_openstitch_satin_compare.py
~~~
