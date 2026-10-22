---
name: openstitch-evaluate
description: Evaluer sur 20 une numerisation OpenStitch reelle, comparer ses versions et piloter une boucle d'amelioration autonome traçable. Pour une image et un OSP/DST, un banc de tests de broderie, une auto-critique, ou une tâche planifiée d'experimentation.
---

# Evaluation indépendante et boucle d'apprentissage — OpenStitch

## Principe

Ce skill **complète** [openstitch-digitize](../openstitch-digitize/SKILL.md), ne le remplace jamais.
Il vise un agent de recherche *auto-améliorant* par experimentation, **pas un réentraînement
des poids du modèle**. Ne pas annoncer « apprentissage » sans journal et transfert démontré
sur des images qui n'ont pas servi à la mise au point.

La **source de vérité est le .osp natif** : même OSP pour le rendu et le DST.
La note porte sur la **qualité de broderie**, pas sur la facilité d'obtenir un bon chiffre.
Le rendu logiciel, même noté 20/20, **n'est jamais une validation machine**.

L'évaluateur visuel doit être indépendant de la session/du rôle qui a numérisé :
donner au juge l'image d'origine, la carte sémantique, l'OSP, le rendu réel et
les diagnostics CLI. Il ne doit pas recevoir la note espérée, ni les promesses
du numériseur. Si une seconde session/modèle n'est pas accessible, signaler
la limite ; pas d'autocertification « indépendante ».

## Prérequis et preuve d'exécution

1. Lire la version **main à jour** du skill [openstitch-digitize](../openstitch-digitize/SKILL.md)
   et les références utiles (segmentation, satin, directions, retouches).
2. Exécuter le binaire officiel `openstitch-cli` ; si absent, installer selon
   `scripts/install-cli-linux.sh`, vérifier l'origine et son hash, ou compiler.
   Ne pas fabriquer des rapports ou des rendus de substitution.
3. Entrées : image originale, dimensions physiques, support textile si connu,
   contraintes palette/cadre, liste explicite des **détails identitaires protégés**.
   Pour un banc d'essai autonome : sélectionner **une image de test autorisée**
   et la taille figée dans le manifeste de benchmark ; ne pas inventer de nouvelle
   image en prétendant mesurer un progrès sur l'ancienne.
4. Construire une carte sémantique fidèle : porte A, puis plan des points
   porte B. Les couleurs de segmentation ne sont pas les fils. Produire un OSP
   réel (porte C), puis contrôle détaillé (porte D).
5. Conserver **l'image source immuable** et chaque version d'OSP / DST / rendu.
   Les sorties d'expérimentation doivent vivre hors des fichiers sources du dépôt.

## Rapports minimum, pour chaque version

Depuis **le même OSP**, exécuter réellement :

```bash
openstitch-cli project inspect essai.osp --json > inspect.json
openstitch-cli project check essai.osp --json > check.json
openstitch-cli project diagnose essai.osp --json > diagnose.json
openstitch-cli project fidelity essai.osp --json > fidelity.json
openstitch-cli project density essai.osp --json > density.json
openstitch-cli project render essai.osp -o preview.png --px-per-mm 7
openstitch-cli osp2dst essai.osp broderie.dst
openstitch-cli stats broderie.dst --json > stats.json
```

Note : `project check` et `diagnose` peuvent sortir avec le code **2**
quand ils détectent des erreurs ; capturer ce code et conserver leurs rapports.
Une commande qui n'a pas réellement tourné reçoit `not_run`, jamais `pass`.
Inspecter les retours `--json` et le catalogue d'opérations de **la version
installée** ; ne jamais supposer une option prise en charge.

Le rapport d'inspection et les images sont des éléments d'appréciation pour
le juge, **pas une note calculée par fidélité pixel**. Les métriques de
couverture ne remplacent pas l'identification sémantique des objets.

## Barème commun (20 points)

