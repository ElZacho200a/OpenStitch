# Réduction des coupes de fil par ponts cachés — veille et conception (2026-10-13)

Demande : réduire le nombre de coupes (par exemple avec des coutures cachées), en
appliquant l'optimisation **à la toute fin** du flux. Cette note rassemble ce
que disent les pratiques et brevets lus, l'état du code, et une conception de
passe finale. Entrées de roadmap : `HP-SEQ-006` et ses sous-entrées
(`docs/roadmap-parite-hatch.md`). **Mise à jour : le cœur est implémenté**
(`libs/stitch_generation/src/hidden_travel.*`, voir §5) ; ce qui précède dans
cette note reste la conception et les sources.

## 1. Ce qui a été lu

Sources lues en texte intégral ou en extraits ciblés (le texte récupéré peut
être incomplet, notamment pour les pages de brevets) :

| Source | Apport |
|---|---|
| Wilcom, *Branching* (2019) | objets qui se touchent regroupés en un seul parcours ; « les segments recouverts sont cachés derrière les objets cousus plus tard » ; un point d'entrée et un de sortie pour le groupe |
| Wilcom, *Travel Toolbar* (2019) | on « imbrique » une course de liaison entre deux objets pour éviter une coupe ; l'objet suivant la recouvre |
| Wilcom, *Minimizing thread trims* (2020) | une coupe naît d'un changement de couleur ou d'une distance entre objets au-dessus d'un seuil (onglet Connecteurs) ; remède : Branching + Closest Join + resequençage |
| Hatch, *Avoid the JUMP* | une course de liaison entre objets de même couleur qui ne se touchent pas n'est acceptable que si d'autres objets sont cousus par-dessus ; Closest Join réduit les sauts sans les supprimer |
| Ink/Stitch, *Stitch Path Optimization* | « ne pas cacher les sauts par des coupes : elles font un envers désordonné et ralentissent la machine » ; outil *jump to stroke* qui crée une course de liaison à cacher sous d'autres éléments |
| US8271123B2 (Brother) | description : si les points d'un segment de liaison seront recouverts par des points cousus plus tard, on coud un point droit sur ce segment ; **revendication 1 lue : génération de segments à partir d'une image (angles, couleurs)** ; aucune revendication dépendante lue sur le recouvrement |
| US5189623A (expiré selon Google) | point d'entrée et de sortie choisis pour minimiser le transfert entre zones successives : c'est le « Closest Join » déjà fait (HP-ENG-010) |

Non lus : EP1102881 et US20020183886 (pages brevet sans texte exploitable),
US11727615 (404), US9200397 (décrit, d'après le résumé de recherche seulement,
un ordre de fragments qui limite les coupes par des points droits entre
sous-régions), US6860214B1 / US20050061220A1 (« broderie en relief » : points de
liaison recouverts ensuite, d'après le résumé de recherche seulement).

**Aucun article scientifique spécifique** sur la minimisation des coupes n'a été
trouvé : la littérature lue traite l'ordre des segments (voisin le plus proche
dans OriStitch) mais pas le recouvrement des liaisons. L'idée est de pratique
courante dans les logiciels commerciaux, pas une nouveauté de recherche. Aucun
chiffre de gain de temps par coupe n'a été trouvé non plus : ne pas en inventer,
le mesurer (HP-PROD-002).

## 2. État du code (vérifié)

- `finish_sequence` (`libs/stitch_generation/src/finish.cpp`) est **déjà la
  dernière passe d'`effective_sequence`**, après les retouches manuelles ; les
  index des retouches visent la vue brute, que cette passe ne modifie pas. C'est
  l'emplacement naturel, et il respecte la contrainte « tout à la fin ».
- Règle actuelle (l. 248-257) : entre deux objets distincts, **tout déplacement
  réel est coupé**, quelle que soit sa longueur (`objectGap`, correctif
  2026-10-08 contre un fil tendu visible). Un déplacement qui dépasse
  `trim_threshold` (3 mm) ou précède un changement de fil est coupé aussi.
  Chaque coupe ajoute un verrou de sortie et un verrou d'entrée (BackAndForth,
  0,8 mm, 2 passes).
