---
name: openstitch-bugfix
description: Méthode d'investigation et de correction d'un bug OpenStitch par cause racine (repro en test, hypothèses, correctif minimal, preuve, trace écrite dans la doc). À utiliser quand l'utilisateur signale un défaut (« ça bave », « un résidu reste », « le satin est refusé », crash, mauvais DST, undo qui casse…), quand un test échoue sans raison évidente, ou avant de toucher au satin/auto-satin/satin_planning pour corriger un comportement.
---

# Corriger un bug par cause racine

Le projet documente chaque bug non trivial avec sa **cause racine exacte**,
les **hypothèses rejetées** et la **preuve de test** (voir `satin.md`, et les
commits `docs(satin): …hypotheses… rejetees`). Un correctif qui masque le
symptôme sans cause identifiée n'est pas acceptable ici.

## 1. Comprendre avant de toucher

- `openstitch-docs` sur le sous-système : le bug est peut-être déjà connu
  (`limitations.md`, « Dette technique connue »), ou une hypothèse a déjà été
  essayée et rejetée — ne pas la retenter sans élément nouveau.
- Satin / auto-satin / jonctions : lire les sections pertinentes de
  `docs/source/satin.md` **avant** tout changement (obligatoire, cf. CLAUDE.md).
- Identifier le **chemin réel** emprunté : UI → `MainWindow` → commande →
  lib. Plusieurs points d'entrée UI (menu contextuel, barre d'outils,
  auto-numérisation) convergent-ils vers la même fonction ? Un réseau satin
  multi-sections = plusieurs `EmbroideryObject` pour un seul
  `source_vector` : source fréquente de « ne s'applique qu'à une partie ».

## 2. Reproduire dans un test qui échoue

- Repro minimale : forme de référence CLI (`openstitch-cli-debug`),
  projet synthétique en µm, ou fixture dans `tests/fixtures/`.
- Écrire le test **avant** le correctif et vérifier qu'il échoue pour la
  bonne raison. Test au niveau le plus bas qui montre le bug (lib), plus un
  test du vrai chemin UI dans `test_main_window.cpp` si le bug venait de
  l'app.
- Bug non reproductible en test : le dire à l'utilisateur, ne pas
  « corriger » à l'aveugle.

## 3. Cause racine

- Formuler la cause en une phrase vérifiable (« `erase()` au fil de l'eau
  enregistre l'index post-retrait au lieu de l'index d'origine dès que 2+
  objets consécutifs partagent le même `source_vector` »).
- Instrumenter plutôt que deviner : `INFO`/`CAPTURE` dans le test, sortie
  texte de `sgsd-debug`, SVG de diagnostic.
- Chercher les **jumeaux** : le même motif fautif ailleurs (même pattern de
  boucle, même conversion µm↔px qui tronque, même commande voisine). Les
  corriger dans le même lot avec leur propre test.

## 4. Correctif

- Minimal et au bon niveau : dans la lib, pas un contournement dans
  `apps/desktop`. Aucune logique métier ajoutée aux widgets.
- Respecter les invariants : µm entiers, déterminisme, toute mutation via
  `ICommand`, consommateurs via `effective_sequence`.
- Si une hypothèse de correctif résout le cas mais en casse un autre
  (typique en satin : résout `E` mais casse `trident`), elle est **rejetée** :
  revenir en arrière et la documenter plutôt que d'ajuster les tests.

## 5. Preuve

- Le nouveau test passe, **et** la suite complète de la lib + les libs
  dépendantes (`openstitch-build-test`, boucle complète).
- Pour un bug de rendu : SVG avant/après sur la forme de repro et sur les
  formes de référence voisines (non-régression visuelle).

## 6. Trace écrite

- Doc du sous-système (via `openstitch-docs`) : symptôme, cause racine,
  correctif, test qui le prouve ; hypothèses rejetées et pourquoi. Si la
  limite reste partielle, mettre à jour `limitations.md`.
- Message de commit `fix(<lib>): …` dont le corps reprend : défaut signalé
  (avec la date si l'utilisateur l'a rapporté), cause racine, correctif,
  bugs connexes trouvés, tests ajoutés, non-régression lancée (chiffres).
  Voir `openstitch-commit`.
