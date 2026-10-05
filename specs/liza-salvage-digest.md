# Digest de sauvetage Liza — Phase 1

**Généré le :** 2026-10-04 (session manuelle, post-arrêt Liza)
**But :** inventaire factuel de ce que l'orchestration multi-agent Liza a produit entre le
2026-09-28 et son arrêt le 2026-09-30 (épuisement de quota Claude, jamais repris), avant de
reprendre le backlog P0 en pairing manuel. Matière brute pour la phase 2 (planification),
pas une analyse ni une recommandation.

## 1. Intention de départ

`specs/vision.md` donnait pour objectif de combler les 42 entrées **P0** restantes du
backlog de parité Hatch (`docs/roadmap-parite-hatch.md`) : formats machine (PES/JEF/EXP),
fils/couleurs, texte, moteur de points, auto-numérisation, édition d'objets, film
couleur/séquençage, rendu réaliste, fiche de production, fichiers récents + autosave,
tâches de fond, i18n, installateur Windows, protocole de validation physique — sous les
contraintes non négociables du projet (Apache-2.0 strict, cœur sans Qt vérifié par
`linux-core`, µm entiers, mutation uniquement via `ICommand`, séquence de points
uniquement via `effective_sequence`, déterminisme).

Liza a tourné du 2026-09-28 au 2026-09-30T21:26 UTC, puis s'est arrêtée net sur un
épuisement de quota de session Claude (`.liza/provider-quota-exhausted-claude`), sans
reprise depuis.

## 2. Découpage prévu (statu quo)

`arm-1` (architecture maître, **mergée**, `specs/arch-plan/vision/20260928-091642-arm-1.md`,
commit `700414c`) a éclaté le backlog en **18 scopes spécialisés**, chacun devenant une
tâche `arm-1-ar-<N>` :

| Scope | Tâche | Codes HP-* | Résumé | Statut |
|---|---|---|---|---|
| S1 — Catalogue de fils | ar-0 | THR-001/002/003 | thread_palette : ThreadKey, nuanciers, CIEDE2000 | **Architecture approuvée**, jamais consommée |
| S2a — Couche machine + design importé | ar-1 | FMT-001 | Normalisation machine, ColorBlock, nature importée | Non commencé |
| S2b — Codec PES | ar-2 | FMT-002/003 | Codec PES/PEC | Non commencé |
| S2c — Codecs JEF/EXP | ar-3 | FMT-004/005 | Codecs JEF+/EXP | Non commencé |
| S3 — Sélection/presse-papiers/transfo | ar-4 | OBJ-001/002/003/004/006/014/016 | Multi-sélection, presse-papiers, opérations affines | Non commencé |
| S4 — Fils du design/film couleur | ar-5 | THR-004/005, SEQ-003, OBJ-018 | Fil par objet, film couleur | Non commencé |
| S5 — Tracés ouverts/satin bordure | ar-6 | VEC-002, STI-004 | Tracés ouverts, satin de bordure | Non commencé |
| S9 — Tâches longues | ar-7 | PERF-001, UX-006 | Infra progression/annulation | Non commencé |
| S10 — Rendu réaliste | ar-8 | VIEW-001 | Rendu réaliste des points | Non commencé |
| S11 — Récents + autosave | ar-9 | FILE-003/004 | Fichiers récents + autosave/récupération | **Architecture mergée** ; code partiellement récupérable (§3) |
| S14 — Infra traduction | ar-10 | I18N-001 | Infra i18n, langue source ADR | Non commencé |
| S6 — Pipeline de génération | ar-11 | ENG-001/002/008/010, PERF-002 | Sous-couche, bornes min/max, cache incrémental | Non commencé |
| S7 — Lettrage | ar-12 | TXT-001/003/004 | Objet texte, lettrage FreeType/HarfBuzz | Non commencé |
| S8 — Auto-numérisation | ar-13 | AUTO-001/003/009 | Clic-pour-broder, choix satin/tatami | Non commencé |
| S12 — Fiche de production | ar-14 | PROD-001 | Fiche de production imprimable | Non commencé |
| S13 — Validation physique | ar-15 | PHYS-001 | Protocole de test physique | Non commencé |
| S15 — Installateur | ar-16 | DIST-001 | Installateur Windows signé | Non commencé |
| S14b — Anglais complet | ar-17 | I18N-002 | Traduction anglaise complète + gate de complétude | Non commencé |

16 scopes sur 18 n'ont jamais dépassé le stade de stub (`DRAFT_ARCHITECTURE`) — c'est le
backlog intact, pas un échec. Seuls S1 (ar-0) et S11 (ar-9) ont eu une architecture
spécialisée approuvée.

## 3. Ce qui est réellement récupérable

- **`specs/arch-plan/vision/20260928-091642-arm-1.md`** (1268 lignes) — design transversal
  complet : graphe de dépendances entre libs, table d'interfaces AI-01..AI-17, ownership
  des sections de `main_window.cpp`, ordre de vagues, écarts assumés vs EP-001. Solide,
  réutilisable quel que soit ce qu'on refait dessous.
- **`specs/arch-plan/vision/20260929-110240-arm-1-ar-0.md`** (358 lignes) — design de
  `libs/thread_palette` (ThreadKey, Thread, ThreadChart, politique de sourcing des
  nuanciers, CIEDE2000). Zéro code écrit contre.
