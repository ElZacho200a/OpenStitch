# Guide du développeur

Public : développeur contributeur.

## Conventions

- **C++23**, RAII, `std::optional`/`std::variant`/`std::span`/`std::filesystem`,
  `std::expected` (via `Result<T>`).
- **Types forts** pour les unités : jamais de coordonnée en `double` nu.
- **Const-correctness**, warnings élevés (`/W4 /permissive-`).
- **Formatage** : `.clang-format` (base LLVM, 4 espaces, 100 colonnes).
- **En-tête de licence** : `// SPDX-License-Identifier: Apache-2.0`.

## Pièges connus (spécifiques à ce dépôt)

Avertissement : Quelques écueils rencontrés lors du développement :

- les **noms de tests Catch2** doivent être en **ASCII** (les accents ne
  survivent pas à l'encodage console Windows lors de la découverte des tests) ;
- l'en-tête spdlog est `stdout_color_sinks.h` ;
- un membre nommé `slots` est interdit (macro Qt) — utiliser un autre nom ;
- déclarer les classes Qt en **forward declaration au scope global** ;
- pas de `M_PI` sous MSVC → utiliser `std::numbers::pi` (`<numbers>`) ;
- cibles vcpkg exactes : `Clipper2::Clipper2`, `MINIZIP::minizip-ng`.

## Ajouter une fonctionnalité (recettes)

- **Nouveau type de point** : voir *Référence des modules → Points d'extension*.
- **Nouvelle opération d'image** : une alternative de `ImageOp`, un cas dans
  `apply_op`, un test, une action menu.
- **Nouvelle commande annulable** : dériver `ICommand` (`apply`/`revert`/`name`),
  l'exécuter via `UndoStack::execute`. Toute mutation du document doit passer par
  une commande.

## Undo/redo

`UndoStack` conserve deux piles (`undo`/`redo`). Une nouvelle commande vide la
pile `redo`. Les commandes de segmentation mémorisent les **indices** de pixels
modifiés pour une annulation exacte et légère.

## Gestion des unités

Toujours convertir aux frontières : pixels→µm à l'import (résolution explicite),
µm→mm pour l'affichage, µm→0,1 mm à l'export DST. Les algorithmes internes
peuvent utiliser des `double`, mais les entrées/sorties des modules sont en types
forts.

## Multithreading

Il n'y a pas encore d'infrastructure asynchrone. Écrire les nouveaux traitements
comme des **fonctions pures** sur des instantanés, pour rester compatible avec un
futur passage en tâche de fond.

## Implémentation associée

- `.clang-format`, `CMakeLists.txt` (cible `openstitch::warnings`).
- `libs/commands/` — `ICommand`, `UndoStack`, commandes du projet.
- `docs/phase0/` — décisions d'architecture (ADR).
