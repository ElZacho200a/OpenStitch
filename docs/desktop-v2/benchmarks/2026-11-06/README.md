# Mesures natives Studio — 2026-11-06

Une passe Release, Qt 6.8.3, Windows 11 25H2, AMD Ryzen 7 7700X ; écran Samsung U28E590 à
59,997 Hz, DPR 1,5, viewport 1280 × 800 pixels logiques (1920 × 1200 pixels physiques).
Le [journal d'adaptateur](adapters.txt) confirme le backend D3D11, la boucle de rendu threadée
et **NVIDIA GeForce RTX 5060 Ti** sélectionnée. Le pilote déclaré par Windows est 32.0.16.1088.
L'iGPU AMD et le pilote logiciel Microsoft sont seulement énumérés, ils ne sont pas sélectionnés.
Cette première passe utilise un échantillon MSAA. La [passe finale à 4× MSAA du 2026-11-07](../2026-11-07-msaa4/README.md)
est conservée séparément ; une passe par configuration ne suffit pas à isoler l'effet du MSAA.

Chaque cas est un processus séparé, avec 30 gestes de chauffe puis 240 échantillons. Le banc ne
mesure que le canevas. Les limites et les tables se trouvent dans le [rapport](../../18-studio-benchmarks.md).
Une cadence Quick de 60 Hz ne prouve pas un gain sur Widgets : les JSON Widgets mesurent la
fin de peinture CPU, sans mesure de présentation par le compositeur.

| Scène | Quick | Widgets |
|---|---|---|
| 1 000 points | [JSON](1000-quick.json) | [JSON](1000-widgets.json) |
| 10 000 points | [JSON](10000-quick.json) | [JSON](10000-widgets.json) |
| 50 000 points | [JSON](50000-quick.json) | [JSON](50000-widgets.json) |
| 100 000 points | [JSON](100000-quick.json) | [JSON](100000-widgets.json) |
| Tentabrode, 43 670 points | [JSON](real-quick.json) | [JSON](real-widgets.json) |

Les tableaux d'échantillons sont conservés intacts ; seul le chemin absolu du champ `project`
a été remplacé par `tentabrode-300mm.osp`. [Métadonnées et empreintes SHA256](machine.json),
[synthèse arrondie](summary.json), [extraits QSG_INFO](adapters.txt).
Le fichier de synthèse arrondit certains champs pour affichage ; les dix JSON constituent la
référence numérique complète.

Le projet réel a été généré par `run_digitize` depuis `tests/fixtures/tentabrode.png` avec une
hauteur **d'image source** de 300 mm : 138 objets, 43 670 commandes Stitch, 43 660 segments
visibles ; l'emprise cousue mesure 230,202 × 276,870 mm après suppression du fond. Le générateur
et le script de reproduction sont décrits dans le rapport ; aucun projet binaire supplémentaire
n'est nécessaire dans ce dossier.

Références Qt : [diagnostics du Scene Graph](https://doc.qt.io/qt-6.8/qtquick-visualcanvas-scenegraph.html),
[signal frameSwapped](https://doc.qt.io/qt-6.8/qquickwindow.html#frameSwapped).
