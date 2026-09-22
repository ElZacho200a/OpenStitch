---
name: openstitch-commit
description: Finaliser un lot de travail OpenStitch et le committer selon les conventions du dépôt (vérifications finales, doc/PDF, découpage, message conventionnel en français). À utiliser quand l'utilisateur demande de committer, de « finir le lot », de préparer une PR, ou de faire le point sur ce qui reste avant de livrer.
---

# Finaliser et committer un lot

Ne committer / pousser que sur demande explicite de l'utilisateur. Sur
`main` sans consigne contraire, demander s'il veut une branche avant une PR.

## 1. Vérifications avant commit

1. Build + tests des libs touchées (`openstitch-build-test`), boucle
   complète si `core`/`geometry`/`document` ont changé.
2. Format : `clang-format -i` sur les fichiers `.cpp/.hpp` **modifiés
   seulement** (clang-format 18, `.clang-format` du dépôt).
3. Aucun appel direct à `generate_sequence` ajouté hors
   `libs/stitch_generation/` ; aucun include Qt dans `libs/` ou `apps/cli`.
4. Doc à jour (`openstitch-docs`) et **PDF régénéré** si `docs/source/` a
   changé : `powershell -File docs/scripts/build-docs.ps1` (met à jour
   `docs/build/OpenStitch-Studio-Documentation.pdf` et
   `documentation-report.md`, à committer avec).
5. `git status` : ne pas embarquer les artefacts locaux (`out.svg`,
   `*-debug.svg` à la racine, `aqtinstall.log`, `sample/` non suivi,
   `.venv*`, fichiers utilisateur type GISTRE). Stager fichier par fichier,
   jamais `git add -A` aveugle.

## 2. Découpage

Un commit = un sujet cohérent qui compile et passe les tests. Les gros
travaux sont découpés en **lots** (« Lot A/B/C… », « étape 3/6 ») : un
commit par lot, la mention entre parenthèses en fin de sujet. La doc qui
accompagne un changement va dans le même commit ; une doc indépendante
(analyse, hypothèses rejetées) peut faire un `docs(<scope>): …` séparé.

## 3. Message

```
<type>(<scope>): <résumé en français, minuscule, sans point final> [(Lot X)]

<corps : pourquoi, puis quoi — paragraphes courts et/ou puces « - »>

Co-Authored-By: <ligne d'attribution fournie par le système>
```

- **type** : `feat`, `fix`, `docs`, `test`, `refactor`, `style`, `ci`,
  `perf`, `chore`.
- **scope** : nom de la lib (`autodigitize`, `satin_planning`, `commands`,
  `stitch_generation`…), `desktop`, `cli`, `scripts`, `release`, ou le
  sous-système doc (`docs(satin)`). Omis pour un changement transverse.
- **Sujet** : de préférence sans accents (la majorité de l'historique),
  ≤ ~80 caractères, décrit l'effet (« ne plus ignorer le fond sur le seul
  critere "pas d'alpha" »).
- **Corps** (obligatoire hors trivial), dans cet ordre :
  1. le **problème constaté**, chiffré si possible (« reflet éclaté en 817
     morceaux », défaut signalé par l'utilisateur le AAAA-MM-JJ) ;
  2. la **cause racine** pour un `fix` ;
  3. ce qui change, symboles qualifiés sans backticks
     (« - segmentation::merge_small_regions : … »), valeurs par défaut et
     unités (mm, mm²) ;
  4. compatibilité : `.osp` existants, défauts inchangés ;
  5. tests ajoutés et non-régression lancée (« test_commands 57 cas / 406
     assertions »), golden régénérés s'il y en a.
- Lignes du corps ≤ 76 colonnes.

Passer le message via here-string PowerShell (`git commit -m @'…'@`, `'@`
en colonne 0) ou heredoc Bash pour préserver les accents et les guillemets.

## 4. PR (si demandée)

Titre = sujet du commit principal ; description : contexte, liste des lots,
tests lancés, captures/SVG si rendu modifié, points d'attention pour la
relecture ; terminer par la ligne d'attribution fournie par le système.
La CI (Windows Debug+Release, `linux-core` sans Qt) doit passer.
