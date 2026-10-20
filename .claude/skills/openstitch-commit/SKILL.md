---
name: openstitch-commit
description: Finaliser un lot de travail OpenStitch et le committer selon les conventions du dépôt (vérifications finales, doc/PDF, découpage, message conventionnel en français). À utiliser quand l'utilisateur demande de committer, de « finir le lot », de préparer une PR, ou de faire le point sur ce qui reste avant de livrer.
---

# Finaliser et committer un lot

**Livraison autonome (consigne permanente, 2026-10-19)** : quand un lot est terminé et
vérifié, enchaîner SANS demander : commit -> `git push -u origin <branche>` ->
`gh pr create --base main` (titre = sujet du commit, corps = résumé, tests, limites, ligne
d'attribution) -> annoncer le lien. Ne jamais pousser ni committer sur `main` (créer une
branche d'abord) ; ne jamais fusionner (`gh pr merge`) ; ne pas ouvrir de PR si des tests du
périmètre échouent. Un échec hors périmètre est signalé dans le corps de la PR.

**Modes de session** (le mode se déduit de la demande ; dans le doute, mission ciblée) :
- *Mission ciblée* (défaut) : faire la demande, valider, commit, PR, **puis s'arrêter** en
  proposant la suite en une ligne. Pas de nouveau lot de roadmap de sa propre initiative.
- *Autonomie continue* : seulement sur demande explicite (« continue », « enchaîne »,
  « travaille en autonomie », `/loop`) : après la PR, reprendre `openstitch-roadmap` §1.
  Arrêt quand l'utilisateur le dit, quand un lot échoue deux fois, ou quand le contexte est
  lourd (écrire alors un point de reprise, `openstitch-token-efficiency` §4).
- *Audit* : examiner, documenter et corriger la configuration ou le code concerné ; ne pas
  enchaîner sur une feature sans rapport.

## 1. Vérifications avant commit

1. Build + tests des libs touchées (`openstitch-build-test`), boucle
   complète si `core`/`geometry`/`document` ont changé.
2. Format : `clang-format -i` sur les fichiers `.cpp/.hpp` **modifiés
   seulement** (clang-format **18** exactement, `.clang-format` du dépôt : pas celui de
   `C:\Program Files\LLVM` qui est en 23 ; voir le piège dans `openstitch-build-test`).
3. Aucun appel direct à `generate_sequence` ajouté hors
   `libs/stitch_generation/` ; aucun include Qt dans `libs/` ou `apps/cli`.
4. Doc à jour (`openstitch-docs`) et **PDF régénéré** si `docs/source/` a
   changé : `powershell -File docs/scripts/build-docs.ps1` (met à jour
   `docs/build/OpenStitch-Studio-Documentation.pdf` et
   `documentation-report.md`, à committer avec).
5. **Roadmap à jour (bloquant, tout travail, pas seulement en mode piloté)** :
   `docs/roadmap-parite-hatch.md` fait partie du même commit. Statut de
   l'entrée `HP-XXX-NNN` touchée (`☑ Fait (date, hash)` ou `◐ Partiel` +
   « Livré : … / Reste : … »), entrée ajoutée pour tout manque découvert face
   à Hatch, ligne au journal (§28), et `docs/source/limitations.md` /
   `roadmap.md` si le tableau des fonctionnalités change. Un correctif ou un
   refactor sans entrée concernée l'écrit dans le message de commit
   (« roadmap : aucune entrée concernée »). Pas de commit sans l'un des deux.
6. **Câblage bureau (bloquant sauf demande explicite)** : toute fonctionnalité ajoutée
   dans le cœur ou le CLI doit aussi être accessible dans `apps/desktop` (action,
   dialogue ou outil + `ICommand` + test QTest). Sinon le commit le dit
   (« bureau : non câblé, demandé par l'utilisateur » ou entrée laissée `◐` avec
   « Reste : câblage bureau » dans la roadmap).
7. `git status` : ne pas embarquer les artefacts locaux (`out.svg`,
   `*-debug.svg` à la racine, `aqtinstall.log`, `sample/` non suivi,
   `.venv*`, fichiers utilisateur type GISTRE). Stager fichier par fichier
   (ou `git add -A` seulement après un `git status` propre de tout artefact).

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
