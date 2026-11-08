# ADR 0006 — Calcul CPU/GPU expérimental du moteur

*État : proposé pour le prototype R01. Intégration produit conditionnée aux mesures et à la parité.*

## Contexte

Le [profilage R01-01](https://github.com/ElZacho200a/OpenStitch/blob/17839bf1/docs/validation/r01/README.md)
mesure un budget de 1,52 ms au p50 et 1,15 ms au p95 pour le champ de
distance sur le texte satin, dans le modèle à un fil. Le budget inclut préparation,
transferts, calcul et synchronisation. L'ouverture seule prend environ
0,37 ms sur la marine. Le chantier explore donc d'abord la parité des
masques, puis l'intérêt d'un champ de distance sur des lots de petits
masques. L'aperçu Qt Quick suit sa propre décision d'architecture.
[ADR 0005](0005-studio-qtquick-gpu.md).

## Options

| Option | Intérêt | Coût et limites |
| --- | --- | --- |
| D3D11 Compute | Windows déjà ciblé, compilateur HLSL et bibliothèques dans le SDK local, adaptateur Win32 isolable des contrats hôte | Windows seul. Sélection explicite du GPU, niveau de fonctionnalité 11.0, synchronisation et lecture retour à mesurer. |
| Vulkan compute | Contrat d'adaptateur similaire, Windows et Linux, voie macOS avec MoltenVK | Compilateur SPIR-V et SDK à ajouter à la chaîne de build. Gestion explicite de la mémoire, des barrières et des limites du pilote. Le loader seul est disponible dans l'environnement mesuré. |
| QRhi | Mutualisation possible avec une application Qt | API privée de Qt 6.8, affinité au fil et cadence du rendu. Le cœur et le CLI ont besoin d'un contrat indépendant de Qt. |

Références : [Compute D3D11](https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-compute-shader),
[Vulkan compute](https://docs.vulkan.org/guide/latest/compute_shaders.html),
[QRhi 6.8](https://doc.qt.io/qt-6.8/qrhi.html).

## Décision proposée

Confiner le premier prototype à un exécutable de banc facultatif. Garder
un contrat hôte C++23 testable partout et utiliser D3D11 dans l'adaptateur
Windows. D3D11 permet de mesurer le matériel disponible avec le SDK
10.0.26100.0. Réexaminer Vulkan si un cas viable requiert plusieurs OS.
Les handles, le choix de l'adaptateur et les shaders appartiennent au banc.

Le contrat reçoit un instantané immuable, avec dimensions, octets et
révision. Il restitue un résultat hôte typé, un identifiant de révision
et des diagnostics. Les masques sont bornés à 2 048 pixels par axe
dans l'expérience, ce qui couvre la limite auto-satin usuelle de
1 500 pixels. Le diamètre de l'ellipse est borné à 63 pixels. Le budget
est vérifié avant les allocations connues.
L'appel est synchrone et le résultat peut être rejeté après annulation.
Une intégration asynchrone devra posséder son instantané et comparer
sa révision à celle du document avant publication.

Le mode CPU exécute la référence OpenCV. En mode automatique, une
indisponibilité, un budget GPU insuffisant ou un échec du pilote
déclenche un repli CPU signalé. Le mode GPU forcé fait remonter la
cause afin de préserver l'interprétation des mesures. L'annulation
et les erreurs d'entrée terminent l'opération.

## Critères avant essai GPU

- Ouverture elliptique avec les mêmes bords, ancre et diamètre qu'OpenCV.
  Utiliser la valeur de bord neutre de chaque passe pour `CV_8U`
  (255 pour l'érosion, 0 pour la dilatation). Exiger l'égalité de
  chaque octet, y compris quand des régions touchent les bords.
- Distance `DIST_L2, 3` pour OpenCV 4.12 sans IPP. Exiger 0 ULP après
  conversion en pixels puis en micromètres sur cette chaîne. Le chemin
  natif utilise les poids entiers Q16 62587 et 89738. Vérifier la
  sentinelle en absence de zéro, les trous, les zones disjointes, les
  diagonales fines, les bords et les tailles proches des limites du
  dispatch. Une variante OpenCV avec IPP demande une qualification
  distincte. [Source OpenCV 4.12](https://github.com/opencv/opencv/blob/4.12.0/modules/imgproc/src/distransform.cpp#L45-L129).
- Mesurer l'initialisation à froid, la préparation et l'envoi,
  le calcul, la synchronisation et la copie retour. Le temps mur
  jusqu'au résultat hôte et la stabilité des sorties pilotent la
  décision. Un gain local doit ensuite se traduire par au moins 5 %
  au p50 et au p95 du parcours texte à chaud, sur le même matériel
  et avec le même réglage de fils, avec un repli CPU fiable.
- Valider séparément toute intégration avec empreintes d'objets, de
  séquence et d'aperçu. Conserver les résultats bruts du banc et
  le nom exact de l'adaptateur et du pilote.
