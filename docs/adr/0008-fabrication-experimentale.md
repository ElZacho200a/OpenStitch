# ADR 0008 — Fabrication textile expérimentale (FAB-EXP-01) : audit anti-doublon et périmètre

*État : proposé (L0 de FAB-EXP-01, 2026-11-22). Audit statique de `main` (en-têtes et docs), sans
exécution ni essai textile. Aucune décision d'algorithme ni d'API n'est prise ici.*

## Contexte

FAB-EXP-01 (Notion, Roadmap — Chantiers) vise des capacités que les remplissages actuels ne
couvrent pas : structures de fil autoportantes, déformation programmée du tissu, formes 3D par fil
rétractable, mécanismes à fil coulissant, glitch contrôlé. Sa règle d'entrée : aucun code de moteur
avant d'avoir montré un écart avec l'existant, et pas de doublon du remplissage directionnel, du
stipple, des recettes (ADR 0007) ni du rendu.

## Inventaire de l'existant (preuves : fichier)

| Besoin FAB-EXP | Existant | Verdict |
| --- | --- | --- |
| Treillis maintenu aux croisements | `CrosshatchFillParams::tie_down` (`embroidery_object.hpp`) : petite barre cousue à chaque croisement de deux couches | Couvre un treillis **régulier dans une région**, pas un graphe arbitraire |
| Réseau de traits ouverts + point droit | `autodigitize::auto_digitize_stipple` produit un objet vectoriel de chemins ouverts et un objet `Running` ; `RunningStitchParams::repeats` va jusqu'à 3 (point triple) | **Chemin de sortie réutilisable** pour un graphe généré |
| Ordre de couture d'un semis / réseau | `stipple::build_stitch_path` (tour TSP, coupe aux liaisons longues) ; `hidden_underpath` des remplissages | Ordre pour semis ; rien de spécifique à un graphe à jonctions |
| Aléa déterministe / « glitch » | `MotifFillParams::scatter` (hachage de cellule, sans graine stockée), `SatinSplit::DeterministicJitter`, `jagged_seed`, `GradientMethod::ControlledScatter` | **Les effets de dispersion existent.** Écart non démontré pour le glitch |
| Déformation du tissu | `stitch_analysis/sheet_deformation.hpp` : solveur éléments finis 2D (tissu + stabilisateur), déterministe, lecture seule, **`calibrated` faux** ; `PullMode::Predicted` | Solveur présent mais **non calibré** : L5 = calibration, pas un nouveau solveur |
| Contrôles machine | `machine_checks.hpp` (champ, cadre, point trop long, coupes), `export_limits.hpp`, delta DST ±121 unités | Réutilisables. **Aucun** contrôle de connexité du réseau cousu |
| Dentelle autoportante | `docs/roadmap-parite-hatch.md` **HP-SPEC-003** (P3, « À faire : sous-couche en treillis, contrôle de connexité des fils ») ; `docs/veille-innovations-2026-10.md` V10 | **Doublon de périmètre** avec l'axe A de FAB-EXP-01 |
| Relief / 3D | HP-STI-015 (satin relief sur mousse) | Autre technique ; ne couvre ni fil rétractable ni conception inverse |
| Validation physique | HP-PHYS-001 (protocole), HP-PHYS-002 | À réutiliser pour L2/L5, pas à recréer |
| Fonction de recherche | Aucune occurrence de « dissolution », « hydrosoluble », « autoportant » dans `libs/` ; `connected` n'apparaît que dans la segmentation d'image | Pas de génération topologique ni d'analyse de connexité de fils |

Limite de l'audit : lecture d'en-têtes et de documentation. Je n'ai pas lancé les générateurs ni
vérifié si une perturbation de l'**ordre** de couture (hors dispersion de position) existe ailleurs
dans `stitch_generation`.

## Décision proposée

1. **Axe A (structures autoportantes) : seul écart confirmé, mais à fusionner avec HP-SPEC-003.**
   Une seule entrée de suivi, pas deux. La capacité nouvelle est un **générateur de graphe**
   (réseau organique, treillis ajouré, cellule répétitive) avec **analyse de connexité**
   (composantes, ponts, nœuds fragiles) et renfort aux jonctions. Il émet des objets existants
   (objet vectoriel à chemins ouverts + `Running` triple), comme `auto_digitize_stipple` :
   aucun nouveau type dans `StitchParams` au L1, donc aucune migration OSP.
2. **Axe E (glitch) : pas de moteur.** Traiter comme préréglages/recettes sur `scatter`,
   `ControlledScatter` et `DeterministicJitter`. Rouvrir au L4 uniquement si l'inventaire de
   l'ordre de couture montre un manque réel.
3. **Axe B (déformation programmée) : existant partiel, écart réel sur la prédiction directe.**
   Voir l'analyse ci-dessous. Pas de nouveau solveur au L0 ; la calibration (HP-PHYS-001) passe
   avant toute extension.
