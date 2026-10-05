# Feuille de route d'implémentation — backlog P0 (post-Liza)

**Généré le :** 2026-10-05, phase 2 (planification) après arrêt de Liza. Base :
le découpage de `specs/arch-plan/vision/20260928-091642-arm-1.md` (architecture
maître, approuvée, branche `integration`), repris tel quel — c'est un travail
solide, inutile de le refaire. Un seul écart assumé par rapport à ce plan :
**l'infrastructure i18n (S14/S14b) est repoussée en toute fin de backlog**
au lieu de faire goulot entre la vague 1 et la vague 2 (décision utilisateur
du 2026-10-05 : le plan original en faisait le plus gros lot transverse,
bloquant 6+ scopes — on code en français en dur pour l'instant, on fera un
seul passage `tr()`/`.ts` une fois le reste fonctionnel).

Pour le détail de chaque entrée HP-* (comportement Hatch attendu, critères
d'acceptation) : voir `docs/roadmap-parite-hatch.md`. Pour le détail de
conception de chaque scope (limites de composants, interfaces, propriété des
fichiers partagés) : voir `specs/arch-plan/vision/20260928-091642-arm-1.md`
(branche `integration` — absent de `main`, le récupérer via
`git show integration:specs/arch-plan/vision/20260928-091642-arm-1.md`).

## Ordre d'implémentation (vagues révisées, i18n en dernier)

| Vague | Scope | Codes HP-* | Dépend de | État | Plan détaillé |
|---|---|---|---|---|---|
| 0 | **S1** — Catalogue de fils | THR-001/002/003 | — | Archi approuvée, **0 code** | `specs/plans/thread-palette-implementation.md` |
| 0 | **S11** — Récents + autosave | FILE-003/004 | — | Archi approuvée ; FILE-003 codé non reviewé (`task/arm-1-ar-9-cp-0-code-0-r1`) ; FILE-004 à planifier | `specs/plans/autosave-implementation.md` |
| 1 | S2a — Couche machine + design importé | FMT-001 | S1 | Non commencé | à produire quand la vague 0 est mergée |
| 2 | S2b — Codec PES | FMT-002/003 | S1, S2a | Non commencé | — |
| 2 | S2c — Codecs JEF/EXP | FMT-004/005 | S1, S2a | Non commencé | — |
| 2 | S3 — Sélection/presse-papiers/transfo | OBJ-001/002/003/004/006/014/016 | S2a | Non commencé | — |
| 3 | S4 — Fils du design/film couleur | THR-004/005, SEQ-003, OBJ-018 | S1, S2a, S3 | Non commencé | — |
| 3 | S5 — Tracés ouverts/satin bordure | VEC-002, STI-004 | S3 | Non commencé | — |
| 4 | S9 — Tâches longues | PERF-001, UX-006 | S2a, S3, S4 | Non commencé | — |
| 4 | S10 — Rendu réaliste | VIEW-001 | S2a, S3 | Non commencé | — |
| 5 | S6 — Pipeline de génération | ENG-001/002/008/010, PERF-002 | S9 *(S14 retiré)* | Non commencé | — |
| 5 | S8 — Auto-numérisation | AUTO-001/003/009 | S9 *(S14 retiré)* | Non commencé | — |
| 6 | S7 — Lettrage | TXT-001/003/004 | S3, S6, S8 *(S14 retiré)* | Non commencé | — |
| 7 | S12 — Fiche de production | PROD-001 | S4, S6 *(S14 retiré)* | Non commencé | — |
| 8 | S13 — Validation physique | PHYS-001 | S2a, S2b, S6, S7 | Non commencé | — |
| 9 | S15 — Installateur | DIST-001 | (packaging final, pas de blocage dur) | Non commencé | — |
| **10 (fin)** | S14 — Infra traduction | I18N-001 | **tous les scopes 0-9** (balayage final) | Non commencé, repoussé | — |
| **10 (fin)** | S14b — Anglais complet | I18N-002 | S14 | Non commencé, repoussé | — |

Les vagues 1-9 restent à planifier en détail **juste avant implémentation**
(pas toutes maintenant — évite de produire 14 plans détaillés qui vieillissent
avant d'être codés). Chaque plan détaillé reprendra la section correspondante
de l'architecture maître (Components, Interfaces, Shared-File Ownership) sans
la redéfinir.

## Points d'attention hérités de la revue systémique du plan maître

(Issus de la section *Systemic Decomposition Review* d'arm-1 — constats, pas
des échecs, à garder en tête pendant l'implémentation :)

- **`apps/desktop/main_window.cpp`** (~311 Ko) reste le carrefour de 12 des 18
  scopes. La carte par fonction (section *Carte par fonction* d'arm-1) fixe un
  écrivain ou une chaîne d'écrivains ordonnée par fonction — **ne pas
  retoucher une fonction hors de son scope sans vérifier cette carte**.
- Les dorés `tests/golden/auto-satin/**` et le PDF de doc (`docs/build/*.pdf`)
  sont des artefacts dérivés à écrivain unique (S8 et S15 respectivement) :
  s'ils fusionnent avant S6/S7/S8/S12/S13, ils décrivent un état antérieur
  sans qu'aucun test ne le signale. **Régénérer explicitement après chaque
  vague touchant la génération de points.**
- Six entrées (parmi lesquelles HP-PHYS-001, HP-AUTO-001, HP-DIST-001) ne
  peuvent passer à ☑ sans une preuve humaine non fournie par Liza (machine
  réelle, scans Hatch, certificat de signature) — pas un blocage de code, une
  retenue d'acceptation finale.
- `StitchParams` gagne des champs/alternatives venus de 4 scopes (S4, S5, S6,
  S7) : chaque ajout force la mise à jour de tous les `std::visit` exhaustifs
  (génération, analyse, inspecteur, sérialisation, cache). À vérifier à chaque
  vague qui touche `StitchParams`.

## Ce qui est déjà récupérable (voir `specs/liza-salvage-digest.md` pour le détail)

- `task/arm-1-ar-9-cp-0-code-0-r1` : code réel pour HP-FILE-003, à reviewer et
  merger (phase 3).
- `task/arm-1-ar-9-cp-0-code-1-r1` : vide, rien à récupérer.
