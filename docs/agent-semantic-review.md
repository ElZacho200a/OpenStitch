# Revue visuelle semantique — C04 (pont agent partiel)

L'outil `scripts/openstitch_semantic_review.py` produit un vrai rendu OpenStitch, un SVG annote avec les identifiants semantiques et un rapport JSON des mesures de couverture, debordement et avertissements issus du moteur.

    python3 scripts/openstitch_semantic_review.py resultat.osp --out-dir review --reference reference.png --cli openstitch-cli

Livrables : `preview.png`, `annotated.svg`, `review.json`, et avec reference `compare.html` et copie de la reference. Les boites de diagnostic sont approximees a partir des bornes des formes et des dimensions du rendu. Les alertes reposent sur les statistiques d'OpenStitch, jamais sur une image inventee.

Limites : l'affichage cote a cote n'est pas une metrique de similarite artistique automatique. Les erreurs de segmentation sémantique exigent encore la revision par l'agent ou l'utilisateur. Le rendu logiciel ne prouve pas la validation machine. Fonction CLI/pont agent ; integration bureau a developper.
