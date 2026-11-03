# Retouches collectives d'objets OSP — C01 (agent)

État : pont Python vers les opérations transactionnelles existantes
du client officiel OpenStitch. Aucune mutation du moteur ou du format OSP.

Un agent peut filtrer les objets par type, couleur, surface et identifiants
issus de project inspect, puis créer un lot natif openstitch.batch/1.

## Exemple sur les ruines

~~~bash
python3 scripts/openstitch_bulk_edit.py ruines.osp \
    --ids 48,336 \
    --operation set_params --params '{"row_spacing_mm":0.45}' \
    --out ruines-retouchees.osp --apply --cli /chemin/openstitch-cli
~~~

Sans --apply, le script **ne crée pas de nouvel OSP** : il écrit le lot JSON,
exécute project apply --dry-run et retourne le rapport. Avec --apply, un
nouveau projet est écrit après ce contrôle, puis comparé à l'original avec
project diff --json. L'OSP source n'est jamais modifié.

Sélections possibles :
- --type tatami (ou running, auto_satin, directional)
- --color #RRGGBB
- --min-area-mm2 et --max-area-mm2
- --ids 48,336,150
- --max-objects 20 (limite de sécurité par défaut)

Au moins un filtre est obligatoire ; les objets verrouillés sont exclus.
Si la sélection excède la limite, le script refuse de produire un lot.
Un fichier de sortie existant n'est jamais écrasé.

Opérations proposées :
- set_params : paramètres adaptés au type existant.
- set_color : couleur de fil.
- set_stitch_type : running, tatami ou directional.
- auto_satin : conversion native ; si l'objet est déjà auto_satin, ses
  paramètres sont modifiés via set_params sans nouvelle géométrie.

## Garanties et limites

- Les identifiants proviennent du vrai project inspect du fichier OSP.
- Le moteur official valide toute commande avec project apply --dry-run.
- Une opération refusée entraîne un rapport JSON d'échec, et aucune
  copie OSP n'est écrite.
- project diff permet de vérifier les changements réalisés.
- Le script n'infère pas encore les familles sémantiques du manifeste B01.
  Il faut connecter B01 et B03 avant de filtrer par instance de lierre,
  arche, pierre ou roseau.
- Ce script **ne remplace pas** project check, density, fidelity,
  le rendu réaliste ni la validation sur machine après une retouche.

Validation : 5 tests unitaires stdlib ; essai sur le véritable projet des
ruines (objets 48 et 336, espacement tatami 0,45 mm), copie OSP produite,
diff officiel non vide.

~~~bash
python3 -m unittest discover -s tests/scripts -p test_openstitch_bulk_edit.py
~~~
