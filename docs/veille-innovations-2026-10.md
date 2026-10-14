# Veille innovations broderie — 2026-10-13

Complète `docs/roadmap-parite-hatch.md` (source de vérité des statuts) et
`docs/roadmap-rd-brevets.md` (brevets déjà triés). Cette veille cherche ce que
ces deux documents ne couvrent pas : littérature scientifique récente, brevets
absents de `docs/source/patent-research.md`, pratiques de l'écosystème libre.

## Ce qui a été lu, et ce qui ne l'a pas été

Révision du jour après lecture des textes. La première version de cette note
reposait sur des résumés de moteur de recherche ; **elle contenait des erreurs**,
corrigées ci-dessous (marquées « corrigé »).

| Source | Lecture |
|---|---|
| StitchOver, arXiv 2609.08311 (UIST '26) | texte intégral |
| OriStitch, arXiv 2412.02891 (SCF '25) | sections multi-cadrage et export |
| Magne et al., arXiv 2606.01910 (CGF) | résumé, section broderie, limites |
| Li & Mould, *Continuous Line Drawings* (2014) | principe de l'algorithme |
| Kaplan & Bosch, *TSP Art* (Bridges 2005) | résumé, introduction |
| MSEmbGAN (Hu et al., TVCG) | résumé, §3.2 (classifieur) |
| Shaping Lace (SCF 2024, PMC12645458) | extraits |
| US8851001B2 (Melco) | revendications et description |
| US5343401A, US6584921B2, US8818773B2 | statut, résumé, passages ciblés |
| **Non lus** | US9702070B2 et US10590580B2 (erreur 503), US11727615 (404) ; Cui 2017 et Liu 2023 (Wiley/ACM : 403) ; Hu 2024 / Guan 2021 cités par MSEmbGAN |

Statuts juridiques : ceux de Google Patents, qui précise lui-même que ce n'est
pas une conclusion juridique. Aucun avis. Brevets et articles sont des sources
d'idées ; toute implémentation est indépendante (Apache-2.0, Tier 0 de
`GUARDRAILS.md`). Déjà intégré, donc hors veille : Liu et al., CGF 2023
(`docs/source/directional-fill.md`).

## Synthèse

| # | Innovation | Entrée liée | Priorité |
|---|---|---|---|
| V1 | Compensation d'étirement calibrée par pièce-test | HP-ENG-001, HP-FAB-001 | P1 |
| V2 | Classification satin/tatami par érosion : confirme notre approche, rien à apprendre | HP-AUTO-009, RD-PAT-004 | informatif |
| V3 | **Brevet actif Melco** sur le choix du point selon la taille : risque à instruire | HP-AUTO-009, HP-FAB-001 | **à traiter avant de coder** |
| V4 | Multi-cadrage : couverture gloutonne + repères de recalage | HP-HOOP-004 | P2 |
| V5 | Dualité arbre/contour : parcours unique d'un squelette | HP-SEQ-004, RD-PAT-008 | P2 |
| V6 | Stippling + TSP pour le pointillé continu | HP-STI-013, HP-STI-017 | P3 |
| V7 | Zones à risque autour des coutures (JumpStitches) | HP-FAB-001 | P3 |
| V8 | Rendu par bibliothèque d'images de points (US8818773, expiré) | HP-VIEW-001 | P2 |
| V9 | Idées de l'écosystème libre (concepts seulement) | HP-STI-005/010 | P2 |
| V10 | Dentelle auxétique | HP-SPEC-003 | P3 |

## V1 — Compensation calibrée (inchangé, renforcé)

- Aucun modèle physique de l'étirement dans ce que j'ai lu. Les sources
  techniques décrivent la compensation comme règle empirique.
- US5343401A (« Expired - Lifetime » selon Google) décrit la compensation de
  tirage comme un **pourcentage de sur-couture** du satin et du tatami, avec
  l'exemple d'un disque rempli qui devient un ovale. Idée libre de droits.
- Idée à nous (hors brevet) : une **pièce-test** (colonnes de largeurs
  croissantes, carré de tatami, disque) que l'utilisateur brode et mesure ;
  les écarts alimentent une table par profil de tissu. Acceptation proposée :
  pièce-test déterministe (DST identique entre deux exécutions), table
  sérialisée dans le `.osp`, appliquée via `effective_sequence`. Preuve
  humaine requise (HP-PHYS-001).
- La compensation « push » (largeur du bout d'élément → réduction, par type de
  tissu) est décrite dans US8851001, **actif** : voir V3. Ne pas la reproduire
  telle quelle.

## V2 — MSEmbGAN : le « classifieur » n'est pas appris (corrigé)

La note précédente le présentait comme un classifieur profond. **C'est faux** :
dans l'article (§3.2.1, éq. 1), le type de point de chaque région de couleur
vient d'une **érosion morphologique** du masque avec deux noyaux circulaires de
rayons λ_f et λ_w :

- le masque disparaît à l'érosion λ_f → « flat » (trait fin) ;
- il survit à λ_f mais disparaît à λ_w → **satin** ;
- il survit à λ_w → **tatami**.

Les régions viennent de l'algorithme *quick shift*. Autrement dit, c'est un
seuil de largeur sur la transformée de distance, ce que `satinability.cpp` et
RD-PAT-004 font déjà, en plus riche. Les parties apprises (GAN de synthèse
d'image, colorisation) produisent un **aperçu**, pas des points : sans intérêt
pour OpenStitch. Le jeu de données (30 000 images, trois types de point plus un
mixte) a été dessiné avec Wilcom 9.0 ; sa licence n'est pas vérifiée, donc ne
pas l'embarquer. Conclusion : abandonner l'idée « classifieur appris ».

## V3 — Brevet actif Melco US8851001B2 (nouveau, important)

- Statut Google : **actif, échéance 2032-09-23**. Famille : US9702070B2 (actif),
  deux demandes abandonnées. Inventeurs Albano et Stengel.
- Revendications indépendantes 1 et 19 (lues) : après modification du dessin
  (agrandissement ou changement de tissu), **déterminer la taille d'un élément
  en lui attribuant un type de point temporaire, en générant des points
  temporaires et en mesurant leur largeur de point**, puis **choisir le type de
  point (dont le type de sous-couche : centre, bord, zigzag, remplissage) en
  comparant cette taille à un ensemble de valeurs**.
- Pourquoi c'est un risque pour nous : `HP-AUTO-009` et `RD-PAT-004` choisissent
  satin ou tatami d'après la largeur, et `HP-FAB-001` (profils de tissu) pourrait
  changer le point selon le tissu. Notre mesure se fait sur la **géométrie**
  (squelette, transformée de distance), pas sur des points temporaires, ce qui
  s'écarte du libellé de la revendication 1. **C'est mon analyse, pas un avis
  juridique.**
- Action : ne pas implémenter de mesure « par points temporaires » ; faire
  relire par une personne compétente avant HP-FAB-001 et tout recalcul du type
  de point lors d'un redimensionnement. Ajouter le brevet à la liste des brevets
  à éviter de `patent-research.md`.
- La description (pas les revendications lues) mentionne aussi une sous-couche
  continue par parcours de graphe (tour d'Euler, parcours en largeur) et la
  compensation « push » ; les revendications de US9702070B2 n'ont **pas** pu
  être lues.

## V4 — Multi-cadrage

- OriStitch (algorithme lu) : problème de recouvrement d'ensemble, NP-difficile,
  résolu par une heuristique gloutonne. Candidats = produit des coordonnées x et
  y minimales des boîtes englobantes ; chaque candidat est essayé en deux
  orientations ; on retient le cadre qui contient entièrement le plus d'unités
  restantes ; on retire et on recommence. Coût O(N⁴), le papier le juge
  polynomial donc acceptable.
- Limite pour nous : le cadre doit contenir une unité **entière**. Un objet de
  broderie plus grand qu'un cadre échoue (l'algorithme s'arrête en échec) ; il
  faut donc soit travailler au niveau objet, soit découper les gros objets.
- Repères de recalage : OriStitch coupe de petits carrés au laser et les brode
  sur le stabilisateur ; StitchOver ajoute des **points de jonction** (zigzag en
  verrou) entre deux cadrages pour garder la continuité d'un fil conducteur. La
  documentation Wilcom décrit des repères cousus dans la zone de recouvrement,
  non enregistrés dans le design.
- Brevet expiré US6584921B2 (« Expired - Fee Related ») : interface qui affiche
  un design multi-fichiers, multi-sections ou multi-cadrages avec une propriété
  (section, fichier, position de cadre) sur un second axe. Idée d'IHM pour
  `HP-HOOP-004` ; statut à vérifier avant tout emprunt.
- Entrée existante : HP-HOOP-004 (« À faire »). Ajouter aux critères : placement
  glouton déterministe (tri explicite des candidats, pas de dépendance à l'ordre
  d'itération), repères de recalage, zone de recouvrement réglable.

## V5 — Dualité arbre/contour (Li & Mould, 2014)

Un arbre (graphe connexe sans cycle) possède un contour extérieur qui forme
automatiquement **un seul trajet fermé sans auto-intersection**. L'algorithme
construit un dendrite (points de pointillé reliés par recherche de chemin à coût
minimal), le « double » en érigeant un mur synthétique, et obtient une ligne
continue qui longe l'arbre des deux côtés. Les auteurs critiquent le TSP :
chaque tournée est une ligne continue, mais pas l'inverse, et le contrôle est
difficile.

Lien direct avec le dépôt : le squelette d'une forme fine est déjà un arbre, et
le parcours des deux côtés d'un arbre est l'idée du routage des colonnes satin
(`route_columns`, RD-PAT-008). À comparer à notre routage avant d'en tirer
quoi que ce soit ; ne pas supposer que c'est meilleur.

## V6 — Stippling + TSP (Kaplan & Bosch, 2005)

Bosch et Herman : « villes » distribuées selon la densité de l'image, puis
solveur TSP heuristique, donnant une courbe fermée simple. Kaplan ajoute deux
distributions plus esthétiques (pointillé de Voronoï pondéré de Secord,
tramage ordonné). Pour la broderie : pointillé continu sans coupe
(`HP-STI-013`, `HP-STI-017`). Risque : la densité de fil n'est pas contrôlée.
P3.

Magne et al. (CGF, ETH Zurich) : courbe B-spline unique optimisée par *score
distillation* d'un modèle de diffusion, à partir d'un texte ou d'une image.
**Environ 15 minutes sur une RTX 4090**, donc inadapté à une application de
bureau sans GPU. Seul élément réutilisable : leur démonstration qu'une même
trajectoire reçoit plusieurs **styles de point** (satin, satin perlé, point
« bean ») ; cela renforce `HP-STI-001` et `HP-STI-003` (points le long d'un
tracé), sans lien avec leur méthode. Leur code est annoncé en accès libre
(github.com/tanguymagne/SLDgen) ; licence non vérifiée.

## V7 — StitchOver : coutures et zones à risque (corrigé)

Corrections : la publication est **UIST '26** (DOI 10.1145/3830398.3830717) et
non CHI '26 ; elle cible la **broderie technique au fil conducteur** (capteurs),
pas la broderie décorative. Évaluation sur Brother PE800, cuir PU, 10 essais par
cas : une couture donne 7/10 défauts (tous des boucles) contre 0/10 avec les
JumpStitches ; sur une couture à trois branches, 10/10 contre 4/10.

Mécanisme (lu) : le pied-de-biche se soulève sur la couture et modifie
tension et friction. Quatre zones à risque (couture S, côté bas L, côté haut H,
arête E). On supprime les points d'aiguille dans ces zones, on saute au-dessus,
et on ajoute des points d'ancrage parallèles à la couture. Longueur minimale du
saut : (P_r + W_s + P_l) / cos α. Autres éléments : ShortStitches de 0,5 mm près
des points qui définissent la forme, UnderlayStitches pour aplatir les
intersections de coutures, avertissement si l'entrée ou la sortie tombe en zone
à risque. Implémenté en Python avec PySide6 (sortie SVG).

Pertinence pour OpenStitch : faible pour la broderie décorative. Idée à garder
si l'on ajoute des objets « zone interdite » : un avertissement d'analyse
(`stitch_analysis`) plutôt qu'une réécriture du tracé. P3.

## V8 — Rendu par bibliothèque d'images (US8818773)

« Expired - Fee Related » selon Google (la date d'échéance affichée, 2030, est
incohérente avec ce statut : à vérifier). Principe lu dans le résumé : pour
chaque point, on calcule sa longueur et l'angle d'éclairage, on prend dans une
base d'images de points celle qui correspond, et on la place sur le canevas ; tous
les points semblent éclairés dans la même direction. Pertinence : pour
`stitch_render`, une base pré-rendue indexée par longueur et angle serait un
chemin rapide pour HP-VIEW-009 (fluidité sur gros designs). À mesurer avec le
banc avant de choisir ; notre rendu actuel est déjà qualifié de réaliste.

## V9 — Écosystème libre

Ink/Stitch (GPL) : **ne pas lire ni copier son code** (Tier 0). Fonctions
notées sur sa page publique (versions 3.1 à 3.3) : dégradé linéaire en
remplissage, longueur de point aléatoire, tartan, satin multicolore, retrait des
points courts en satin, lettrage multicolore. Les spécifier par leur comportement
observable. P2.

## V10 — Dentelle auxétique (Shaping Lace, SCF 2024)

Dentelle autoportante brodée sur stabilisateur hydrosoluble (fil polyester 40,
Janome Skyline S9 et Brother SE1900, logiciel Embrilliance), motifs auxétiques
et kirigami. Pas d'apport logiciel : confirme l'intérêt de HP-SPEC-003. P3.

## Brevets pour la suite

| Numéro | Statut Google | Sujet | Action |
|---|---|---|---|
| US8851001B2 | actif, 2032 | choix du point par taille, push, sous-couche continue | voir V3 |
| US9702070B2 | actif | famille du précédent | lire les revendications (503) |
| US5343401A | expiré | compensation en pourcentage, densité | lu, déjà cité |
| US6584921B2 | expiré (frais) | affichage multi-cadrages | lu (résumé) |
| US8818773 | expiré (frais) | rendu par base d'images | lu (résumé) |
| US10590580, US11727615 | inconnu | broderie vectorielle ; édition de chemin | à relire |

## Suite proposée

1. Faire relire V3 (US8851001) avant de lancer HP-FAB-001 ou tout recalcul du
   type de point au redimensionnement.
2. Créer la sous-entrée V1 (pièce-test + table) sous HP-ENG-001, au format §0.1.
3. Compléter HP-HOOP-004 avec V4.
4. Relire les documents restants (US9702070, US10590580, US11727615, Cui 2017)
   quand l'accès sera possible.
