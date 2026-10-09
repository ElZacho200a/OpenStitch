# WIP - task/patent-rd

Derniere mise a jour : 2026-10-09

## Contexte

- Worktree active : `.worktrees/patent-rd`
- Branche : `task/patent-rd`
- Objectif mission : recherche brevets broderie, audit OpenStitch, plan technique, puis implementation par lots coherents.
- Contraintes importantes :
  - Ne pas modifier `main` directement.
  - Ne pas push sans autorisation explicite.
  - Respecter `CLAUDE.md`, `GUARDRAILS.md`, les skills projet et `docs/source/satin.md` avant tout changement satin.
  - Auto-broderie : tatami ou contour uniquement ; ne pas reintroduire d'auto-satin silencieux dans `autodigitize`.

## Etat actuel

Lot 1 termine et committe localement :

- Commit : `331bad4 docs(auto_satin): trace patent R&D and rail provenance`
- Working tree : propre apres commit.

Contenu du lot :

- Nouveau chapitre de recherche brevets : `docs/source/patent-research.md`
- Chapitre ajoute au build docs : `docs/scripts/build-docs.py`
- PDF docs regenere : `docs/build/OpenStitch-Studio-Documentation.pdf`
- Rapport docs mis a jour : `docs/build/documentation-report.md`
- Roadmap HP-STI-018 enrichie : `docs/roadmap-parite-hatch.md`
- Code auto-satin :
  - `RailConstructionMethod` place avant les structs qui l'utilisent.
  - `SatinColumnGeometry` et `ParametricSatinObject` portent une provenance `method`.
  - Les colonnes issues des anneaux / turning satin recoivent `RailConstructionMethod::IsoOffsetRing`.
  - `to_satin_column` propage `method` vers `SatinColumn`.
  - `satin_column_view` conserve la methode source.
- Tests :
  - Les tests d'identite `satin_column_view` comparent maintenant `proj.method == src.method`.
  - Nouveau test pour verifier `IsoOffsetRing` sur `ring` et `disc_15mm`.

Reprise en cours :

- Demande utilisateur : continuer la mission de base.
- Cible choisie : HP-STI-018 Phase B.5b, migration de `extend_tip` vers une sonde direction-aware/angulaire pour corriger `y`/`y_symmetric` sans regresser les embouts ouverts (`rectangle`, `notch`, `t`).
- Etat au demarrage : `WIP.md` non committe, aucun autre changement local.
- Implementation en cours :
  - `extend_tip` tente d'abord une sonde laterale par cone angulaire autour de la normale locale, avec repli `cross_section`.
  - La continuite A/B est preservee avec la station precedente.
  - La station finale au plancher est omise si elle croise immediatement la station precedente.
  - La nouvelle sonde est bornee au chemin `use_corridor_tracing_dev_only` pour eviter de changer le comportement historique Parametric/Legacy hors cutover.
  - Test ajoute : `corridor dev flag Phase B.5b : y/y_symmetric -- extend_tip ne croise plus les premiers barreaux`.
  - Resultat cible observe : `y` et `y_symmetric` construisent 3 colonnes sous le chemin corridor, sans refus ni diagnostic `croisement entre barreaux #0/1`; `trident` reste une limite distincte.
  - Validation en cours : `test_auto_satin.exe` complet passe avec `22877 assertions in 102 test cases`.
  - `test_stitch.exe` passe avec `51765 assertions in 227 test cases`.
  - `docs\\scripts\\build-docs.ps1` passe : PDF 186 pages, 0 probleme, 0 avertissement.
  - `git diff --check` passe.
  - `clang-format` / `clang-format-18` restent absents du PATH local.

## Validation effectuee

Commandes passees :

- `cmake --build --preset msvc-release --target test_auto_satin`
- `.\\build\\msvc\\tests\\unit\\auto_satin\\Release\\test_auto_satin.exe`
  - Resultat : OK, `22809 assertions in 101 test cases`
- `.\\build\\msvc\\tests\\unit\\stitch\\Release\\test_stitch.exe`
  - Resultat : OK, `51765 assertions in 227 test cases`
- `.\\docs\\scripts\\build-docs.ps1`
  - Resultat : OK, PDF genere, `0` probleme, `0` avertissement
- `git diff --check`
  - Resultat : OK

Points non valides localement :

- `clang-format` et `clang-format-18` ne sont pas disponibles dans le PATH local.
- La commande directe `python docs\\scripts\\build-docs.py` echoue sans venv (`ModuleNotFoundError: markdown`), mais `docs\\scripts\\build-docs.ps1` installe/utilise le venv docs et passe.

## Recherche brevets

Sources principales documentees :

- EP0761860B1 : lignes caracteristiques, centre-ligne, branches, orientations interpolees.
- US6390005B1 : satin tournant avec espacement inter-point constant.
- US6690988B2 : bitmap vers objets de broderie par squelette/noeuds/chemins.
- US6397120B1 : UI de singularites et alternatives de jonction.
- US6253695B1 : changement de densite d'un groupe de points existants.
- US6587745B1 : remplissage courbe par transformation parametrique.
- US8219238B2 : generation automatique depuis image scannee.
- US6633794B2 : suppression de points sous-jacents recouverts.

Les statuts sont notes comme preliminaires et issus de Google Patents ; ne pas presenter cela comme un avis juridique.

## Prochaine etape recommandee

Traiter HP-STI-018 Phase B.5b avant un cutover plus large :

- `extend_tip` direction-aware.
- Tests corpus sur cercle, disque 15 mm, petale, T/Y/X, anneau, formes avec trous.
- Utiliser `satin_coverage` pour prouver la couverture et les reliquats.
- Eviter de prolonger SGSD comme strategie principale sans preuve de couverture.

## Reprise rapide

Pour reprendre :

1. `cd C:\Users\zache\Documents\EPITA\Assos\Atelier\Embrodeur\.worktrees\patent-rd`
2. `git status --short --branch`
3. Lire `CLAUDE.md`, `GUARDRAILS.md`, `docs/source/satin.md`, puis ce `WIP.md`.
4. Si l'objectif est de continuer l'implementation, partir de HP-STI-018 Phase B.5b.
5. Si l'objectif est de publier le lot actuel, demander confirmation avant push.
