# Suivi périodique des performances — HP-PERF-008

Le workflow GitHub Actions **Weekly performance snapshot** fonctionne
chaque mercredi à 03:15 UTC et sur lancement manuel. Il mesure le benchmark
Qt-free sur `tests/fixtures/tentabrode.png` en deux passes et enregistre
`bench.log`, `bench.json` (mesures structurées) et `bench.md` comme
artefacts GitHub Actions pendant 90 jours.

Le relevé indique pour chaque étape le temps à froid, le minimum et la
médiane à chaud, et la mémoire de pointe mesurée par le benchmark existant.
**Ce n'est pas un contrôle bloquant** : machines CI, caches vcpkg et bruit
de mesure changent. Une régression doit être confirmée sur la même
configuration avant une conclusion.

Tests locaux du parseur :

```sh
python -m unittest discover -s scripts/performance -p 'test_*.py'
```

Reste : tableau historique comparatif entre plusieurs runs, seuils robustes
une fois un environnement de benchmark comparable disponible.
