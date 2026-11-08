# R01-01 — Profilage CPU avant expérimentation GPU

Le [chantier R01](https://app.notion.com/p/412bb207e2da813c9851f2d55005f76a)
étudie le budget de latence disponible pour un calcul GPU du moteur.
Ce banc mesure le parcours sans Qt de l'image à la prévisualisation,
sur les appels du CLI et le moteur de rendu logiciel.

## Reproduire

```powershell
cmake --preset msvc
cmake --build --preset msvc-release --target openstitch-profile-engine
$bench = 'build/msvc/tests/bench/Release/openstitch-profile-engine.exe'
pwsh -File scripts/performance/profile_engine.ps1 -Executable $bench -OutputDirectory build/r01-serial -Threads 1
pwsh -File scripts/performance/profile_engine.ps1 -Executable $bench -OutputDirectory build/r01-native
```

Lancer une campagne à la fois sur une machine au repos. Le lanceur rejoue
chaque cas dans plusieurs processus. Le premier passage d'un processus
mesure des caches moteur froids. Le cache disque de Windows peut déjà
contenir les images. Les autres passages bénéficient des caches moteur
internes, avec reconstruction complète de la séquence effective à chaque
passage. Le rapport sépare ces populations et conserve chaque échantillon.

Le parcours applique un filtre médian, segmente, vectorise dans la
numérisation, génère la séquence effective avec finitions et ponts cachés,
puis rasterise l'aperçu à 3 px/mm (limite de 2 millions de pixels). Le cas
lin passe en plus par la simulation du tirage. La durée de bout en bout
couvre ces opérations depuis le décodage jusqu'au raster en mémoire.
Les données identifient les dimensions, le nombre de régions, d'objets, de
points, de sauts et de coupes. Les empreintes contrôlent la reproductibilité
de la segmentation, de la séquence et du raster.

## Lire les mesures

- `wall_ms` est la latence perçue par l'appelant. `cpu_ms` additionne le temps
  de tous les fils du processus. Une résolution limitée du compteur CPU
  peut arrondir des mesures courtes à zéro.
- `task_ms` cumule le temps des appels d'une étape. Les sous-étapes sont
  incluses dans leur parent, et plusieurs appels peuvent se recouvrir en
  parallèle. Les mesures à un fil servent à estimer la borne de gain
  théorique des opérations du moteur. Le rasteriseur de prévisualisation
  utilise son propre nombre de fils. Les mesures avec le nombre de fils
  par défaut servent de référence pour la latence utilisateur.
- `upload_bytes` et `download_bytes` indiquent la charge utile logique des
  opérations instrumentées. Les formats GPU, l'alignement, le regroupement
  des appels et les tampons de transit changeront le coût réel. `buffers`
  compte les sorties allouées explicitement aux sites OpenCV instrumentés.
  Cette mesure couvre une partie des allocations du moteur. Le pic RSS
  couvre tout le processus depuis son démarrage.
- `--no-instrument` désactive l'enregistrement des sous-étapes du moteur.
  Les durées du parcours et les empreintes restent disponibles pour le
  contrôle de l'effet de la mesure.
