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
