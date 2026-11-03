# Corpus du benchmark de segmentation

`marine-synth-01` : scène maritime synthétique (voilier, phare, mer, écume, cordages), 320×240 px = 160×120 mm
(2 px/mm). **CC0-1.0**, générée par `scripts/ai_bench/fixtures.py` — ne pas éditer à la main :

    python scripts/ai_bench/fixtures.py --write tests/fixtures/ai-benchmark

La CI vérifie que ces fichiers sont identiques à la régénération (`--check`) et que chaque empreinte du
manifeste (`openstitch.ai.benchmark-manifest/1`) correspond aux octets. Images privées ou sous droits :
hors dépôt, mêmes manifestes, passés par `--manifest`.
