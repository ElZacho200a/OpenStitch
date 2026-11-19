# Cache d'objets C++ avec sccache (POC BUILD-OPT-01.4)

Statut : **POC optionnel, désactivé par défaut**. Rien n'est activé en CI. Ne pas confondre avec le
cache binaire **vcpkg** (paquets tiers déjà en place) : sccache met en cache les `.obj` du projet.

## Activation

Le générateur Visual Studio du preset `msvc` **ignore** `CMAKE_CXX_COMPILER_LAUNCHER`. Le POC passe donc
par un preset séparé, `msvc-sccache` (Ninja Multi-Config, `cl`, launcher `sccache`).

```powershell
winget install Mozilla.sccache --scope user
. scripts/dev/sccache-env.ps1            # vcvars x64 + Ninja + SCCACHE_DIR hors dépôt (10G max)
cmake --preset msvc-sccache
cmake --build --preset msvc-sccache-debug --parallel 2   # ou msvc-sccache-release
sccache --show-stats
```

- `scripts/dev/sccache-env.ps1` : cache dans `%LOCALAPPDATA%\sccache-openstitch`, borné par
  `SCCACHE_CACHE_SIZE` (défaut 10G). Le message « sccache introuvable » apparaît si `sccache` n'est pas
  encore dans le `PATH` du shell (rouvrir le shell après winget).
- `/Zi` est incompatible avec sccache (PDB partagé) : le preset fixe
  `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded` (`/Z7`) pour Debug et RelWithDebInfo (politique CMP0141).
- Désactiver : ne pas utiliser ce preset (ou `-DCMAKE_CXX_COMPILER_LAUNCHER=`).

## Mesures

Machine partagée (16 Go, autres sessions actives : durées bruitées), cible
`openstitch_stitch_generation` et ses dépendances (97 unités de compilation), `--parallel 2`,
sccache 0.19.0, MSVC 14.50, après `clean` de la cible. Une seule exécution par ligne.

| Scénario | Debug | Release | Cache |
|---|---|---|---|
| Sans launcher (Ninja, même preset) | 55 s | 74 s | n/a |
| sccache, cache froid (0/97 hits) | 102 s | 120 s | — |
| sccache, même worktree, rebuild (97/97 hits) | 24 s | 33 s | 100 % |
| sccache, **autre worktree** (chemin différent), Debug | 93 s | — | **0/97 hits** |

- Le temps « chaud » inclut encore lien, Ninja et nettoyage : gain ≈ 56 % (Debug), 55 % (Release) face à
  l'absence de cache.
- Cache froid : surcoût mesuré ≈ +85 % (Debug) / +62 % (Release) (préprocessing + écriture). Partie
  possible de bruit de machine partagée, non isolée.
- **Partage entre worktrees : aucun gain mesuré.** Hypothèse non vérifiée : sccache hache la sortie
  préprocessée de `cl`, qui contient les chemins absolus (`#line`) ; deux worktrees ont donc des clés
  différentes. Piste à tester : worktrees au même chemin logique (`subst`), ou autre mécanisme de
  normalisation ; non essayé.
- Stockage : ≈ 59 Mio pour 3 builds de 97 unités (Debug + Release + 2e worktree) ; build dir ≈ 2 Go
  (dont vcpkg_installed) par arbre, hors cache.
- Non mesuré : cible complète (apps, tests, Qt moc/QML), builds concurrents, CI.

## Verdict provisoire

Utile pour **reconstruire dans le même arbre** (changement de branche, `clean`, alternance
Debug/Release) ; **sans intérêt démontré entre worktrees** tant que les chemins diffèrent ;
surcoût au premier build. CI : ne rien activer sans mesure sur runners (cache `actions/cache` du dossier
sccache à évaluer, chemins stables sur runner). Roadmap : aucune entrée concernée.