- **`specs/arch-plan/vision/20260929-102601-arm-1-ar-9.md`** (413 lignes) — design complet
  fichiers récents (`apps/desktop/recent_files.hpp/.cpp`, clé `QSettings` `recent/files`,
  dédup/troncature 10) et autosave (`apps/desktop/autosave.hpp/.cpp`, créneaux sous
  `AppDataLocation/autosave/`, timer 120s, jamais d'association automatique au fichier
  utilisateur). Invariant central documenté : ne jamais écraser le fichier de l'utilisateur.
- **`specs/plans/vision/20260930-091520-arm-1-ar-9-cp-0.md`** — plan de code mergé pour
  HP-FILE-003 (fichiers récents).
- **Branche `task/arm-1-ar-9-cp-0-code-0-r1`** (worktree
  `.worktrees/arm-1-ar-9-cp-0-code-0-r1`) — **code réel, complet, non reviewé** : 342 lignes
  / 8 fichiers (`recent_files.hpp/.cpp`, intégration `empty_state_widget` + `main_window`
  avec le fix de réentrance `QTimer::singleShot(0, …)`, `CMakeLists.txt`, 4 nouveaux tests
  Qt dans `test_main_window.cpp`). Implémente HP-FILE-003 de bout en bout.
  **Candidat direct à review-et-merge.**
- **Branche `task/arm-1-ar-9-cp-0-code-1-r1`** — en réalité **vide** (HEAD = commit de
  base, rien committé). Rien à récupérer malgré le statut `DRAFT_CODE`.
- **`specs/plans/vision/20260930-185033-arm-1-ar-9-cp-1.md`** — plan de code pour l'autosave
  (HP-FILE-004), conçu et approuvé par un reviewer avant que le bug de blocage (ci-dessous)
  ne frappe. Texte de conception récupérable, zéro code écrit contre. **À relire
  intégralement avant de l'utiliser** (non relu en détail en phase 1, seulement via les
  citations dans `.liza/state.yaml`).

## 4. Échecs et causes racines

- **Bug d'outillage récurrent Liza (pas un défaut OpenStitch)** : format d'en-tête
  « Source revision » mal respecté par les agents → worktree/branche détruite à chaque
  fois, récupérée sur des branches `preserved/*` (`preserved/arm-1-ar-0-20260929`,
  `preserved/arm-1-ar-0-20260929-attempt2`, `preserved/arm-1-ar-9-20260929`).
  L'orchestrateur a lui-même qualifié ça de « defect récurrent, pas un incident isolé ».
- **Les 4 rejets de `arm-1`** : même classe de défaut répétée — l'architecte corrigeait
  l'instance signalée (une arête de dépendance/interface manquante entre deux scopes) sans
  jamais balayer tout le plan pour la classe complète ; le reviewer suivant trouvait
  l'instance suivante.
- **Superseding de `code-0`/`code-1`** : conflit de provenance de carrier — une référence
  non épinglée vers le doc d'archi ar-9 remontait à la fois le commit canonique et un
  commit orphelin (issu du bug d'en-tête ci-dessus) avec un contenu différent au même
  chemin. Bug d'outillage, pas de conception.
- **Blocage de `cp-1`** : le plan cite le doc d'archi ar-9 au commit *avant* sa fusion, qui
  ne contient pas encore le fichier — bug de pinning de commit côté Liza.
- **Rejet isolé de l'architecture S11** : un vrai défaut de contenu (pas le pattern
  ci-dessus) — le scope S11a (fichiers récents) omettait `main_window.hpp` de sa liste de
  composants alors que le design y ajoute un membre et une méthode. Corrigé par ajout du
  fichier manquant à la déclaration de scope.
- **Cycle de review de `cp-0`** : défaut de réentrance détecté *au stade du plan*, avant
  tout code — `refreshRecentFilesUi()` était initialement appelé de façon synchrone depuis
  l'intérieur même du handler de clic qui venait de le déclencher. Corrigé par
  `QTimer::singleShot(0, …)`, confirmé implémenté dans `code-0-r1`.

## Chemins clés pour la phase 2

- `specs/vision.md`, `specs/arch-plan/vision/20260928-091642-arm-1.md`
- `.worktrees/arm-1-ar-0/specs/arch-plan/vision/20260929-110240-arm-1-ar-0.md`
- `.worktrees/arm-1-ar-0/specs/arch-plan/vision/20260929-102601-arm-1-ar-9.md`
- `.worktrees/arm-1-ar-9-cp-0-code-0-r1/specs/plans/vision/20260930-091520-arm-1-ar-9-cp-0.md`
- Branche `task/arm-1-ar-9-cp-0-code-0-r1` (diff réel 342 lignes, `CODE_TO_REVIEW`) —
  candidat de sauvetage prioritaire
- Branche `task/arm-1-ar-9-cp-0-code-1-r1` (vide, rien à sauver)
- `specs/plans/vision/20260930-185033-arm-1-ar-9-cp-1.md` (design approuvé, à relire)
- `.liza/state.yaml` lignes 575-660 (RCA rejets arm-1), 5493-6187 (tâche archi ar-9),
  6187-7901 (cp-0, dont review réentrance 7548-7575), 7902-9581 (cp-1, dont blocage 7943),
  9582-11304 (code-0/code-1 superseded + code-0-r1/code-1-r1)