- Les points sont des `StitchCommand{pos, type, source, pass}` ; la passe
  `StitchPass::Travel` (« déplacement caché ») existe déjà, ainsi que
  `StitchPass::Lock`. Une coupe retouchée à la main (`trim_after`) ou déjà
  présente (`hasTrim`) n'est pas dupliquée.
- Les générateurs savent déjà tracer un trajet caché **à l'intérieur d'un
  objet** (HP-ENG-015, partiel). Rien n'existe **entre** objets.
- `parallel_for` et le banc de mesure existent ; la séquence n'a pas de notion
  de « ce qui est recouvert plus tard ».

## 3. Conception : pont caché en passe finale

### 3.1 Idée

Pour chaque écart entre deux tracés qui produirait aujourd'hui une coupe,
chercher un trajet **entièrement recouvert par des points cousus plus tard** ;
s'il existe, remplacer `verrou de sortie + coupe + saut + verrou d'entrée` par
un trajet cousu (ou un saut seul, voir modes) sur le même fil. Le fil est
continu, donc ni coupe ni verrous à cet endroit.

### 3.2 Conditions de validité (toutes obligatoires)

1. **Même fil** : aucun changement de couleur, Stop ou coupe retouchée dans
   l'écart. Un changement de couleur garde sa coupe.
2. **Recouvrement prouvé par la séquence elle-même**, pas par les objets du
   document : un satin manuel n'a pas de surface vectorielle (`filter_short_stitches`
   doit déjà le contourner). On rastérise les segments des passes
   `TopStitch` (jamais Underlay, Travel, Lock) en une grille d'entiers dont
   chaque cellule retient **l'indice de commande du dernier segment qui la
   couvre**. Un écart situé à l'indice `g` ne peut s'appuyer que sur des
   cellules dont cet indice est `> g` : « recouvert plus tard ».
3. **Largeur de fil et marge** : un segment couvre sa cellule sur une largeur de
   fil réglable (défaut 0,4 mm) ; le masque « recouvert plus tard » est ensuite
   **érodé** de la demi-largeur du fil plus une marge, de sorte que tout chemin
   tracé dans le masque érodé est entièrement sous des points. Un remplissage
   trop lâche laisse des trous : le pont est refusé, sans heuristique.
4. **Point de départ et d'arrivée dans le masque** : la fin du tracé précédent
   doit elle aussi être recouverte plus tard (cas typique : formes qui se
   touchent ou se chevauchent, lettres, rayons).
5. **Plafond de longueur** (`max_bridge_length`) et gain net : on ne remplace une
   coupe que si le trajet ajouté reste court.
6. Une coupe posée par l'utilisateur (`trim_after`) ou par un format n'est
   jamais supprimée.

### 3.3 Modes

- **Saut caché** : si l'écart est court (≤ `max_hidden_jump`, quelques
  millimètres) et recouvert plus tard, on garde un saut sans coupe ni verrous :
  aucun point ajouté. Cela revient à assouplir la règle `objectGap` quand on a la
  preuve de recouvrement.
- **Pont cousu** : sinon, chemin le plus court dans le masque érodé (A* sur la
  grille, voisins dans un ordre fixe, départage déterministe), simplifié en
  segments rectilignes dont chacun reste dans le masque, redécoupés à
  `bridge_stitch_length` (défaut 2,5 mm, jamais sous `min_stitch_length` ni
  au-dessus de `max_stitch_length`), passe `Travel`.
- **Option plus tard (P2)** : autoriser aussi le trajet sur une surface **déjà
  cousue de la même couleur** (comme le « travel on edges » de Hatch). Moins sûr
  (relief visible sur un satin), donc désactivé par défaut.

### 3.4 Contraintes du dépôt

- Fonction pure et déterministe sur la séquence, aucune mutation du document ;
  réglages dans `SequenceFinishing` (donc dans le `.osp`), **désactivés par
  défaut** comme `split_long_stitches` et `auto_join` : un projet existant
  produit le même DST.
- Aucun consommateur de production ne contourne `effective_sequence` : la passe
  vit dans `finish_sequence`.
- Chaque écart est indépendant une fois la grille construite (lecture seule) :
  `parallel_for` possible, assemblage séquentiel ensuite ; sortie indépendante de
  `OPENSTITCH_THREADS`.
- Mémoire : grille d'entiers 32 bits, taille de cellule adaptative pour
  plafonner le nombre de cellules à quelques millions.
- Pas de réordonnancement des objets : l'ordre est celui de `optimization`.
  C'est le choix explicite « à la toute fin ». Conséquence : le gain dépend de
  l'ordre amont (HP-ENG-010, HP-SEQ-001), et une passe d'ordre qui chercherait les
  ponts possibles est une autre entrée, non retenue ici.

### 3.5 Risques

- Épaisseur et relief : chaque pont ajoute du fil sous un objet ; plafonner la
  longueur et signaler les cellules où plusieurs ponts s'empilent.
- Transparence sur tissu clair ou fin : un recouvrement « suffisant » pour la
  géométrie peut laisser voir une ombre. Réglage utilisateur.
- Le recouvrement est géométrique ; l'effet réel (tirage, repoussement) ne se
  vérifie qu'en broderie réelle (HP-PHYS-001).
- Brevets : US8271123B2 (Brother) est affiché actif (2031-04-22) ; sa
  revendication 1 vise la génération de segments depuis une image, ce qui n'est
  pas notre cas, mais d'autres revendications ou brevets non lus (US9200397,
  US6860214B1) peuvent toucher la liaison recouverte. Notre mécanisme
  (preuve par grille « dernier recouvrement », érosion, A*) est de notre
  conception. Analyse non juridique : faire relire avant livraison.

## 4. Mesure d'acceptation proposée

- Scènes synthétiques de `openstitch-cli engine-debug` : deux formes qui se
  chevauchent (pont accepté), deux formes séparées sans recouvrement (coupe
  conservée), remplissage trop lâche (refusé), changement de couleur (coupe
  conservée), satin manuel sans surface (preuve par la séquence).
- Propriété : tout point d'un pont est dans le masque érodé ; aucune passe
  `Travel` créée hors recouvrement.
- Métriques avant/après : nombre de coupes (`compute_stats().trims`), nombre de
  points de verrou supprimés, points et longueur de fil ajoutés.
- Désactivé : séquence strictement identique à l'actuelle ; idempotence ;
  deux exécutions et `OPENSTITCH_THREADS=1` donnent le même DST.
- Les corpus « marine » et `tentabrode` sont absents de ce dépôt (voir
  HP-ENG-010) : à fournir pour une mesure réelle.

## 5. Réalisation et mesure (2026-10-13)

Implémenté dans le cœur sans Qt : `HiddenTravel` dans `SequenceFinishing`,
planificateur `hidden_travel.{hpp,cpp}`, branchement dans `finish_sequence`,
`effective_sequence_report`, persistance `.osp`, CLI `digitize --hidden-travel`.
Écarts par rapport à la conception : le plafond de longueur s'applique aussi à la
distance directe ; la grille est reconstruite à la première demande seulement ;
l'érosion est une fenêtre carrée (plus stricte qu'un disque) ; la surface de même
fil (option .b) est incluse.

Mesure sur `tests/fixtures/tentabrode.png`, mode formes, 442 coupes au départ :

| Réglage | Coupes supprimées | Fil ajouté | Refus (trop long / extrémité / chemin) |
|---|---|---|---|
| `--hidden-travel` | 6 | 47 mm | 269 / 132 / 27 |
| + `--hidden-travel-same-colour` | 6 | 47 mm | 271 / 32 / 125 |
| + `--hidden-travel-max 40` | 20 | 371 mm | 100 / 76 / 238 |

Lecture : l'idée est correcte (les tests couvrent chaque condition de la §3.2)
mais le gain est faible sur une image auto-numérisée, parce que la segmentation
produit des régions juxtaposées **sans recouvrement** : entre deux régions
éloignées, il n'y a pas de matière cousue plus tard pour cacher le fil, et la
passe refuse à raison. Les cas gagnants sont les designs dont les objets se
chevauchent (lettrage, formes superposées, dessin vectoriel) et les enchaînements
bien ordonnés en amont. Mesure sur machine et corpus « marine » : à faire.

Limites connues : pas de parallélisation (`parallel_for`) des recherches de
chemin ; pas de réglage dans l'inspecteur du desktop ; `cli_help` échoue dans cet
environnement de test parce que la version de CLI11 installée par le système
écrit « Positionals » et non « POSITIONALS » (sans rapport avec ce travail).