| Critère | Max | Questions que le juge doit résoudre |
|---|---:|---|
| `fidelity` | 5 | Sujet lisible ? Proportions ? Instances, silhouettes et détails identitaires préservés ? |
| `stitch_choices` | 4 | Tatami, points droits, trois satins et directionnels choisis selon topologie et expression ? |
| `fabric_feasibility` | 5 | Densités, sous-couches, superpositions, tirage, risques support/stabilisateur plausibles ? |
| `technical_cleanliness` | 3 | Trop d'îlots/coupes, sauts apparents, trajets cachés, ordre de couture ? |
| `aesthetics` | 2 | Volume, contraste, rythme, qualité du rendu de fil, équilibre avec le tissu ? |
| `file_integrity` | 1 | OSP réellement éditable, DST cohérent, dimensions et cadre contrôlés ? |

Chaque sous-note s'écrit par pas de **0,5 point**, avec une observation concrète
de ≥ 20 caractères. Une bonne note globale ne permet pas de masquer un trou
structurel, l'effacement d'un oeil, d'une plume distinctive, d'un mât, etc.

**Seuil expérimental : 17/20**, sous réserve des critères bloquants.
Un contrôle `project_check` ou export DST en échec, ou la perte d'un détail
identitaire marqué `critical`, bloque la livraison. Le score affiché est
**plafonné à 9/20**, avec score brut conservé pour diagnostic. Les preuves absentes,
les commandes non exécutées ou un critique non indépendant donnent
**`incomplete` et aucune note**, pas un score arbitraire de zéro.

Un succès logiciel est libellé `software_only` tant qu'un échantillon textile
n'a pas été réellement cousu. En particulier, ne jamais confirmer l'absence de
fronces, d'usure ou de mauvaise couverture au simple vu d'un PNG.

## Contrat de l'évaluateur

Créer un `review.json` dans le dossier d'essai (chemins relatifs à ce dossier).
Renseigner **tous** les champs ci-dessous. Les fichiers sous `artifacts`
doivent réellement exister et être non vides :

```json
{
  "schema": "openstitch.evaluation/1",
  "source_id": "benchmark-001",
  "size_mm": [240, 240],
  "digitizer": "session-numerisation-001",
  "reviewer": "session-critique-002",
  "machine_tested": false,
  "artifacts": {
    "source": "source.png",
    "semantic_map": "segmentation.png",
    "osp": "broderie.osp",
    "dst": "broderie.dst",
    "preview": "preview.png",
    "check_report": "check.json",
    "diagnose_report": "diagnose.json",
    "fidelity_report": "fidelity.json",
    "density_report": "density.json"
  },
  "checks": {
    "project_check": {"status": "pass", "evidence": "Résultat du vrai check.json, aucune erreur"},
    "project_diagnose": {"status": "warning", "evidence": "Avertissements précisément documentés"},
    "dst_export": {"status": "pass", "evidence": "Export effectué à partir du même OSP"},
    "preview_render": {"status": "pass", "evidence": "Rendu généré par project render"}
  },
  "protected_details": [
    {"name": "silhouette du sujet", "preserved": true, "critical": true}
  ],
  "rubric": {
    "fidelity": {"score": 4, "evidence": "Forme globale fidèle, un petit détail perdu"},
    "stitch_choices": {"score": 3, "evidence": "Satin de l'anneau correct, direction de voile discutable"},
    "fabric_feasibility": {"score": 4, "evidence": "Densité raisonnable sur simulation, essai tissu nécessaire"},
    "technical_cleanliness": {"score": 2.5, "evidence": "Quelques petits îlots, sauts à réduire"},
    "aesthetics": {"score": 1.5, "evidence": "Bon contraste des masses mais nervure peu lisible"},
    "file_integrity": {"score": 1, "evidence": "OSP éditable et DST issus du même projet"}
  }
}
```

Valider (stdlib Python, **sans IA cachée dans le script**) :

```bash
python3 scripts/openstitch_evaluate.py essais/01/review.json -o essais/01/score.json
```

Le programme vérifie contrat, présence des fichiers, notes, preuves,
indépendance nominative, points bloquants et seuil. **Il ne regarde pas
les images et ne génère pas lui-même les sous-notes** : celles-ci doivent
venir d'une inspection visuelle réelle documentée. Les champs
`checks` sont une attestation structurée du pilote, pas une preuve
cryptographique du résultat CLI.

## Boucle itérative (une image)