4. **Axes C et D (rétractable, coulissant) : études de faisabilité sans code.** Un DST ne porte
   ni chauffage, ni tension, ni geste manuel ; livrable = fiche de fabrication, pas fonctionnalité.
5. **Code nouveau autorisé au L1 seulement pour l'axe A**, hors interface, déterministe (graine
   enregistrée), derrière une interface interne, sans Qt. Le choix d'une nouvelle lib et de son
   nom reste ouvert (couches : `libs/* → libs/core`).

## Analyse de l'axe B (textiles à déformation programmée)

Ce que FAB-EXP-01 demande : « motif de couture + tissu + préparation → déformation constatée »,
avec hypothèses explicites sur étirement initial, orientation, élasticité, stabilisation.

| Élément demandé | Existant (`sheet_deformation.hpp`, `docs/deformation-tissu-solveur.md`) | Écart |
| --- | --- | --- |
| Élasticité du tissu, orientation | Plaque orthotrope Q4 (chaîne/trame/cisaillement), `grain_angle`, préréglages woven/jersey/felt × none/tearaway/cutaway | Couvert, empirique |
| Effet du fil | Dipôle de tension par point, raidissement par la densité de fil | Couvert, linéaire |
| Stabilisation | Préréglages d'entoilage | Couvert, empirique |
| **Étirement initial (pré-étirement)** | Absent : « cadre fixe », pas de pré-tension (cité dans « Reste ») | **Écart** : cœur de StitchSwitch |
| **Relâchement à la sortie du cerceau** | Absent : « pas de fluage ni de relaxation » ; état final avec bord fixé | **Écart** |
| **Plis, bosses, bistabilité** | Absent : pas de flambage hors plan ; l'indice mesure la compression dans le plan | **Écart** : l'effet visé est hors plan |
| Calibration | `calibrated = false`, aucune mesure incorporée | Bloquant pour toute promesse |

Constat reproductible (CLI de `main`, carré de 60×60 mm, mêmes commandes `fabric-deformation`,
maille 1 mm, tension 0,2 N, non calibré) :

| Motif | Tissu / entoilage | Indice de plissement max | Déplacement max |
| --- | --- | --- | --- |
| Tatami plein | jersey / aucun | 7,4 | 0,53 mm |
| Tatami plein | woven / cutaway | 0,86 | 0,04 mm |
| Croisé + `tie_down` | jersey / aucun | 8,6 | 0,24 mm |
| Croisé + `tie_down` | woven / cutaway | 0,74 | 0,01 mm |
| Semis dispersé | jersey / aucun | 16,4 | 0,28 mm |
| Semis dispersé | woven / cutaway | 1,42 | 0,01 mm |

Lecture : le solveur distingue bien un tissu souple sans entoilage d'un tissu stable entoilé, et
il localise la compression au niveau des attaches du croisé. Il ne dit rien de la **forme**
obtenue (hauteur d'un pli, courbure), seulement d'un seuil de risque. Les valeurs ne sont pas
des mesures. Un point reste inexpliqué : l'aire « au-dessus du seuil » rapportée pour le tatami
sur jersey (6108 mm²) dépasse la surface cousue (3600 mm²) ; à relire avant de citer ce chiffre.

Conséquence pour le L5 : deux ajouts minimaux, dans l'ordre. (1) Pré-tension/pré-étirement et
relâchement comme conditions aux limites du solveur existant, pour prédire la **déformation
dans le plan après relâchement**. (2) Seulement ensuite, une étude de faisabilité du flambage
hors plan. Les deux restent indicatifs tant que `calibrated` est faux ; la pièce-test de
`pull-test-piece` doit recevoir des mesures de plis et de courbure avant toute extension.

## Conséquences

- La recette de FAB-EXP-01 (« un résultat impossible avec les remplissages actuels ») ne pourra
  être démontrée que par l'axe A ; il faut la **fixer avant le L1** : par exemple un réseau
  100×100 mm dont la connexité reste prouvée après dissolution, et que `Crosshatch` + `tie_down`
  ne sait pas produire (nœuds de degré > 4, topologie non réglée).
- Rien n'est « validé » sans tissu : le L2 (banc hydrosoluble) reste la condition de toute
  affirmation de solidité. Une prévisualisation de fil n'est pas une preuve.
- HP-SPEC-003 passe de « À faire » à « cadré par ADR 0008 » dans la roadmap quand cette décision
  est acceptée ; aucune entrée n'est modifiée par cet ADR.

## Questions ouvertes (à trancher avant le L1)

- Critère quantitatif de « autoportant » (traction, manipulation) et machine/fil/stabilisateur
  du banc d'essai.
- Représentation persistante du générateur (paramètres + graine dans l'OSP, ou objets seuls).
- Emplacement dans Studio (espace existant, pas de cinquième espace).
