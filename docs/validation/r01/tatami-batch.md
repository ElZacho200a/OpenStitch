# R01-04 — Intersections de tatami par lots

L'expérience compare trois chemins pour produire les abscisses des
intersections. Le premier utilise le balayage actuel. Le deuxième
cherche par dichotomie les rangées couvertes par chaque arête et
interpole seulement ces croisements sur CPU. Le troisième utilise
la même préparation puis envoie le lot d'un remplissage à D3D11.
Chaque arête dispose de slots disjoints dans le résultat compact.
Le lot demande un dispatch et une lecture de retour.

Les rangées gardent l'addition répétée du moteur et les croisements
gardent la convention demi-ouverte. Le shader exécute la soustraction,
la division FP64, la multiplication et l'addition dans cet ordre.
La division exige la capacité D3D11 de doubles étendus. Le tri,
les pénétrations, les sous-couches, le routage, l'arrondi des coordonnées
et les finitions passent par le générateur de production.
[Instruction DDIV](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/ddiv--sm5---asm-).

Le temps mur va du projet déjà numérisé à la séquence effective complète,
avec cache de fragments désactivé. Il comprend les transferts et le coût
de préparation du lot. Les validations ont lieu hors chronométrage.
Le banc vérifie chaque commande, y compris ordre, coordonnées, type,
passe et objet source, à chaque répétition. Il compare aussi le DST et
relève les écarts binaires et absolus des abscisses FP64 au début de
chaque processus. Il simule un échec d'allocation pour vérifier le repli
sur le balayage CPU. Une divergence des commandes ou du DST exclut la
configuration des comparaisons de vitesse.

`sum_adapter_ms` cumule les appels de l'adaptateur, attente éventuelle
du verrou comprise. Plusieurs appels peuvent se recouvrir. Le temps
mur de la séquence reste la mesure de référence. Les bornes de mémoire
s'appliquent à chaque lot.

## Reproduire

```powershell
cmake --preset msvc -DOPENSTITCH_EXPERIMENTAL_TATAMI_SCAN=ON
cmake --build --preset msvc-release --target openstitch-tatami-batch test_stitch
$exe = 'build/msvc/tests/compute_experiment/Release/openstitch-tatami-batch.exe'
& $exe tests/fixtures/cli/logo_small.png 30.48 11
& $exe tests/fixtures/tentabrode.png 119.2106667 11
& $exe --stress 11
```

Le cas synthétique dessine un motif d'environ 300 mm, avec un contour
lobé de 2 048 sommets, neuf trous de 128 sommets et un angle de
0,31 radian. Les mesures à un fil s'obtiennent en définissant
`OPENSTITCH_THREADS=1` avant de lancer le banc. Le mode habituel
reprend le réglage de fils par défaut. La cible de banc et son hook
sont activés par l'option expérimentale.

Le seuil d'intérêt est un gain d'au moins 5 % au p50 et au p95 sur
la génération complète d'un cas éligible, face au meilleur CPU exact.