1. **V0** : préparer sémantique, OSP, DST, diagnostics, aperçu.
2. **Juge** : inspection du global + zoom sur le sujet prioritaire, score JSON.
3. **Diagnostiquer la cause**, pas seulement le symptôme :
   - identité/instance perdue → reprendre segmentation A ;
   - point ou direction inadaptée → revenir au plan B ;
   - erreur moteur/OSP → reprendre C ;
   - défaut local de densité/trajet/coupe → retouche OSP puis D.
4. **Une hypothèse principale par variante**. Conserver l'ancienne version et
   les mesures ; tester les trois méthodes satin si le problème le requiert.
5. Rejuger à **source, taille, fond et conditions identiques**. Accepter une
   variante seulement si elle ne perd pas un détail protégé et améliore
   réellement la qualité ; pas de préférence automatique pour moins de points.
6. Arrêt : >= 17/20 sans bloqueur et sans régression significative, ou
   budget atteint, ou deux variantes successives sans amélioration.
   Limite suggérée : **5 variantes** ou **60 min** de calcul par image,
   à adapter à la capacité réelle, sans boucle infinie.

Garder `best.osp` / `best_score.json`, pas seulement la dernière version.
En cas de blocage, livrer la meilleure version **étiquetée non validée** et
la liste exacte des problèmes.

## Boucle inter-sessions (tâche planifiée)

**Destination imposée des résultats : branche `agent-trials`**, chemin
`experiments/runs/YYYY-MM-DD/<run_id>/result.json`. Lire au préalable
`experiments/README.md` dans cette branche. Ne **jamais** merger
`agent-trials` dans `main` ; les améliorations de code/skill passent par
une PR séparée. Enregistrer également les essais ratés ou bloqués, avec
une raison précise. Un nouvel essai possède un identifiant unique,
aucun force-push et aucune écriture sur un essai précédent.

Conserver **sur la branche seulement les métadonnées légères** (scores,
sources/hashes, versions CLI, blocages, hypothèses, rapports synthétiques,
liens d'artefacts). Mettre les OSP/DST/PNG et logs détaillés dans
des GitHub Actions artifacts si disponibles et noter leur date d'expiration ;
une conservation pérenne des binaires nécessitera un support séparé.
Si aucun artefact n'a été produit, inscrire `null`/unavailable — ne
fabriquer ni URL ni résultat.

- Utiliser un **lot de validation fixe et versionné** (benchmarks avec
  autorisation d'utilisation) ; répartir les images par familles de
  difficulté : personnages, plumes, bateaux/cordages, végétation, architecture.
- Séparer **développement** des heuristiques et **validation tenue à l'écart**.
  Ne modifier ni les images, ni les dimensions, ni les notes de référence
  pour améliorer artificiellement le score.
- Journal persistant par exécution : date UTC, commit OpenStitch, version CLI,
  hash de la source, dimensions mm, paramètres, OSP, PNG, JSON, note avant/après,
  décision motivée, coût et temps approximatif si mesurables.
- Transformer un résultat récurrent en proposition de modification de skill
  **uniquement après plusieurs échantillons indépendants** ; tester sur le
  lot témoin et faire une PR distincte, jamais réécrire main en tâche de fond.
- La tâche doit **vérifier la présence du skill sur main avant d'agir** ;
  si PR pas mergée, informer de l'attente et ne pas simuler une expérience.
- Pas de publication massive de fichiers lourds dans git : conserver les
  artefacts d'exécution dans des ressources persistantes adéquates
  (GitHub Actions artifacts, stockage autorisé), et n'émettre une PR que pour
  des changements de code/skills validés.
- En l'absence de CLI, d'images benchmark, de capacité d'exécution ou de
  second juge indépendant, enregistrer **bloqué**, la raison, et une prochaine
  action concrète. Ne jamais prétendre avoir été « entraîné » si aucune
  numérisation et évaluation réelle n'a eu lieu.

## Tests et régression

Exécuter `python3 -m unittest discover -s tests/scripts
-p 'test_openstitch_evaluate.py' -v`. Ne pas mettre à jour le barème
entre deux variantes ; versionner les changements de barème et noter
une rupture de comparabilité dans le journal. Comparer qualité des sujets,
risques de densité et taux de rejet, **pas seulement la moyenne des notes**.
