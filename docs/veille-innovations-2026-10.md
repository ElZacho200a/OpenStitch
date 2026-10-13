# Veille innovations broderie — 2026-10-13

Complète `docs/roadmap-parite-hatch.md` (source de vérité des statuts) et
`docs/roadmap-rd-brevets.md` (brevets déjà triés). Cette veille cherche ce que
**ces deux documents ne couvrent pas** : littérature scientifique récente,
brevets absents de `docs/source/patent-research.md`, pratiques de l'écosystème
libre.

## Limites (à lire d'abord)

- Recherche faite **uniquement à partir de résumés de moteur de recherche** :
  `WebFetch` n'a pas pu joindre arxiv.org (DNS). **Aucun article n'a été lu en
  texte intégral.** Chaque affirmation ci-dessous est à vérifier dans la source
  avant de coder (comme l'exige l'en-tête de la roadmap Hatch).
- Statuts juridiques des brevets : ceux affichés par Google Patents, qui précise
  lui-même que ce n'est pas une conclusion juridique. Aucun avis.
- Les brevets et le code d'autres projets sont des **sources d'idées** ; toute
  implémentation est indépendante (Apache-2.0 strict, Tier 0 de `GUARDRAILS.md`).
- Déjà intégré, donc hors veille : Liu et al., *Directionality-Aware Design of
  Embroidery Patterns*, CGF 2023 (`docs/source/directional-fill.md`, PR #16).

## Synthèse

| # | Innovation | Source | Entrée liée | Priorité proposée |
|---|---|---|---|---|
| V1 | Compensation d'étirement calibrée par pièce-test, fonction de la largeur de colonne et du tissu | pratique (Hatch/Wilcom), US5343401 (expiré) ; **évite** US8851001 | HP-ENG-001, HP-FAB-001 | P1 |
| V2 | Zones d'exclusion et « sauts » autour des coutures/épaisseurs | StitchOver (CHI '26) | HP-FAB-001, nouvelle sous-entrée | P2 |
| V3 | Tracé en ligne unique (TSP art, arbre de Li & Mould, optimisation sémantique) | Kaplan 2005 ; Li & Mould 2014 ; Magne et al. CGF | HP-STI-013, HP-STI-017, HP-SEQ-004 | P2 |
| V4 | Classifieur de type de point appris, et corpus d'évaluation | MSEmbGAN (TVCG 2024) | HP-AUTO-009 | P2 (évaluation seulement) |
| V5 | Multi-cadrage : repères de recalage, placement glouton des cadres | docs Wilcom ; OriStitch (SCF '25) | HP-HOOP-004 | P2 |
| V6 | Rendu fil réaliste : modèles de fil procéduraux | Cui 2017 ; littérature fil/tissu | HP-VIEW-001/002 | P3 |
| V7 | Idées de l'écosystème libre (concepts seulement, **pas de code GPL**) | Ink/Stitch 3.x | HP-STI-005/010, HP-ENG-005 | P2 |
| V8 | Broderie fonctionnelle : dentelle auxétique, chaînette sur film thermorétractable | Shaping Lace (SCF '24) ; EmbroChet (UIST '25) | HP-SPEC-003, HP-STI-020 | P3 |

## V1 — Compensation calibrée

- **Constat littérature** : aucun article trouvé qui modélise physiquement
  l'étirement (tension du fil, déformation du tissu). Les modèles masse-ressort /
  éléments finis existent pour le tricot et la couture technique, pas pour la
  broderie décorative. L'industrie utilise des règles empiriques et des essais de
  broderie (valeurs indicatives d'un guide commercial : environ 0,15 mm pour une
  colonne de 2 mm, 0,25–0,30 mm pour 5–6 mm — non vérifiées, dépendent du tissu).
- **Brevets** :
  - US5343401A, *Embroidery design system* : expiré selon Google, décrit la
    sur-couture en pourcentage du satin/tatami. Déjà cité dans le dépôt.
  - **US8851001B2 / US9702070B2 (Melco)**, *Method for improved stitch generation* :
    compensation de poussée (« push ») par table largeur → réduction du bout
    d'élément, éventuellement par type de tissu. Google les affiche **actifs** :
    ne pas reproduire cette méthode ; à mettre dans la liste des brevets à éviter.
- **Idée à nous (non tirée d'un brevet)** : un générateur de **pièce-test**
  (colonnes de largeurs croissantes, tatami carré, cercle) que l'utilisateur
  brode puis mesure ; les écarts alimentent une table de compensation par profil
  de tissu. Cela comble le trou de la littérature par de la mesure plutôt que par
  un modèle. Acceptation proposée : pièce-test déterministe (DST identique entre
  deux exécutions), table sérialisée dans le `.osp`, compensation appliquée par
  `effective_sequence`.
- **Preuve humaine requise** : broderie réelle (voir HP-PHYS-001).

## V2 — Coutures et zones sans couture

StitchOver (arXiv 2609.08311, CHI '26) : broderie sur tissu à coutures ; génère
des « JumpStitches » qui contournent la zone de couture et une sous-couche qui
aplatit les épaisseurs. Intérêt OpenStitch : une notion d'**objet « zone
interdite »** (le générateur saute au lieu de coudre) et de sous-couche
d'aplatissement. À lire : algorithme de contournement, sinon on retombe sur un
simple masque booléen. Dépend de HP-FAB-001.

## V3 — Ligne unique

- Kaplan, *TSP Art* (Bridges 2005) : points par pointillisme, puis tournée TSP.
- Li & Mould, *Continuous Line Drawings and Designs* (2014) : remplace la tournée
  par un arbre (chaque nœud visité deux fois), évitant les croisements gênants.
- Magne et al., *Single Line Drawing Generation via Semantics-Driven
  Optimization*, CGF (arXiv 2606.01910) : annonce des courbes directement
  exploitables par une brodeuse.
- Lien : `PhotoStitch` (HP-STI-017, partiel), stippling (HP-STI-013), fusion en
  un seul parcours (HP-SEQ-004). Atout : ce sont des **points de passage uniques,
  donc zéro coupe**. Risque : densité de fil incontrôlée si l'on ne l'impose pas.

## V4 — Classification de type de point apprise

- MSEmbGAN (Hu et al., IEEE TVCG 2024) : GAN de synthèse à plusieurs types de
  point ; son classifieur de type de point travaille sur des sous-régions
  segmentées. Jeu de données annoncé : plus de 30 000 images de broderie
  multi-points, 13 000 alignées avec leur contenu (types non nommés dans les
  résumés lus).
- Pour OpenStitch, la **synthèse d'image** n'a pas d'intérêt (nous produisons
  des points, pas un aperçu). Le seul intérêt est un **corpus pour mesurer**
  HP-AUTO-009 (choix satin/tatami). Licence du jeu de données **non vérifiée**.
- Avant tout apprentissage, finir l'éligibilité géométrique RD-PAT-004 : c'est
  la base de comparaison à battre.

## V5 — Multi-cadrage

- Pratique Wilcom/Hatch : repères de recalage cousus dans la zone de recouvrement
  entre deux cadrages, non enregistrés dans le fichier de design, visibles dans
  l'aperçu ; le recouvrement est un compromis facilité/précision.
- OriStitch (arXiv 2412.02891, SCF '25) : algorithme glouton de placement des
  cadres fondé sur les boîtes englobantes de paires de charnières.
- Entrée existante HP-HOOP-004 (« À faire ») : ajouter les repères de recalage
  aux critères d'acceptation.

## V6 — Rendu fil

Pas d'article de rendu de broderie au niveau du fil trouvé. Pistes voisines :
modèles procéduraux de fils ajustés sur mesures (rendu de tissu), Cui 2017
(*Image-based embroidery modeling and rendering*, CAVW : rendu par traits avec
éclairage de Phong). `stitch_render` couvre déjà le segment ombré ; gain
marginal, à traiter après HP-VIEW-001 complet.

## V7 — Écosystème libre (Ink/Stitch)

À considérer **comme des idées de fonctionnalité uniquement** : Ink/Stitch est
sous GPL, donc **ne pas lire ni copier son code** pour OpenStitch (Tier 0).
Fonctionnalités notées dans ses versions 3.1 → 3.3 : remplissage en dégradé
linéaire, longueur de point aléatoire en remplissage, tartan, satin multicolore,
retrait de points courts en satin (inset multi-valeurs), lettrage multicolore et
multi-lignes. Les ancrer dans HP-STI-005/010 et HP-ENG-005 en spécifiant le
comportement observable (documentation publique), pas l'implémentation.

## V8 — Broderie fonctionnelle

Shaping Lace (Glazko et al., SCF 2024, DOI 10.1145/3639473.3665792) : dentelle
autoportante produisant des effets kirigami et auxétiques ; EmbroChet (UIST '25) :
chaînette sur film thermorétractable. Hors cœur de cible, mais confirme la valeur
de HP-SPEC-003 (FSL) et HP-STI-020 (chaînette) ; un point de chaînette générique
servirait les deux.

## Brevets non encore dans `patent-research.md`

À lire en texte intégral avant tout tri (statut juridique non vérifié, sauf mention) :

| Numéro | Sujet | Remarque |
|---|---|---|
| US8851001B2, US9702070B2 | compensation « push » (Melco) | affichés actifs : à éviter, voir V1 |
| US11727615 | édition de chemin d'aiguille, réduction des sauts par fusion de segments | récent, probablement actif ; recoupe RD-PAT-008 |
| US10590580 | broderie définie par vecteurs | statut inconnu |
| US6584921B2 | modification des données de points | « Expired - Fee Related » selon Google |
| US8818773 | rendu de broderie par texture paramétrique | statut inconnu ; pertinent pour V6 |

## Suite proposée

1. Obtenir le texte intégral (PDF via un autre accès réseau) de : Magne et al.,
   StitchOver, OriStitch, MSEmbGAN ; corriger cette note si un résumé s'avère faux.
2. Créer la sous-entrée V1 (pièce-test + table de compensation) sous HP-ENG-001,
   au format §0.1 de la roadmap — c'est la plus rentable et la seule qui comble
   un vrai trou de la littérature.
3. Ajouter les brevets du tableau ci-dessus à `patent-research.md` après lecture.
