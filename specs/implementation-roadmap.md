# Feuille de route d'implémentation — backlog P0 (post-Liza)

**Créé le :** 2026-10-05 (planification après arrêt de Liza). **Révisé le :** 2026-10-06
(audit de l'état réel contre `main` `40bf4c0`, voir `docs/roadmap-parite-hatch.md`
§ *État d'avancement*). Base : le découpage de
`specs/arch-plan/vision/20260928-091642-arm-1.md` (architecture maître, approuvée,
désormais sur `main`), repris tel quel. Un seul écart assumé : **l'infrastructure i18n
(S14/S14b) est repoussée en toute fin de backlog** (décision utilisateur du 2026-10-05 : on
code en français en dur, un seul passage `tr()`/`.ts` une fois le reste fonctionnel).

**Rôle de ce fichier** : l'*ordre de livraison* (vagues, scopes, dépendances, plans).
Les **statuts** des entrées HP-* vivent uniquement dans `docs/roadmap-parite-hatch.md` ;
ne pas les recopier ici sauf pour la colonne « État », à resynchroniser à chaque audit.
Pour la conception détaillée d'un scope : section correspondante de l'architecture maître
(composants, interfaces, propriété des fichiers partagés).

## Ordre d'implémentation (vagues révisées, i18n en dernier)

Légende de l'état : ☑ fusionné sur `main` · ◐ partiel · PR = en pull request ouverte, non
fusionnée · ☐ pas commencé.

| Vague | Scope | Codes HP-* | Dépend de | État au 2026-10-06 | Plan détaillé |
|---|---|---|---|---|---|
| 0 | **S1** — Catalogue de fils | THR-001/002/003 | — | ◐ THR-001 ☑, THR-003 ☑ ; THR-002 partiel (deux nuanciers placeholder : Madeira Polyneon, Isacord 40) | `specs/plans/thread-palette-implementation.md` (livré) |
| 0 | **S11** — Récents + autosave | FILE-003/004 | — | ☑ FILE-003 et FILE-004 sur `main` | `specs/plans/autosave-implementation.md` (livré) |
| 1 | S2a — Couche machine + design importé | FMT-001 | S1 | **PR #5** (`task/s2a-machine-layer`) : `machine.hpp`, `format_registry`, tests — à relire et fusionner | à produire si besoin après fusion |
| 2 | S2b — Codec PES | FMT-002/003 | S1, S2a | ☐ (attend S2a) | — |
| 2 | S2c — Codecs JEF/EXP | FMT-004/005 | S1, S2a | ☐ (attend S2a) | — |
| 2 | S3 — Sélection/presse-papiers/transfo | OBJ-001/002/003/004/006/014/016 | S2a | ◐ **sélection multiple, Suppr universel, déplacement/duplication de la sélection livrés** (PR #6, hors du plan S3 : OBJ-001/003/016 ◐) ; **restent** presse-papiers (OBJ-002), rotation (OBJ-004), miroir (OBJ-006), transformer le design entier (OBJ-014), Ctrl+A ; **PR #3** = plan de code antérieur, à rebaser | `specs/plans/s3-selection-implementation.md` (dans la PR #3) |
| 3 | S4 — Fils du design/film couleur | THR-004/005, SEQ-003, OBJ-018 | S1, S2a, S3 | ☐ (THR-004 démarrable : S1 livré) | — |
| 3 | S5 — Tracés ouverts/satin bordure | VEC-002, STI-004 | S3 | ◐ briques via le mode Contours (`Path::closed`, `strip_polygon`) ; **aucun outil utilisateur** | — |
| 4 | S9 — Tâches longues | PERF-001, UX-006 | S2a, S3, S4 | ☐ (epic et stories rédigées dans `specs/`, zéro code) | — |
| 4 | S10 — Rendu réaliste | VIEW-001 | S2a, S3 | ☐ | — |
| 5 | S6 — Pipeline de génération | ENG-001/002/008/010, PERF-002 | S9 *(S14 retiré)* | ◐ ENG-002 partiel (sous-couche auto en auto-numérisation) ; ENG-008/010 partiels ; ENG-001 ☐ | — |
| 5 | S8 — Auto-numérisation | AUTO-001/003/009 | S9 *(S14 retiré)* | ◐ mode **Contours / Line Art** fusionné (PR #4) ; AUTO-001/009 partiels, AUTO-003 ☐ | — |
| 6 | S7 — Lettrage | TXT-001/003/004 | S3, S6, S8 *(S14 retiré)* | ☐ (aucun code texte) | — |
| 7 | S12 — Fiche de production | PROD-001 | S4, S6 *(S14 retiré)* | ☐ | — |
| 8 | S13 — Validation physique | PHYS-001 | S2a, S2b, S6, S7 | ☐ (preuve humaine requise) | — |
| 9 | S15 — Installateur | DIST-001 | (packaging final, pas de blocage dur) | ◐ installateur Inno + release sur tag + release « latest » ; **manquent** signature, associations de fichiers, runtime MSVC | — |
| **10 (fin)** | S14 — Infra traduction | I18N-001 | **tous les scopes 0-9** (balayage final) | ☐ repoussé | — |
| **10 (fin)** | S14b — Anglais complet | I18N-002 | S14 | ☐ repoussé | — |

Les vagues restantes se planifient en détail **juste avant implémentation** (pas tous les
plans maintenant : ils vieillissent avant d'être codés). Chaque plan reprend la section de
l'architecture maître sans la redéfinir.

## Prochaines actions recommandées (dans l'ordre)

1. **Relire et fusionner la PR #5 (S2a, FMT-001)** : elle débloque PES/JEF/EXP et S3. Elle a
   10 commits de retard sur `main` : fusionner `main` dedans d'abord.
2. **S3 (reste : presse-papiers, rotation, miroir, transformation du design)** : la sélection multiple et le
   mutateur unique existent (PR #6) ; le plan de la PR #3 est à rebaser sur ce modèle (`multiSelection_`,
   `CompositeCommand`, `InteractionMap`) avant fusion.
3. **S4 : THR-004/THR-005** (fil par objet, film couleur) : démarrable dès maintenant côté
   modèle (S1 livré), en parallèle de S2a/S3 pour la partie cœur.
4. **S5 : outils utilisateur de tracé ouvert et de satin à largeur fixe** : le cœur est
   largement prêt grâce au mode Contours ; il manque commandes `libs/commands` et outils desktop.
5. **Mesure plutôt que nouvelles fonctions** pour AUTO-001/009 : constituer le jeu de référence
   (10-20 images) et les métriques satin/tatami ; sans cela ces deux P0 ne peuvent pas se clore.
6. **Valider le desktop du mode Contours sous Windows** (dialogue et test QTest jamais compilés
   dans l'environnement de développement sans Qt).

## Points d'attention hérités de la revue systémique du plan maître

(Issus de la section *Systemic Decomposition Review* d'arm-1 — constats, pas des échecs.)

- **`apps/desktop/main_window.cpp`** (~324 Ko au 2026-10-06) reste le carrefour de 12 des 18
  scopes. La *Carte par fonction* d'arm-1 fixe un écrivain ou une chaîne ordonnée par fonction —
  **ne pas retoucher une fonction hors de son scope sans vérifier cette carte**.
- Les dorés `tests/golden/auto-satin/**` et le PDF de doc (`docs/build/*.pdf`) sont des
  artefacts dérivés à écrivain unique (S8 et S15) : **régénérer explicitement après chaque
  vague touchant la génération de points** ; le PDF se régénère avec
  `docs/scripts/build-docs.ps1` (PowerShell, Windows).
- Six entrées (dont HP-PHYS-001, HP-AUTO-001, HP-DIST-001) ne peuvent passer à ☑ sans preuve
  humaine (machine réelle, scans Hatch, certificat de signature) — retenue d'acceptation,
  pas un blocage de code.
- `StitchParams` gagne des champs/alternatives venus de 4 scopes (S4, S5, S6, S7) : chaque
  ajout force la mise à jour de tous les `std::visit` exhaustifs (génération, analyse,
  inspecteur, sérialisation, cache).

## Historique de l'orchestration Liza (voir `specs/liza-salvage-digest.md`)

Liza a tourné du 2026-09-28 au 2026-09-30 puis s'est arrêtée (quota). Ce qui en a été repris :
l'architecture maître (sur `main`), le code de FILE-003 (récents, fusionné via la PR #2), les
plans S1 et S11 (livrés). Les branches `task/arm-*` de Liza n'existent plus sur le dépôt
distant ; seules restent actives `task/s2a-machine-layer` (PR #5) et `docs/s3-selection-plan`
(PR #3), issues du travail en pairing manuel qui a suivi. L'ancienne branche
`feat/satin-networks-and-manual-shape-tools` (PR #1, fermée, août 2026) n'est pas fusionnée
commit par commit : vérifier son contenu avant de la supprimer.
