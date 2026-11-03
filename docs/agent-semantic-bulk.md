# Retouches collectives par famille semantique — C01

Fonctionne sur les OSP crees par le pont B03 avec un sidecar `*.semantic.json`. Les objets sont identifies d'apres leurs noms persistants et non pas uniquement d'apres leur couleur de fil.

    python3 scripts/openstitch_semantic_bulk.py scene.osp --family morse --semantic-prefix morse12 --type tatami --rgb '#D8743B'
    python3 scripts/openstitch_semantic_bulk.py scene.osp --semantic-prefix morse12.tusk --params '{"strategy":"skeleton"}' --apply --out scene-retouchee.osp

Sans `--apply`, l'outil effectue uniquement un lot `project apply --dry-run` via le vrai CLI. Avec `--apply`, il cree un nouveau projet et recopie le sidecar, sans modifier l'original. Selecteurs obligatoires, limite de 20 objets par defaut, refus des objets verrouilles, refus d'une sortie existante. Chaque lot est transactionnel du cote OpenStitch.

Ce script est un pont Python C01 ; l'index semantique natif dans l'OSP et l'integration a l'interface bureau restent a developper.
