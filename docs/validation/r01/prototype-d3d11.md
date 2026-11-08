# R01-03 — Prototype D3D11 pour masques et distances

Le banc appelle le même contrat hôte en CPU et en GPU. Les comparaisons
sont faites avec l'ouverture elliptique et la distance `DIST_L2, 3`
d'OpenCV. Les sorties doivent concorder octet pour octet pour les
masques et bit à bit pour les flottants en pixels. Les jeux limites
couvrent tous les masques 3 × 3 binaires, les diamètres pairs et impairs,
les trous, les traits fins, les valeurs grises, les bords, les masques
sans zéro et les dimensions proches d'une taille de groupe.
Les ouvertures de petit diamètre sont comparées jusqu'à 2 048 × 2 048.
La distance est comparée sur des bandes de 2 048 pixels et des masques
denses jusqu'à 512 × 511.

Le noyau de distance examine les lignes par écart vertical croissant.
Un premier passage donne, pour chaque pixel de chaque ligne, l'écart
horizontal au zéro le plus proche. Le second calcule le minimum de la
métrique chanfrein Q16. Pour des écarts `dx` et `dy`, le coût vaut
`62587 × max(dx, dy) + 27151 × min(dx, dy)`. Un écart vertical suffit
à arrêter la recherche quand sa borne `62587 × dy` atteint le meilleur
coût déjà trouvé. [Référence OpenCV 4.12 sans IPP](https://github.com/opencv/opencv/blob/4.12.0/modules/imgproc/src/distransform.cpp#L45-L129).

## Reproduire

```powershell
cmake --preset msvc
cmake --build --preset msvc-release --target openstitch-compute-prototype test_compute_experiment
ctest --test-dir build/msvc/tests/compute_experiment -C Release --output-on-failure
$exe = 'build/msvc/tests/compute_experiment/Release/openstitch-compute-prototype.exe'
& $exe --cpu-only
& $exe | Set-Content -Encoding utf8 build/r01-d3d11.jsonl
```

Le banc requiert Windows, le SDK D3D11 et un adaptateur matériel de niveau
11.0. La cible est exclue du build par défaut. Le test hôte indépendant
est disponible dans le build de test sans Qt. Le mode `--cpu-only`
exerce le repli automatique en omettant l'adaptateur.

Le prototype échange des buffers hôte pour chaque appel. Le banc
mesure le temps mur entre ces buffers, avec la préparation, les
transferts et la synchronisation. Il alterne l'ordre CPU/GPU au fil
des répétitions. Les résultats du lot sont conservés jusqu'à la fin du
chronométrage, puis comparés bit à bit à la référence CPU à chaque
répétition. Les masques synthétiques reproduisent les tailles et
les volumes d'appels choisis à partir du
[profilage CPU](https://github.com/ElZacho200a/OpenStitch/blob/17839bf1/docs/validation/r01/README.md).
Ils contiennent des motifs disjoints, des trous et des traits qui
touchent le cadre. La mesure concerne le prototype et la charge
utile de ses appels. Une décision d'intégration demanderait ensuite
la latence et les empreintes du pipeline complet.

`upload_ms` couvre la préparation et l'envoi côté hôte.
`readback_ms` couvre la copie vers le tampon de lecture, l'attente
et la conversion en sortie. Le timestamp GPU `dispatch_ms` peut
recouvrir cette attente. Le temps mur est la mesure de référence.

Le prototype borne les buffers et le nombre de visites calculées avant
dispatch. Le budget de 128 Mio couvre les tampons comptabilisés. La
borne de 134 217 728 visites couvre deux voisinages pour l'ouverture
ou les lignes candidates et le prépassage pour la distance.
Le banc vérifie le refus de deux requêtes coûteuses de 2 048 × 2 048
en mode GPU forcé et le repli CPU bit à bit pour la distance en mode
automatique. Une requête annulée pendant la lecture bloquante D3D11
attend le retour du pilote avant d'écarter son résultat.
