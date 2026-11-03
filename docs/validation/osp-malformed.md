# Régressions de chargement .osp — HP-QA-003

Le script `scripts/qa/fuzz_osp.py` fabrique **61 fichiers invalides**
reproductibles (ZIP tronqués, manifeste absent, versions incohérentes,
corruptions déterministes) et les soumet à `openstitch-cli project inspect
--no-stitches`. Chaque fichier doit être **rejeté proprement** (code de
sortie non nul), sans sortie anormale ni dépassement du délai de 4 secondes.

Commande (après construction du binaire) :

```sh
python scripts/qa/fuzz_osp.py --cli build/linux-core/apps/cli/openstitch-cli
python -m unittest discover -s scripts/qa -p 'test_*.py'
```

La CI `linux-core` lance les tests Python et le script contre le **binaire
C++ réel** après `cmake --build`. Ce n'est **pas** un fuzzer natif, ni un
corpus de fichiers utilisateurs réels. Aucune migration ou séquence de
points n'est écrite. En cas de défaut, l'identifiant du cas reproduit la
même entrée sur toute plateforme.
