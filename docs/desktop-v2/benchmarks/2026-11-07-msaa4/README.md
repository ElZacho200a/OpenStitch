# Mesures natives Studio, MSAA 4× — 2026-11-07

Passe finale mesurée le **2026-11-07 en Europe/Paris** (les horodatages UTC des JSON indiquent
le 2026-11-06 vers 23:09). Release, Qt 6.8.3, Windows 11 25H2, AMD Ryzen 7 7700X ; écran
Samsung U28E590 à 59,997 Hz, DPR 1,5, viewport 1280 × 800 pixels logiques pour les deux fronts.
Le [journal D3D11](adapters.txt) confirme la boucle threadée, **4 échantillons MSAA** et
**NVIDIA GeForce RTX 5060 Ti** sélectionnée. Pilote Windows : 32.0.16.1088.

Chaque cas D3D11/Widgets est un processus séparé, avec 30 gestes de chauffe puis 240 échantillons.
Les limites et tables sont dans le [rapport](../../18-studio-benchmarks.md). Les valeurs Widgets
mesurent la fin de peinture CPU et ne fournissent aucun FPS présenté par le compositeur.

| Scène | Quick D3D11 | Widgets |
|---|---|---|
| 1 000 points | [JSON](1000-quick.json) | [JSON](1000-widgets.json) |
| 10 000 points | [JSON](10000-quick.json) | [JSON](10000-widgets.json) |
| 50 000 points | [JSON](50000-quick.json) | [JSON](50000-widgets.json) |
| 100 000 points | [JSON](100000-quick.json) | [JSON](100000-widgets.json) |
| Tentabrode, 43 670 points | [JSON](real-quick.json) | [JSON](real-widgets.json) |

Un essai matériel supplémentaire utilise OpenGL 4.6 NVIDIA, 100k points et MSAA 4× :
[JSON complet](100000-opengl.json), [preuve du renderer OpenGL](opengl-adapter.txt). Il comporte
**120 échantillons après 20 gestes de chauffe**, contrairement à la passe D3D11 (240/30) ;
il constitue une validation ponctuelle du backend, sans comparaison statistique contrôlée.

Les échantillons numériques sont intacts ; seul le champ `project` a été remplacé par le nom
`tentabrode-300mm.osp`. [Métadonnées et empreintes SHA256](machine.json),
[synthèse arrondie](summary.json), [extraits QSG_INFO](adapters.txt). Les dix JSON D3D11/Widgets
et le JSON OpenGL sont la référence numérique complète. Les pointeurs de fenêtres/QRhi et les
chemins de cache locaux ont été omis des extraits de logs.

Le projet provient de `tests/fixtures/tentabrode.png`, généré par `run_digitize` avec une hauteur
d'image source de 300 mm : 138 objets, 43 670 commandes Stitch, 43 660 segments visibles et
emprise cousue 230,202 × 276,870 mm. Les commandes de génération figurent dans le rapport.

La [première passe MSAA 1×](../2026-11-06/README.md) est conservée séparément. Une seule passe
existe par configuration ; les écarts ne sont pas attribuables au seul changement de MSAA sans
répétitions ni contrôle de la variance.

Références Qt : [diagnostics du Scene Graph](https://doc.qt.io/qt-6.8/qtquick-visualcanvas-scenegraph.html),
[signal frameSwapped](https://doc.qt.io/qt-6.8/qquickwindow.html#frameSwapped).
