# R02 — Comparaison des technologies de calcul

Synthèse de la [recherche documentaire](research.md), de l'[ADR 0006](../../adr/0006-compute-moteur-experimental.md),
des prototypes R01 ([D3D11 masques](../../validation/r01/prototype-d3d11.md),
[tatami par lots](../../validation/r01/tatami-batch.md)) et de la nouvelle mesure R02 sur GPU de
bureau ([résultats](benchmark-results.md#gpu-d3d11-sur-gpu-de-bureau)).

## Ce que les mesures imposent

1. **Les charges du moteur sont petites et fragmentées.** Le parcours complet le plus lourd du
   corpus prend 300 ms en baseline. Il se compose de centaines d'appels de 0,1 à 5 ms sur des
   masques par région, des polylignes et des listes de points.
2. **Le coût fixe d'un aller-retour GPU domine.** Sur RTX 5060 Ti de bureau, l'envoi et la
   lecture retour seuls des 106 masques de distance de tentabrode prennent 73 ms au p50, contre
   18,7 ms pour **tout** le calcul CPU. Même un noyau de durée nulle perdrait d'un facteur 4.
3. **La sortie doit rester identique octet pour octet.** Les noyaux R01 sont exacts parce que
   purement entiers (morphologie, chanfrein Q16). Le tirage du tissu, les distances
   point-segment et les remplissages sont en FP64 avec `exp`, `tanh` et `hypot`. Les fonctions
   transcendantes GPU (HLSL D3D11 n'a pas d'`exp` FP64) varient selon le pilote.

## Grille de décision

| Critère | CPU optimisé (algorithme + `parallel_for`) | D3D11 compute | Vulkan compute | QRhi compute | CUDA |
|---|---|---|---|---|---|
| Performance de bout en bout mesurée | **gains de 20 à 58 % au p50 (R02, A/B entrelacé)** | perd 3 à 5× (R01, R02) | non mesuré : mêmes coûts de transfert attendus | non mesuré | non mesuré |
| Portabilité | toutes machines | Windows | Windows, Linux | Windows, Linux, macOS via Qt | NVIDIA seul |
| Intégration (cœur sans Qt) | native | adaptateur Win32 isolable (prototype R01) | possible, SDK et SPIR-V à ajouter | **impossible dans le cœur** (Qt, API semi-privée) | toolkit propriétaire, casse `linux-core` |
| Maintenance | un seul chemin | deux chemins bit-exacts | deux chemins + barrières/mémoire explicites | deux chemins, liés à la cadence de rendu | deux chemins |
| Mémoire | RAM seule | RAM + VRAM + tampons de transit | idem | idem | idem |
| Déploiement | aucun | système | loader + pilote | inclus dans Qt | runtime NVIDIA |
| Fiabilité / repli | sans objet | repli CPU requis (perte de périphérique) | idem | idem | idem |
| Déterminisme | total (assemblage séquentiel) | seulement en arithmétique entière | idem | idem | contrôlable mais propre au matériel |
| Machines modestes | gain conservé (le monofil progresse aussi) | GPU intégré : transfert moindre, calcul aussi | idem | idem | absent |

## Décision

1. **Moteur : CPU uniquement pour l'instant.** Les gains R02 viennent de réécritures
   algorithmiques exactes et d'un parallélisme déterministe. Aucun noyau GPU ne bat le CPU sur
   une charge réelle du corpus, transferts compris. Aucune dépendance GPU n'est ajoutée.
2. **Studio : le GPU reste celui du scene graph** (RHI/D3D11) pour l'affichage, déjà en place
   ([ADR 0005](../../adr/0005-studio-qtquick-gpu.md)). L'accélération de l'affichage ne
   confond pas celle de la numérisation.
3. **Conditions pour rouvrir la piste GPU** (une seule suffit, à mesurer) :
   - une étape à résultat entier ou visuel dépasse 20 ms au p50 de bout en bout **après**
     optimisation CPU, sur une donnée qui peut rester résidente en VRAM sur plusieurs étapes ;
   - une fonctionnalité nouvelle (simulation physique de fil dense, rendu réaliste haute
     définition hors Studio) dont le calcul se mesure en centaines de millisecondes par image.
   Le cas échéant, backend D3D11 dans un adaptateur isolé (contrat de l'ADR 0006) pour Windows,
   Vulkan si Linux devient une cible produit. CUDA reste exclu.
4. **Alternatives CPU non retenues pour l'instant** : SIMD explicite (Highway, xsimd) et
   oneTBB. Les postes restants sont dominés par la structure des algorithmes, pas par le débit
   arithmétique ; un pool de fils persistant maison est préférable à une dépendance (voir
   [roadmap](roadmap.md)).
