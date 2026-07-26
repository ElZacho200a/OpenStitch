# Guide de contribution

Public : contributeur.

## Flux

1. Cloner le dépôt et créer une **branche** dédiée (le développement se fait sur
   `main` en local ; ne pas committer directement sans branche pour une
   fonctionnalité).
2. Compiler et exécuter les tests (`ctest --preset msvc-debug`).
3. Ajouter/mettre à jour les **tests** couvrant la modification.
4. Formater (`clang-format`) et vérifier les warnings.
5. Rédiger un message de commit clair (voir ci-dessous).

## Messages de commit

Le dépôt utilise des messages descriptifs en français, terminés par une ligne de
co-auteur. Exemple observé :

```
Corrige le debordement des remplissages hors des regions

<description détaillée>

Co-Authored-By: ...
```

## Style C++

- Respecter `.clang-format` et les warnings élevés.
- Types forts pour les unités ; pas de logique métier dans les widgets Qt.
- Toute mutation du document via une **commande** (undo/redo).
- En-tête SPDX Apache-2.0 en tête de fichier.

## Ajouter une dépendance

- La déclarer dans `vcpkg.json` (ou documenter l'installation si hors vcpkg).
- **Vérifier la licence** : compatible Apache-2.0, permissive de préférence.
  **Aucun code GPL** ne peut être copié dans ce dépôt (voir *Licences*).
- L'encapsuler derrière une interface interne (ne pas exposer ses types).
- La documenter dans `THIRD_PARTY_LICENSES.md`.

## Signaler un bug / proposer une fonctionnalité

*Information non déterminée dans le dépôt* : aucun gestionnaire d'issues public
n'est configuré (dépôt local). En interne, documentez le problème avec un cas
minimal reproductible et, si possible, un test qui échoue.

## Documentation

Toute fonctionnalité visible doit être reflétée dans `docs/source/` puis le PDF
régénéré (voir *docs/README.md*). Les affirmations doivent rester **vérifiables**
dans le code (section « Implémentation associée »).

## Implémentation associée

- `.clang-format`, `THIRD_PARTY_LICENSES.md`, `LICENSE`.
- `docs/README.md` — régénération de la documentation.
