---
name: openstitch-studio-ux
description: >-
  Auditer et corriger l'ergonomie d'OpenStitch Studio (menus, panneaux,
  popups, clavier, accessibilite, QML). A utiliser pour toute demande
  d'audit UX/UI, de nettoyage d'interface, ou avant de modifier un fichier
  QML de apps/desktop_v2/studio/qml/. Integre la methode des skills Qt
  officiels (qt-ui-design, qt-qml-review) au chantier Notion QA-STUDIO-001 /
  Registre UX/UI V2 existant : ne remplace ni ne duplique son registre.
---

# OpenStitch Studio — audit et correction UX/UI

Ce skill adapte deux skills publics du Qt Company (voir §5 Sources) au
contexte OpenStitch : il ne les recopie pas, il en extrait les quelques
regles qui s'appliquent a notre codebase QML et les relie au registre QA
deja existant. Charger ce skill **avant** toute modification QML dans
`apps/desktop_v2/studio/qml/` pour une demande d'ergonomie, et avant
d'annoncer un defaut UX comme nouveau (le registre QA-STUDIO-001 en
connait deja 1050).

## 0. Ne pas dupliquer le registre existant

Avant tout audit : lire `qa/checklist.csv` (1050 controles, 43 familles —
`LAY` disposition, `CTL` controles, `POP` popups/menus, `SPACE` espaces,
`POL` polish/coherence, `KEY` clavier, `A11Y` accessibilite) et le
[Registre UX/UI V2](https://app.notion.com/p/411bb207e2da810187f2e6d97961e6ff)
sur Notion. Un defaut que ce skill fait remonter correspond presque
toujours a un ID deja present : rattacher la preuve a cet ID plutot que
creer un doublon. N'ouvrir une nouvelle ligne que si aucun ID existant ne
couvre le constat.

Avant de marquer un ID `PASS`, **executer** le test concerne (QTest/Qt
Quick Test, `QT_QPA_PLATFORM=offscreen`) — la seule lecture de code ne
suffit pas (cf. §12 de la mission d'audit, et l'incident ou plusieurs
lignes du registre citaient un commit jamais fusionne).

## 1. Installation des skills Qt officiels — limite connue de cet environnement

Source : [TheQtCompanyRnD/agent-skills](https://github.com/TheQtCompanyRnD/agent-skills)
(licence `LicenseRef-Qt-Commercial OR BSD-3-Clause`). L'installation via
`/plugin marketplace add TheQtCompanyRnD/agent-skills` puis
`/plugin install` **est bloquee par le classifieur auto-mode de Claude
Code** dans cet environnement (categorie « Undermining Oversight » :
modifier la configuration de l'outil lui-meme). Ne pas retenter cette
installation automatiquement ; si elle est vraiment necessaire, le
demander explicitement a l'utilisateur, qui peut l'executer lui-meme via
`claude plugin marketplace add TheQtCompanyRnD/agent-skills --scope
project` puis `claude plugin install qt-development-skills --scope
project`. Idem pour toute execution du linter autonome
`qt_qml_lint.py` du skill `qt-qml-review` (telecharger-puis-executer un
script tiers est bloque pour la meme raison) : appliquer ses regles
**manuellement** via Grep/Read (§3), pas en executant le script.

La lecture seule des fichiers `SKILL.md`/`references/*.md` de ce depot
(via `gh api repos/.../contents/...`) n'est pas bloquee et est la source
des regles condensees ci-dessous.

## 2. Methode d'audit (qt-ui-design, adapte)

Checklist etendue desktop (section 5 du skill `qt-ui-design`), appliquee
a un espace Studio a la fois (Image/Segmentation/Vecteurs/Points) :

- **Mouvement** : animations <= 400 ms, fonctionnelles (pas decoratives),
  chemin "reduced motion" existant (`reducedMotion` deja cable, voir
  `PreferencesDialog.qml`).
- **Typographie** : texte de corps >= 16 px (`Theme.bodySize`), au plus
  3-4 tailles actives par ecran.
- **Clavier** : chaque controle interactif atteignable et utilisable au
  clavier seul ; focus visible ; pas de piege (Echap doit toujours sortir
  d'un popup/modale) ; un champ texte/numerique/slider focalise doit
  garder les fleches/Suppr/Echap (`window.keyboardOwnedByControl` dans
  `Main.qml` — ne pas le casser).
- **Couleur** : tokens `Theme.*` uniquement, jamais de couleur en dur ;
  tout changement d'etat est aussi porte par une icone/un texte, pas
  seulement une couleur.
- **Localisation** : `qsTr()` partout, pas de format de date/nombre en
  dur ; conteneurs tolerants a +30-40% de longueur de texte.

Classement des constats (reutiliser ces 3 niveaux, puis les relier a la
severite S1-S3 du registre QA) :

| Niveau qt-ui-design | Equivalent registre QA |
|---|---|
| Critical (loi UX ou WCAG violee) | S1 / P0-P1 |
| Warning (friction, charge cognitive) | S2 / P2 |
| Opportunity (amelioration) | S3 / P3 |

## 3. Revue de code QML (qt-qml-review, adapte)

Regles condensees les plus pertinentes pour notre codebase (liste
complete : voir §5). Appliquer par `Grep` cible, pas par lecture
exhaustive de chaque fichier.

**Layout (cause frequente des defauts `LAY-*`/`SPACE-*` du registre) :**
- `anchors.*` et `Layout.*` ne se melangent jamais sur le meme item.
- Un panneau dont le contenu peut deborder (liste de calques, inspecteur,
  menu long) a besoin d'un `Controls.ScrollView`/`Flickable` — voir le
  correctif deja applique a `VectorSpace.qml` et `FilePanel.qml` (PR #291,
  #317) comme reference de patron a reproduire ailleurs (ex. `Inspector.qml`
  si LAY-023/POL-021 volet Points est repris).
- Un popup/menu doit se repositionner depuis `Overlay.overlay` avec un
  recalcul a l'ouverture (`onAboutToShow`), jamais une position fixe —
  patron de reference : `StudioComboBox.qml`.

**Performance (pertinent pour POL-010 et le chantier PERF-STUDIO) :**
- `Rectangle { color: "transparent" }` pour grouper sans fond -> `Item`.
- `clip: true` hors ListView : verifier que c'est necessaire.
- `opacity: 0` sans animation -> `visible: false`.

**Bindings et logique (A1, A7 du skill) :**
- Toute ecriture imperative (`control.prop = value`) detruit un binding
  declaratif existant ; verifier qu'il n'y en avait pas un.
- Chercher les reglages/handlers declares mais jamais lus/appeles
  (« declare mais non cable ») — cause frequente de regressions
  silencieuses apres un refactor de panneau.

## 4. Tests (qt-qml-test / qt-qml-test-run, adapte)

Le depot utilise deja QTest/Qt Quick Test offscreen (voir
`openstitch-build-test`) ; ne pas introduire d'autre framework. Pour
chaque correctif UX :

1. Localiser la suite existante qui charge le composant touche
   (`tests/unit/desktop_v2/test_studio_*.cpp`, `grep -rl <ComponentName>`).
2. Ajouter un cas plutot qu'un nouveau fichier quand c'est possible.
3. Construire la cible precise (`cmake --build build/msvc --target
   test_desktop_v2_studio_<x> --config Debug`), jamais la suite complete
   en iteration.
4. Executer avec `QT_QPA_PLATFORM=offscreen` et `QT_ROOT/bin` sur le
   `PATH` (sinon `Qt6Qmld.dll` introuvable) ; utiliser `-o <fichier>,txt`
   pour recuperer une sortie fiable (la redirection stdout simple de
   PowerShell perd parfois la sortie de QTest).

## 5. Profilage (qt-qml-profiler, reference seulement)

Non couvert par ce skill : le chantier `PERF-STUDIO-2026-11-10` (Notion)
a deja un protocole de mesure propre (corpus, p50/p95/p99, DoD). Ne pas
reprofiler ad hoc depuis ce skill ; s'y referer.

## 6. Rapport

Reutiliser le format standard `docs/agents/notion-protocol.md` §5 (Tache/
Agent/Bail/Objectif/Branche/Perimetre/Faits demontres/Tests/CI/Studio/
Textile/PR/Decisions/Blocage/Prochaine action/Relecteur). Mettre a jour
`qa/checklist.csv` (statut, sha, preuve, anomalie, pr) pour chaque ID
touche, puis le [Registre UX/UI V2](https://app.notion.com/p/411bb207e2da810187f2e6d97961e6ff).
Ne jamais marquer `PASS`/`Valide` sans execution reelle du test.

## 7. Sources (reference, non dupliquees)

- [qt-ui-design](https://github.com/TheQtCompanyRnD/agent-skills/tree/main/skills/qt-ui-design) — principes de design, checklist d'audit §5.
- [qt-qml-review](https://github.com/TheQtCompanyRnD/agent-skills/tree/main/skills/qt-qml-review) — `references/qt-qml-review-checklist.md` (regles deterministes IMP/ORD/BND/LAY/LDR/DEL/STA/IMG/PRF/STY/SIG/ERR/JS) et `references/agent-rules.md` (regles semantiques A1-A7).
- [qt-qml](https://github.com/TheQtCompanyRnD/agent-skills/tree/main/skills/qt-qml), [qt-qml-test](https://github.com/TheQtCompanyRnD/agent-skills/tree/main/skills/qt-qml-test), [qt-qml-test-run](https://github.com/TheQtCompanyRnD/agent-skills/tree/main/skills/qt-qml-test-run), [qt-qml-profiler](https://github.com/TheQtCompanyRnD/agent-skills/tree/main/skills/qt-qml-profiler) — non consultes en detail dans cette version (couverts par l'outillage deja existant du depot, §4-5).
- Commit de reference au moment de l'ecriture de ce skill : `TheQtCompanyRnD/agent-skills` HEAD au 2026-11-14 (non epingle — relire si le comportement decrit ici semble perime).
