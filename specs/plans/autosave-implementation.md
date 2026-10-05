# Coding Plan: S11b — Sauvegarde automatique et récupération (HP-FILE-004)

Status: draft (plan reconstruit — voir note)

> Le document de plan original de ce scope a été perdu avant d'être committé
> (jamais atteint git ; ne survit que paraphrasé dans l'état de
> l'orchestration, voir `specs/liza-salvage-digest.md`). Ceci est un plan
> neuf, construit directement depuis l'architecture approuvée
> (`specs/arch-plan/vision/20260929-102601-arm-1-ar-9.md`, branche
> `integration`), le diff réel de `task/arm-1-ar-9-cp-0-code-0-r1` (tâche
> sœur HP-FILE-003, pour que les deux atterrissent dans les mêmes fichiers
> sans conflit), et l'état actuel de `apps/desktop/main_window.hpp/.cpp` sur
> `main`.

## 1. Scope recap

Livre HP-FILE-004 : un autosave piloté par `QTimer` (~120 s) qui écrit un
instantané du `document::Project` courant vers
`QStandardPaths::AppDataLocation/autosave/<slug>.osp` (+ un sidecar JSON) dès
que `isWindowModified()` est vrai et que le document n'est pas vide — jamais
dans le fichier de l'utilisateur, jamais via `setCurrentProjectPath`. Au
démarrage, les créneaux orphelins (laissés par une sortie anormale)
déclenchent un dialogue de récupération ; accepter charge le contenu comme
document **sans nom** (`currentProjectPath_` vide, `isWindowModified() ==
true`) et purge le créneau ; refuser purge aussi sans charger. Un
`closeEvent` propre arrête le timer et purge le créneau courant.

**Exclusions explicites** : pas d'infrastructure de tâche de fond (écriture
synchrone sur le thread UI via `project_io::save_project`, per
`CLAUDE.md#Architecture` — « No async task infrastructure exists yet ») ;
pas de réglage d'intervalle (120000 ms fixe) ; aucun changement à
`libs/project_io` ni à `kSchemaVersion` ; pas de vignette ; le document
récupéré n'est **jamais** réassocié à son chemin d'origine ni au créneau
autosave lui-même (invariant, voir §4).

## 2. Nouveaux fichiers

- `apps/desktop/autosave.hpp` / `.cpp` (nouveau). Module fonctions pures +
  I/O fine, même découpage que `recent_files.hpp`/`ai_preferences.hpp` (gabarit
  cité par l'architecture) : dérivation de slug/chemin (pure), écriture
  `.osp`+sidecar, suppression best-effort, scan des orphelins. Aucune
  mutation de `document::Project`.
- `apps/desktop/CMakeLists.txt` (modif additive) : ajouter `autosave.cpp`/
  `autosave.hpp` à la liste de sources d'`openstitch_desktop_widgets`, à côté
  de `recent_files.cpp`/`.hpp`.

## 3. Types et API

**Fonctions libres + deux structs — pas de classe.** Le texte de
l'architecture utilise un raccourci `autosave_.method()` (laissant croire à
une instance), mais sa propre liste Boundary/Expose ne nomme que des
fonctions libres et une struct, exactement comme `recent_files.hpp` (cité
comme précédent direct). Une classe ajouterait une couche d'état (un cache
« créneau courant ») jamais nécessaire : le créneau est toujours une fonction
pure de `currentProjectPath_`, donc recalculé à la demande plutôt que stocké.

```cpp
// apps/desktop/autosave.hpp
#pragma once
#include <QDateTime>
#include <QString>
#include <vector>
#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::desktop {

struct AutosaveSlot {
    QString osp_path;
    QString sidecar_path;
};

struct AutosaveCandidate {
    AutosaveSlot slot;
    QString original_path;  // vide -> "Projet sans nom" (document jamais enregistré)
    QDateTime saved_at;      // invalide si le sidecar est absent/corrompu
};

// Hachage (SHA-1 hex, QCryptographicHash) du chemin CANONIQUE de currentProjectPath,
// ou "untitled-<pid>" si currentProjectPath est vide (AD-S11-1). Pure.
[[nodiscard]] QString autosaveSlug(const QString& currentProjectPath);

// Résout les deux chemins sous AppDataLocation/autosave/<slug>.{osp,json}. Pure
// (ne crée pas le dossier -- writeAutosave le fait au moment d'écrire).
[[nodiscard]] AutosaveSlot slotFor(const QString& currentProjectPath);

// project_io::save_project(slot.osp_path, project) puis sidecar JSON
// {"original_path", "saved_at" (ISO 8601 UTC)} via QSaveFile. Le Result<void>
// retourné reflète l'écriture .osp ; un échec du sidecar seul est avalé
// (best-effort -- au pire un chemin d'origine périmé au prochain démarrage,
// jamais une perte de données, cf. architecture §Invariant central).
[[nodiscard]] Result<void> writeAutosave(const AutosaveSlot& slot,
                                         const document::Project& project,
                                         const QString& originalPathForDisplay);

// Supprime les deux fichiers s'ils existent ; no-op silencieux sinon.
void discardAutosave(const AutosaveSlot& slot);

// Énumère autosave/*.osp (tous PID), lit chaque sidecar si présent.
[[nodiscard]] std::vector<AutosaveCandidate> scanForRecoverableAutosaves();

} // namespace openstitch::desktop
```

