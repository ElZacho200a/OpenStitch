# Audit et feuille de route UX/UI

Dossier de l'audit UX/UI du 2026-10-18 (analyse statique du code + une mesure hors écran). Il
**complète** les audits antérieurs sans les remplacer.

| Document | Contenu |
|---|---|
| [synthese.md](synthese.md) | Version courte : 3 problèmes structurels, 10 gains rapides, décisions, ordre. **Commencer ici.** |
| [audit-ux-ui-2026-10-18.md](audit-ux-ui-2026-10-18.md) | Inventaire des constats (classe, priorité, preuves, correction), état réel des constats d'octobre, mesures, limites. |
| [parcours-utilisateur.md](parcours-utilisateur.md) | Les 14 étapes image → DST : clics actuels, frictions, cibles. |
| [strategie-interface.md](strategie-interface.md) | Principes, organisation des panneaux, refontes évaluées, lots A à E. |

La liste de travail exécutable est dans `docs/roadmap-parite-hatch.md` §20 (HP-UX-017…044),
§22 (HP-PERF-009) et §23 (HP-QA-008).

## Audits et documents liés

- [`ui-ux-audit-2026-10-13.md`](../ui-ux-audit-2026-10-13.md) : registre `UX-02…UX-46` (le statut
  actuel de chaque constat est dans le rapport, §2).
- [`ui-ux-correctifs-2026-10-13.md`](../ui-ux-correctifs-2026-10-13.md) : premier lot de correctifs.
- [`ui-audit-2026-10.md`](../ui-audit-2026-10.md), [`audit-ui-ergonomie.md`](../audit-ui-ergonomie.md),
  [`ui-redesign-*.md`](../ui-redesign-specification.md) : audits et refonte antérieurs.
- [`lot8-manual-editing-design.md`](../lot8-manual-editing-design.md) : retouches manuelles (ADR-014).

## Reproduire la mesure

```powershell
cmake --build --preset msvc-release --target openstitch-ui-shots
$env:QT_QPA_PLATFORM = "offscreen"
build\msvc\apps\cli\Release\openstitch-cli.exe project digitize tests\fixtures\tentabrode.png -o tent.osp
build\msvc\tests\bench\Release\openstitch-ui-shots.exe tent.osp shots
```

(`openstitch-ui-shots` imprime les `minimumSizeHint` de la fenêtre et des docks et enregistre des
captures ; le dossier Qt doit être dans le `PATH`.)
