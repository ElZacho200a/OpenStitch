# Plan de benchmark des backends de segmentation (cadre de PR-01)

Objectif : décider, par mesure, quel backend (ou aucun) améliore la chaîne, sans présupposé. Aucune métrique n'est inventée : une valeur non calculable vaut `null` avec une raison, jamais une valeur de remplacement.

## Corpus (manifeste versionné)

Par image : `source_hash` (SHA-256), dimensions en px et **en mm brodés**, type (illustration, photo, logo, trait), droits/licence, **objets protégés** (id, famille, boîte, ancrage), **relations** (`under/over`, parent), détails fins attendus (cordages, yeux, trous…). Fixtures synthétiques libres générées de façon déterministe ; images réelles seulement si leurs droits sont consignés.

Cas bloquant obligatoire : **scène maritime** (voilier + phare + mer + écume + éléments fins).

## Configurations comparées

Base CIELAB (`segmentation::segment`) · SAM 2.1 (profils existants) · puis, par PR : HQ-SAM 2, MobileSAM, Florence-2 / Grounding DINO avec invites, SAM 3 (après licence). Des poids absents donnent un `skip` explicite, pas un échec.

## Métriques

1. **Instances principales présentes** (bloquant : un sujet principal absent = échec, quel que soit le reste).
2. Frontières (distance de contour en mm), trous préservés, traits fins conservés (longueur/largeur en mm), sous-régions utiles vs sur-segmentation.
3. Couverture de l'image et du fond intentionnel.
4. Effet aval via le moteur : `project_check`, points, coupes, sauts, densité ; l'aperçu est un rendu OpenStitch.
5. Coût : temps, mémoire, taille des poids.

## Reproductibilité

Entrées épinglées (corpus, backend, révision, hash de modèle, graine). Sorties : rapport JSON, images de comparaison, résumé Markdown. Exécution hors ligne ; aucun téléchargement obligatoire en CI.

## Critères d'adoption d'un backend

Gagne sur les instances principales **sans** régression sur la base, coût acceptable, licence code + poids validée, repli hors ligne intact.

## Précisions issues de la veille (cibles à confirmer, pas des résultats)

- **Taille du corpus** : 40 à 60 images en six familles (logos 8–10, aplats stylisés 8–10, scènes picturales dont marine et marais 8–10, structures fines 6–10, emblèmes/animaux 6–10, cas adversariaux 4–10). 10–15 % tenus hors du réglage comme jeu final. PR-01 commence par un **petit jeu synthétique redistribuable** ; le corpus complet se construit ensuite, hors dépôt pour les images privées.
- **Configurations** : B0 (CIELAB actuel), B1 (SAM 2.1 actuel), C1 (couleur + graphe + plan humain), C2 (Florence-2 + SAM 2.1 + réparation géométrique), C3 (Florence-2/Grounding DINO + HQ-SAM 2), C4 (SAM 3, si licence), C5 (MobileSAM), C6 (planner Qwen3-VL à générateur de masques identique à C2, ablation), C7 (VTracer 1, vectorisation seule). Comparaisons à source, dimensions, palette, tissu, moteur de points et budget d'actions humaines identiques.
- **Portes bloquantes avant tout score** : aucun objet `protected` manquant, zones `unstitched` respectées, `project_check` et DST du même OSP. Score pondéré de départ (hypothèse à valider) : 35 % fidélité sémantique, 25 % masque/géométrie, 25 % proxy textile, 15 % effort humain.
- **Seuils provisoires** : −20 % d'actions humaines relatives vs B1 sans dégradation des détails protégés ; pas plus de +10 % d'erreurs bloquantes.
- **Rapport par run** : `run.json`, `model_manifest.json`, `source_sha256`, `scene.json`, `proposals.json`, `masks/`, `semantic.json`, `.osp`, `.dst`, `preview_openstitch.png`, `project_report.json`, `metrics.json`, `errors.json`. Les échecs sont attribués : modèle, masquage, fusion, géométrie ou moteur de points. Les aperçus sont toujours des rendus OpenStitch.