## 4. Intégration MainWindow

**`apps/desktop/main_window.hpp`** (additif uniquement, aucune ligne déjà
touchée par le diff FILE-003 n'est re-touchée) :
- Nouveau membre près de `QTimer* simTimer_{nullptr};` :
  `QTimer* autosaveTimer_{nullptr};` — même gabarit de construction.
- Nouveaux `private slots:` : `void onAutosaveTick();` et
  `void checkAutosaveRecovery();`.
- `#include "autosave.hpp"` dans `main_window.cpp` (pas le header).

**`apps/desktop/main_window.cpp`** — deltas exacts, réconciliés avec le diff
FILE-003 déjà sur `task/arm-1-ar-9-cp-0-code-0-r1` (base sur laquelle cette
tâche atterrit) :

1. **Constructeur**, juste avant le `updateActions();` final (après le bloc
   récents de FILE-003) :
   ```cpp
   autosaveTimer_ = new QTimer(this);
   autosaveTimer_->setInterval(120000); // ASM-S11-01 : 2 min, non configurable en P0
   connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::onAutosaveTick);
   autosaveTimer_->start();
   // Différé après le premier passage de la boucle d'événements (post window.show())
   // pour que le dialogue de récupération s'affiche au-dessus d'une fenêtre déjà visible.
   QTimer::singleShot(0, this, &MainWindow::checkAutosaveRecovery);
   ```

2. **`setCurrentProjectPath`** — ajouter **une ligne en tout début**, avant
   `currentProjectPath_ = file;` (lire l'ANCIEN chemin avant qu'il soit
   écrasé) :
   ```cpp
   void MainWindow::setCurrentProjectPath(const QString& file) {
       // HP-FILE-004 : le créneau autosave suit l'identité du document — un
       // changement d'identité abandonne le créneau de l'ancienne (slotFor est
       // pur, recalculé depuis currentProjectPath_ à chaque usage).
       discardAutosave(slotFor(currentProjectPath_));
       currentProjectPath_ = file;
       updateWindowTitle();
       if (!file.isEmpty()) { /* bloc FILE-003 inchangé */ }
   }
   ```
   Couvre les 3 appelants existants (`resetDocumentState`,
   `saveProjectToPath`, `openProjectFile`) sans nouveau site d'appel.

3. **`closeEvent`** — ajouter à la fin :
   ```cpp
       if (event->isAccepted()) {
           autosaveTimer_->stop();
           discardAutosave(slotFor(currentProjectPath_));
       }
   }
   ```

4. **Nouveau slot `onAutosaveTick()`** :
   ```cpp
   void MainWindow::onAutosaveTick() {
       const bool empty = !project_.hasImage() && project_.vector_objects.empty() &&
                           project_.embroidery_objects.empty(); // garde de updateEmptyState
       if (!isWindowModified() || empty) {
           return;
       }
       const auto written = writeAutosave(slotFor(currentProjectPath_), project_, currentProjectPath_);
       if (!written) {
           statusBar()->showMessage(tr("Sauvegarde automatique impossible : %1")
                                        .arg(QString::fromStdString(written.error().message)));
       }
   }
   ```

5. **Nouveau slot `checkAutosaveRecovery()`** :
   ```cpp
   void MainWindow::checkAutosaveRecovery() {
       for (const auto& candidate : scanForRecoverableAutosaves()) {
           QMessageBox box(QMessageBox::Warning, tr("Récupération après un arrêt anormal"),
                           candidate.original_path.isEmpty()
                               ? tr("Un projet sans nom non enregistré a été retrouvé (%1).")
                                     .arg(candidate.saved_at.toLocalTime().toString())
                               : tr("Une sauvegarde automatique de « %1 » a été retrouvée (%2).")
                                     .arg(QFileInfo(candidate.original_path).fileName(),
                                          candidate.saved_at.toLocalTime().toString()),
                           QMessageBox::NoButton, this);
           auto* recoverBtn = box.addButton(tr("Récupérer"), QMessageBox::AcceptRole);
           recoverBtn->setObjectName(QStringLiteral("action_autosaveRecover"));
           box.addButton(tr("Ignorer"), QMessageBox::RejectRole)
               ->setObjectName(QStringLiteral("action_autosaveIgnore"));
           box.exec();
           if (box.clickedButton() == recoverBtn) {
               auto loaded = project_io::load_project(
                   std::filesystem::path(candidate.slot.osp_path.toStdWString()));
               if (!loaded) {
                   QMessageBox::warning(this, tr("Récupération impossible"),
                                        QString::fromStdString(loaded.error().message));
               } else {
                   applyLoadedProject(std::move(*loaded));
                   // PAS de second setCurrentProjectPath(QString()) explicite : applyLoadedProject
                   // appelle déjà resetDocumentState(), qui appelle déjà
                   // setCurrentProjectPath(QString()) (main_window.cpp:798) -- un second appel ici
                   // serait un 4e site d'appel redondant pour la même valeur (voir §8).
                   setWindowModified(true);
               }
           }
           discardAutosave(candidate.slot); // traité (récupéré ou ignoré) -> jamais reproposé
       }
   }
   ```
   Note : pas de garde `confirmDiscardChanges` avant le chargement — à ce
   point (différé juste après la construction), le document est toujours le
   défaut neuf, jamais modifié.

Aucune ligne de `buildMenus`, `saveProjectToPath`, `openProjectFile`, ou du
code récents de FILE-003 n'est touchée — entièrement additif.

## 5. Tests (`tests/unit/desktop/test_main_window.cpp`, `private slots:` additifs)

Réutilise l'accès `friend class MainWindowTest;` existant pour appeler
`window.onAutosaveTick()` / `window.checkAutosaveRecovery()` directement, et
les fonctions libres `scanForRecoverableAutosaves()`/`slotFor(...)`
directement (pas de friend nécessaire).

- `autosaveTickWritesASeparateFileAndLeavesTheUserFileUntouched`
- `autosaveTickSkipsWhenDocumentUnmodifiedOrEmpty`
- `autosaveRecoveryAcceptLoadsAsUntitledDocumentAndPurgesSlot`
- `autosaveRecoveryIgnoreDiscardsSlotWithoutLoading`
- `cleanCloseDiscardsTheCurrentAutosaveSlot`

(détail des scénarios : voir le rapport de conception — chaque nom de test
ci-dessus prouve directement un des critères d'acceptation de HP-FILE-004.)

## 6. Docs à mettre à jour

- `docs/roadmap-parite-hatch.md` — passer HP-FILE-004 à `☑ Fait (<date>)`,
  ajouter une puce « Livré : » (module + résumé d'intégration + noms de
  tests) et une puce « Reste (hors entrée) : » notant l'absence d'intervalle
  configurable / de passage en tâche de fond.
- `docs/source/module-reference.md` — ligne « Où trouver quoi » pour
  `apps/desktop/autosave.*`, + une note sur la convention
  `QStandardPaths::AppDataLocation` (première utilisatrice dans
  `apps/desktop`, l'existant n'utilisait que `TempLocation`).

## 7. Commandes de validation

```powershell
cmake --build --preset msvc-debug --target openstitch_desktop_widgets
ctest --preset msvc-debug -R main_window
git ls-files '*.cpp' '*.hpp' | xargs clang-format --dry-run --Werror
```

## 8. Questions ouvertes / risques

1. **Incohérence du texte d'architecture, résolue ci-dessus, à faire valider** :
   le doc d'archi écrit `autosave_.onIdentityChanged/discardAutosave(...)`
   (laissant croire à une classe) alors que sa propre liste d'API ne déclare
   que des fonctions libres — résolu en fonctions libres (§3). Il écrit aussi
   un second `setCurrentProjectPath(QString())` explicite dans le flux de
   récupération malgré l'interdiction d'un 4e site d'appel — résolu en le
   supprimant, redondant avec l'appel interne de `resetDocumentState()`
   (§4.5). Décisions d'implémentation, pas de changement de scope, mais à
   valider avant merge.
2. **Fonction de hash du slug** : SHA-1 hex sur le chemin canonique
   (`QCryptographicHash`, repli sur la chaîne brute si la canonicalisation
   échoue, miroir du helper `canonicalOrSelf` de `recent_files.cpp`) —
   l'architecture laisse ce choix au code-planner ; n'importe quel hash
   stable convient.
3. **UX du dialogue de récupération** : une `QMessageBox` par orphelin, en
   boucle — l'ordre d'énumération de `scanForRecoverableAutosaves()` n'est
   pas spécifié trié ; à décider si on trie par `saved_at` décroissant.
4. **Kill/coupure de courant** : si le processus meurt sans que `closeEvent`
   s'exécute, le créneau n'est jamais purgé — c'est voulu (c'est justement ce
   qui le rend récupérable).
5. **Non-atomicité sidecar/.osp** : écriture séparée (`.osp` atomique via
   `save_project`, sidecar via `QSaveFile` séparé) — un crash entre les deux
   laisse au pire un `.osp` sans sidecar (affiché "Projet sans nom" à la
   récupération, inoffensif). Risque théorique, déjà couvert par le cadrage
   "incohérence bénigne" de l'architecture.
