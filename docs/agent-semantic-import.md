# Import semantique (B03 partiel)

Le pont Python `scripts/openstitch_semantic_import.py` valide les masques PNG du format `openstitch.semantic/1`, les vectorise avec OpenCV puis les importe avec `openstitch-cli project op import_svg`.

Installer `opencv-python-headless` dans l'environnement de l'agent. Exemple :

    python3 scripts/openstitch_semantic_import.py scene.json --out scene.osp --cli openstitch-cli

Chaque objet conserve le préfixe `sem:<semantic_id>` dans son nom. Un sidecar `scene.semantic.json` relie ID, famille, parent et masque. L'ordre `under/over` est conservé lors de la création. Le moteur natif assure tous les points ; aucune génération de points n'est recodée.

**Natif (2026-10-23).** L'opération `semantic_import` (CLI `project op`, MCP `semantic_import`) fait la même chose sans Python ni OpenCV externe, de façon transactionnelle ; le pont Python reste utilisable (même convention de nom `sem:<id> <n>`).

**Limite** : il s'agit d'un pont externe, pas d'un import C++ natif. Les intentions `directional` et `satin` sans guides/rails sont refusées ; un auto-satin impossible fait échouer l'import sans livrer l'OSP. Le rendu doit être vérifié.
