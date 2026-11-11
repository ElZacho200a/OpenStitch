# Mesurer où Studio perd du temps (journal de performance)

Studio sait consigner la durée réelle de ses reconstructions, **sans aucun surcoût quand c'est désactivé**.

```powershell
$env:OPENSTITCH_PERF_LOG = "C:\temp\openstitch-perf.jsonl"
.\build\msvc\apps\desktop_v2\Release\openstitch_v2_studio.exe   # puis travailler normalement
```

Une ligne JSON par événement (`t` = horodatage en ms) :

| `event` | Champs | Sens |
| --- | --- | --- |
| `scene.rebuild` | `wallMs`, `objects`, `segments`, `effectiveSequenceMs`, `threadSegmentsMs`, `verticesMs`, `pickGridMs`, `builds` | Reconstruction de la scène de points après une modification : temps réel de bout en bout (`wallMs`) et sa ventilation côté worker. |
| `vector.layer` | `shapes`, `builds`, `cacheHits` | Publication d'un calque vectoriel ; `cacheHits` compte les états déjà rasterisés republiés sans calcul. |

Lecture rapide (PowerShell) :

```powershell
Get-Content $env:OPENSTITCH_PERF_LOG | ConvertFrom-Json | Where-Object event -eq 'scene.rebuild' |
  Measure-Object wallMs -Average -Maximum
```

Règles de lecture : un chiffre n'a de valeur que mesuré sur le même commit, en Release, ≥ 3 fois, sur le projet réel
du ralentissement (voir l'audit PERF-STUDIO-2026-11-10 pour le protocole complet).

## État des optimisations de scène (PR #318)

- L'ancienne scène est **conservée** pendant la reconstruction (`StudioController::sceneStale`) ; seule l'ouverture d'un
  autre document l'efface.
- Les contrôleurs de fils, de vue/filtres, d'arbre d'objets, de tirage et d'appliqué ne rediffusent `changed` que si
  `documentStamp()` (révision + sélection + scène) change.
- Les 4 derniers calques vectoriels sont conservés (plafond 96 Mo) : annuler, rétablir ou ré-afficher un état déjà vu ne
  recalcule rien.
