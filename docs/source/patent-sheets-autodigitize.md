# Fiches brevets : auto-numérisation (famille Goldman et Brother)

> Fiches techniques de travail, issues de la lecture de pages Google Patents (résumés automatiques, pas les PDF). Statuts juridiques préliminaires. Voir l'index et les priorités dans [Recherche brevets](patent-research.md).


Methode et limites (a lire d'abord)
- Source unique : pages patents.google.com/patent/<ID>/en lues via un outil qui renvoie un resume produit par un petit modele (pas le texte brut). Chaque page fait ~190-200 k caracteres ; les ~100 premiers k (description) et la zone suivante (revendications, evenements juridiques) ont ete lus en deux passes. Aucun PDF ni autre miroir n'a ete consulte.
- Consequence : les formules sont de seconde main. Elles sont marquees "A VERIFIER SUR LE PDF". Aucune equation n'est inventee ; tout ce qui n'est pas dans le brevet est etiquete NOTRE PROPOSITION.
- Les revendications ci-dessous sont des paraphrases courtes. Les dates de statut juridique sont celles affichees par Google (qui precise que ce n'est pas une conclusion juridique) et presentent des incoherences internes signalees ci-dessous.
- Aucun texte copie ; aucun code.

## 0. Vue d'ensemble, doublons et correction de categorie

Famille Goldman / SoftSight (puis Vistaprint, Cimpress). Priorite commune : 1998-08-17 (US 09/134,981). Chaine de continuations indiquee : 09/134,981 (US6836695) -> 09/950,521 (US6804573, hors liste) -> 10/806,880 (US7587256, hors liste) -> 12/507,588 (US8219238) -> 13/490,194 (US8532810) -> 14/011,452 (US9200397). Les demandes 10/806,607 (US6947808), 10/806,629 (US7016756, hors liste) et 10/806,863 (US7016757) sont des continuations deposees le 2004-03-23 de 09/950,521. Les six brevets Goldman de la liste PARTAGENT LA MEME DESCRIPTION (memes figures 1-16, meme texte) ; seules les revendications different. Les lire comme un seul corpus technique, six jeux de revendications.

Categorie "numerisation automatique" : correcte pour les six Goldman (pipeline image scannee -> objets -> points). A nuancer pour les autres :
- US8532810 = ordonnancement de fragments de remplissage (optimisation d'ordre / coupes de fil), pas de la numerisation en soi.
- US9200397 = simplification de polylignes (filtre triangulaire), brique geometrique.
- US6947808 = classification de points de contour pres des noeuds du squelette (ancres satin), brique satin.
- US5740056 (Brother) = choix du type de point selon l'epaisseur, depuis un scan ; numerisation automatique tres rudimentaire (traits fins).
- US5576968 (Brother) = choix de la direction/angle de remplissage et du point de depart/fin pour UNE region fermee ; plutot "generation de remplissage / planification", pas de la segmentation d'image.

Statuts : tous expires a la date du jour (2026-10) selon toute vraisemblance (duree 20 ans depuis le depot parent de 1998-08-17, plus ajustements de duree), mais voir les anomalies : US8219238 montre des paiements de taxes en 2020 et 2023 alors que Google affiche une expiration en 2019 ; US8532810 montre une date de grant incoherente entre deux lectures (2013-08-21 vs 2013-09-10). A confirmer par un conseil en brevets ; non critique ici car la duree maximale couvre tous ces cas, mais aucun avis juridique n'est donne.

---

## 1. US6836695B1 (famille racine)

(1) Identification
- Titre : Automatically generating embroidery designs from a scanned image. Inventeur : David A. Goldman. Cessionnaire initial : Soft Sight Inc ; puis Vistaprint (2010) ; Vistaprint Schweiz (2013) ; Cimpress Schweiz GmbH (2015).
- Depot et priorite : 1998-08-17. Delivre : 2004-12-28 (la page indique aussi "granted 2004-12-09" dans un evenement, ecart de date d'evenement vs publication, normal).
- Statut Google : Expired - Lifetime ; expiration ajustee 2021-02-26 (ajustement de duree). Taxes payees : 4e an 2008, 8e an 2012, surtaxe 2014, 12e an 2016.
- Juridictions : seuls des documents US ressortent ; je n'ai pas vu d'equivalents EP/JP (non verifie, la liste de famille de la page ne montre que des US).
- Cite par : 21 a 66 documents (Brother, Vistaprint, Levi Strauss, Zazzle). Cite : Brother, Pulse Microsystems, Aisin ; litterature non-brevet : Borgefors 1986 (transformee de distance), Kwok 1988 (amincissement).

(2) Revendications utiles
- Il n'y en a que deux, independantes : revendication 1 (systeme) et 2 (procede). Elles sont tres larges : entree d'une image couleur en pixels, segmentation en objets, classification mince/epais, localisation et interpretation des regions regulieres et singulieres, calcul d'un ordre de couture optimal pour au moins une colonne, generation d'un fichier. Note : la rev. 2 dit "le systeme" dans son preambule (incoherence redactionnelle).
- C'est le brevet "parapluie" ; les details sont dans les continuations.

(3) Algorithme (description commune, numeros de figures 1-16, etapes 402-432)
Entree : bitmap 24 bits, 300 dpi, un bitmap par couleur. FROM PATENT sauf mention contraire.
a. Segmentation (etape 404) : lissage selectif (moyenne ponderee uniquement dans les voisinages a faible contraste ; zones a fort contraste intactes) ; croissance de regions depuis des graines a faible contraste ; absorption des pixels non classes vers un objet voisin selon proximite et similarite de couleur ; suppression des objets de moins de 6 pixels (leurs pixels reaffectes au plus proche objet plus grand).
b. Codage en chaine et DT (etape 406) : un seul balayage raster produit les contours fermes (exterieur + trous) en chaine a 8 directions (fig. 13). Transformee de distance chamfer entiere "3-4" calculee pendant la generation des contours ; distance normalisee = valeur DT / 3. Amincissement -> squelette (le squelette est calcule en meme temps).
c. Classification (etape 408) : statistiques de la DT sur les pixels du squelette : max, moyenne mu, ecart-type sigma. Exemple de regle donne dans le texte : "objet predominamment regulier si 2*sigma < mu < max/2" (FROM PATENT, exemple ; formule rendue par un resume, A VERIFIER SUR LE PDF). Un objet est "mince" si max et mu sont inferieurs a des seuils predefinis (seuils non chiffres, "choisis experimentalement"). Sinon epais. La classification par branche est mentionnee mais "non utilisee actuellement".
d. Voie epais (remplissage) : approximation polygonale par filtrage triangulaire (voir US9200397) ; choix de l'angle parmi 16 angles de balayage = celui qui donne le moins de fragments (voir US8219238) ; fragmentation par balayage de lignes modifie ; ordonnancement recursif des fragments (voir US8532810) ; entree/sortie sur le contour exterieur ; ligne mediane par fragment pour les deplacements en point droit.
e. Voie mince (satin) : voir US6947808 pour ancres, fusion, normales, lissage, chemin.
f. Sortie : angles, fragments, points de depart/sortie, colonnes.

Formules de recherche (FROM PATENT mais seconde main, A VERIFIER) :
- Distance de recherche d'ancre de jonction : (valeur DT du noeud) x 10^(2*pi - theta), theta = angle entre les deux vecteurs de branches. Une premiere lecture avait donne "(DT)x10/(2pi-theta)" ; les deux lectures divergent, donc ne pas implementer avant d'avoir lu le PDF.
- Distance de recherche de fusion de bifurcation : (valeur DT) x 10^(c*pi + theta), "c" illisible. Constante 10 "choisie empiriquement".
- Critere moindres carres des normales : pas de formule explicite ; angle du nouveau segment vs normales du contour qu'il relie + terme proportionnel a la largeur moyenne de colonne.

(4) Cas limites et cout : bruit < 6 px, objets a trous, objets formes de ruban quasi regulier vs lettres (serifs codes en dur, ex. "A"), points sharp < ~30 degres, coupure de colonne < ~45 degres. Complexite : DT et squelette lineaires en pixels ; fragmentation lineaire x 16 angles ; planification de chemin recursive (exponentielle dans le pire cas, non borne par le texte).

(5) Pertinence OpenStitch : MOYENNE a faible comme exigence, mais utile comme feuille de route. OpenStitch a deja segmentation CIELAB, regions, squelette, rails/rungs, tatami. Ce que la description apporte de neuf : la classification mince/epais par statistiques de DT sur le squelette (regle simple, implementable independamment), qui pourrait nourrir la "satinabilite" ; mais la politique du projet interdit le satin automatique en autodigitize, donc seulement comme avertissement/suggestion.

(6) Caveats : expire, mais verifier avec conseil. Ne rien copier du texte. Les formules sont probablement des constantes empiriques ; en reimplementer des equivalents calibres par nos propres donnees (NOTRE PROPOSITION).

---

## 2. US6947808B2 (revendications : classification des points de contour pres des noeuds du squelette)

(1) Identification : meme famille ; demande 10/806,607 deposee 2004-03-23 ; publication US20040243272A1 (2004-12-02) ; delivre 2005-09-20 (evenement grant 2005-08-31) ; Google : Expired - Lifetime, expiration ajustee 2018-11-15. Taxes : 4e an payee 2009 avec surtaxe ; la lecture est tronquee (derniers evenements non vus). Continuation de 09/950,521. Cessions identiques aux autres.

(2) Revendications : indep. 1 (procede), 14 (systeme), 27 (support). Procede : obtenir les donnees de contour d'un objet, classer automatiquement les points de contour proches d'un noeud du squelette (noeud d'extremite ou de jonction), generer les donnees de broderie a partir de ces points classes. Dependantes : relation avec squelette (2), ancres d'extremite/de jonction (3-4), singularite/discontinuite/concavite (5), suppression de singularite (6), type/angle/chemin de point (7), planification de chemin avec noeud de depart choisi par l'utilisateur et parcours recursif (8-10), normales de trait entre points de contour opposes, supprimer les discontinuites (11-13), image bitmap (40-42).

(3) Algorithme satin (etapes 422-432) :
- Stockage : contours externes/internes en polylignes ; branches du squelette en segments minimaux ; noeuds avec degre et connexions.
- Ancre d'extremite : prolonger le bout du squelette selon sa direction d'une longueur proportionnelle a l'epaisseur locale (DT moyenne sur la fin de branche) ; trouver le sommet de contour le plus proche ; si son angle de courbure < ~30 degres -> pointe unique. Sinon chercher, de chaque cote, jusqu'a 4 sommets voisins et dans une distance max, une paire gauche/droite dont le vecteur de liaison correspond le mieux a l'angle de depart (direction) et a l'epaisseur locale (norme).
- Ancre de jonction : angles delimitant les branches consecutives en chaque noeud de degre >= 3 ; chercher des concavites du contour dans une distance fonction de la DT du noeud et de l'angle theta (formule ci-dessus) ; apparier chaque concavite du squelette a la concavite de contour dont la direction est la plus proche de son centre angulaire.
- Fusion d'artefacts : deux noeuds qui se projettent sur la meme paire de sommets caracteristiques -> fusionnes a leur milieu. Regles codees en dur pour empattements (serifs) : noeud de degre 3 avec deux branches courtes finissant en noeuds de degre 1 dont les ancres d'extremite sont reliees par un seul segment de contour, jonction non concave aigue.
- Codage : singularites (jonctions) resolues en reconstruisant les frontieres occultees par minimisation d'energie (pas de formule dans le texte lu). Normales de trait : segment de depart au noeud, puis chaque normale choisie par moindres carres (angle vs normales de contour + terme de longueur lie a la largeur moyenne), interpolation entre sommets.
- Lissage de colonnes : ajuster une normale incoherente avec ses voisines ; ajouter une normale si un point de contour est orphelin ; lisser premiere/derniere normale ; couper la colonne aux virages < ~45 degres (cousues separement).
- Chemin : parcours recursif depuis un noeud d'entree ; une branche n'est terminee qu'apres les autres branches du noeud ; boucles cousues en continu ; score d'un chemin = longueur cumulee des autres colonnes cousues pendant les interruptions d'une longue colonne ; garder le moindre score ; au moins un chemin sans coupe de fil est garanti (selon une lecture).
- NOTRE PROPOSITION : le squelette et la detection des extremites/jonctions existent deja dans OpenStitch (junction anchoring). Rien d'a coder tel quel.

(4) Cas limites/cout : jonctions en Y, T, X, serifs ; boucles ; lignes qui se croisent a angle aigu (fig. 10). Cout dominant : appariements locaux, faible.

(5) Pertinence : MOYENNE. OpenStitch possede deja ancrage de jonction et rails/rungs ; le seul apport possible est de comparer nos heuristiques (4 sommets, 30/45 degres, serifs) aux notres pour trouver des cas oublies, et la regle de chemin "score = longueur des colonnes cousues pendant l'interruption" pour le routage satin. Satin auto interdit par politique -> usage en "assistance" uniquement.

(6) Caveats : expire ; description identique a celle de la racine, donc risque nul de "doublon" a analyser deux fois.

---

## 3. US7016757B2 (revendications : classification par regularite + type de point)

(1) Identification : demande 10/806,863, deposee 2004-03-23, publiee US20040243274A1 ; delivre 2006-03-21 (evenement 2006-03-01). Expire - Lifetime, expiration anticipee 2018-08-17 selon Google. Taxes : 4e an 2009, 8e an 2013 (surtaxe 2014), 12e an 2017 (coherent avec un brevet maintenu jusqu'a son terme). Continuation de 09/950,521.

(2) Revendications : indep. 1 (procede), 10 (systeme), 19 (support). Procede : recevoir des donnees de transformee de distance, classer un objet de l'image en extrayant une "caracteristique de regularite" liee a la variation d'epaisseur, generer les donnees de broderie selon cette classification. Dependantes : statistiques de DT (2), regularite derivee des statistiques (3), ou de la DT + donnees de squelette (4), objet irregulier ou predominamment regulier (5), type de point choisi selon la regularite (6), remplissage ou colonne (7), objet parmi plusieurs ou partie d'un (8-9). Les rev. 11-18 et 20-27 les dupliquent.

(3) Algorithme : voir racine, etape c. FROM PATENT : statistiques max, mu, sigma de la DT sur les pixels du squelette ; regle exemple 2*sigma < mu < max/2 pour "regulier" (A VERIFIER). Mince si max et mu sous seuils (valeurs non donnees). Epais sinon -> remplissage ; mince et regulier -> satin (colonnes).

(4) Cas limites : objets mixtes (une branche epaisse + une branche mince) ; la classification par branche est mentionnee mais non utilisee dans l'implementation decrite. Cout : un parcours des pixels du squelette.

(5) Pertinence : MOYENNE. Idee implementable independamment : utiliser mu/sigma/max de la distance au bord le long de l'axe median pour classer "ruban regulier / zone large". OpenStitch a deja squelette + satinabilite ; apport = un critere supplementaire peu couteux, NOTRE PROPOSITION : seuils a calibrer sur nos propres jeux de test, ne pas reprendre les valeurs du brevet.

(6) Caveats : expire ; description non protegee par le simple fait que des idees generiques (statistiques d'epaisseur) existent aussi dans le domaine public (ex. Borgefors/Kwok cites dans le dossier).

---

## 4. US8219238B2 (revendications : angle de remplissage = moins de fragments)

(1) Identification : demande 12/507,588 deposee 2009-07-22 ; publiee US20100191364A1 (2010-07-29) ; delivre 2012-07-10 (evenement "patent grant" 2012-06-20). Continuation de 10/806,880 (US7587256). Cessionnaire : Vistaprint Technologies -> Vistaprint Schweiz -> Cimpress. Google : "Expired - Lifetime", expiration 2019-10-13 (ajustee). ANOMALIE : taxes 4e an (2016), 8e an (2020-01-10) et 12e an (2023-12-14) enregistrees ; paiement de taxes ne prolonge pas la duree, mais l'ecart avec "expire 2019" est a faire verifier. Une duree de 20 ans a partir du 1998-08-17 donne 2018 + ajustements ; les taxes pourraient etre un artefact. Sans certitude : statut a confirmer aupres d'un conseil.

(2) Revendications : indep. 1 (procede), 9 (systeme), 18 (support). Procede : identifier un objet contigu dans les donnees d'image ; tester plusieurs angles de couture ; compter les fragments par angle ; choisir l'angle donnant le moins de fragments ; generer les donnees. Dependantes : objet epais (2) ; fragment = sous-region cousable en continu (3), separee par coupe de fil ou points droits (12/21 d'apres la lecture) ; angles = angles de balayage de l'image (4) ; point d'entree/sortie des fragments sur le contour (5-6) ; ligne mediane du fragment (7) ; point droit ou coupe entre sortie d'un fragment et entree du suivant (8).

(3) Algorithme (FROM PATENT) : 16 angles de balayage candidats ; pour chaque angle, fragmentation par balayage de lignes modifie ; nombre de fragments compte ; angle du minimum retenu (fig. 6a/6b : exemple 2 fragments vs 1). Ligne mediane = milieu entre vertex gauche et droit. Details de la fragmentation (traitement des concavites/trous, regle de creation d'un nouveau fragment) : voir le texte complet, non lu en detail ici.
NOTRE PROPOSITION : le critere "minimiser le nombre de composantes connexes des lignes de balayage" est generalisable ; pour OpenStitch, tester N angles (pas forcement 16) et minimiser fragments, puis desempartager par longueur de saut.

(4) Cas limites : egalite de fragments entre angles ; formes avec trous ; angle contraint par l'utilisateur. Cout : 16 x (fragmentation lineaire en nombre de lignes de balayage).

(5) Pertinence : ELEVEE pour tatami. OpenStitch a un fill directionnel et l'optimisation de l'ordre des points ; l'angle automatique base sur le comptage de fragments est genuinement nouveau et simple (autodigitize tatami/contour).

(6) Caveats : expire selon Google (voir anomalie) ; l'idee generale "choisir l'angle minimisant les coupes" est peu inventive isolee, mais ne rien copier du texte.

---

## 5. US8532810B2 (revendications : fragments recursifs + ordre de couture)

(1) Identification : demande 13/490,194 deposee 2012-06-06 ; publiee US20120245726A1 (2012-09-27) ; delivre en 2013 (deux dates divergentes dans mes lectures : 2013-08-21 evenement "patented case" et 2013-09-10 publication ; la seconde est probablement la date officielle de publication). Google : Expired - Fee Related ; expiration anticipee 2018-08-17 en premiere lecture, mais evenements : taxes 4e an 2017, 8e an 2021-02-11, rappel 2025-04-28, EXPIRE pour non-paiement 2025-10-13 (lapsed ; aucune reinstauration vue). Interpretation : le brevet semble avoir ete maintenu bien au-dela de 2018, ce qui est incoherent avec une duree de 20 ans depuis 1998 ; soit une date d'expiration terminale ajustee, soit une erreur de donnees Google. DANS TOUS LES CAS le statut a CONFIRMER aupres d'un conseil ; pas de conclusion ici.

(2) Revendications : indep. 1 (procede), 5 (appareil), 9 (support). Procede : segmenter en objets ; diviser recursivement un premier objet en fragments en enregistrant COMMENT chaque fragment a ete cree (un petit fragment est relie a son fragment parent plus grand au niveau de son bord / ligne de balayage de depart) ; definir l'ordre de couture a partir de cet historique de creation pour reduire les coupes de fil ou augmenter la connectivite de chemin ; sortir le fichier. Dependantes : ligne mediane par fragment (2), trajet de deplacement entre fragments via cette mediane (3), plusieurs petits fragments issus d'un grand (4) ; rev. 13/14 : minimiser coupes / maximiser connectivite.

(3) Algorithme : FROM PATENT, structure arborescente des fragments (arbre de scission) avec lien enfant -> parent au premier bord/ligne ; l'ordre de couture suit l'arbre pour chainer les fragments voisins avec points droits au lieu de coupes. Details de l'ordre exact (DFS ? priorites ?) : non verifies dans le texte lu ; A VERIFIER SUR LE PDF.
NOTRE PROPOSITION : representer chaque region tatami comme un arbre de fragments, cout de saut = distance entre sortie et entree, mediane pour le trajet interne.

(4) Cas limites : fragments sans parent commun, trous, region disjointe. Cout : lineaire en nombre de fragments pour l'arbre.

(5) Pertinence : MOYENNE a ELEVEE. OpenStitch optimise deja l'ordre de points ; l'arbre de scission avec trajet par ligne mediane peut reduire les sauts et les coupes dans le tatami complexe.

(6) Caveats : voir statut ; revendication 9 sans lien explicite avec l'enregistrement de la creation (structure plus large que 1 et 5).

---

## 6. US9200397B2 (revendications : simplification de contour par triangle)

(1) Identification : demande 14/011,452 deposee 2013-08-27 ; publiee US2014/0094952A1 ; delivre 2015-12-01 (lecture alternative d'evenement : 2015-11-11). Cessionnaire initial indique : Cimpress Schweiz GmbH. Statut Google : Expired - Fee Related ; expiration anticipee 2018-08-17 en premiere lecture, mais evenements : rappel 2019-07-22, EXPIRE pour non-paiement des taxes 2020-01-06, "lapsed" effet 2019-12-01. Il n'y a pas de reinstauration vue : lapsed 2019-12-01 (donc brevet tombe avant le terme probable). Interpretation prudente : aucun doute pratique, expire/lapsed ; a confirmer.

(2) Revendications : indep. 1 (procede), 7 (appareil), 13 (support). Procede : identifier les sommets dans un ensemble de points de chaine (contour d'un objet) ; pour chaque sommet, former le triangle avec ses voisins et comparer sa hauteur a un seuil ; supprimer le sommet si la hauteur est plus petite ; generer les donnees de broderie avec les sommets restants. Dependantes : codes de chaine differentiels (2) ; sommets = points dont le code differe du precedent (3) ; sommet fixe sur le bord exterieur ou un objet voisin change (4) ; egalite de hauteur -> supprimer le sommet le plus proche d'un point de reference (5) ; angle de couture et fragments bases sur les sommets filtres (6).

(3) Algorithme (FROM PATENT, fig. 5) : filtrage triangulaire iteratif sur sommets de chaine ; seuil de hauteur NON chiffre. Sommets fixes aux jonctions entre objets adjacents (frontieres partagees, fig. 15b) pour eviter les trous/chevauchements entre regions de couleurs voisines. NOTRE PROPOSITION : c'est essentiellement un critere de type Visvalingam-Whyatt / Ramer-Douglas-Peucker local ; OpenStitch a deja une simplification de chemins dans `geometry`. Elements genuinement utiles : (a) sommets fixes aux frontieres partagees et (b) departage deterministe des egalites.

(4) Cas limites : egalites, boucles fermees courtes, sommets fixes consecutifs. Cout : O(n) par passe, O(n log n) avec file de priorite.

(5) Pertinence : FAIBLE a MOYENNE. Tout a fait generique ; seule la garantie de frontieres partagees apporte quelque chose si OpenStitch ne l'a pas deja dans la vectorisation (a verifier dans `vectorization`).

(6) Caveats : brevet lapsed ; la technique est generique et anterieure (algorithmes classiques de simplification), mais ne rien copier.

---

## 7. US5740056A (Brother : type de point selon epaisseur du trait)

(1) Identification : titre "Method and device for producing embroidery data for a household sewing machine". Inventeur : Masao Futamura. Cessionnaire : Brother Industries (Brother Kogyo KK). Demande 08/545,436 deposee 1995-10-19 ; priorites 1994-10-11 (parente US 08/321,222, devenue US5515289) et 1994-10-19 (JP 253599/94). Delivre 1998-04-14. Statut : Expired - Lifetime ; expiration anticipee 2014-10-11. Famille : continuation-in-part de US 08/321,222 ; je n'ai pas verifie d'autres membres (EP/JP possibles, non confirmes).

(2) Revendications (19) : indep. 1 (dispositif), 5 (dispositif), 10 (dispositif), 15 (procede). Idee : image d'entree -> lignes fines -> une mesure de "forme/epaisseur" par composante -> donnees de broderie selon cette mesure + lignes fines. Rev. 2 : mesure basee sur le nombre de passes d'amincissement ; rev. 3/10 : conversion en distance ; rev. 4 : largeur du zigzag selon la mesure.

(3) Algorithme (FROM PATENT, valeurs chiffrees) :
- Mode 1 : binarisation (scanner a main, noir=1). Extraction des composantes connexes (4 ou 8-connexite). Amincissement sequentiel jusqu'a 1 pixel d'epaisseur, en comptant le nombre de passes N par composante. Vectorisation de la ligne fine : echantillonnage a intervalle, test du vecteur vs vecteur de reference -> segments courts.
- Regle de type de point par N : 1 <= N < 3 : triple point (point de renfort) ; 3 <= N < 5 : zigzag de 1,2 mm ; N >= 5 : zigzag de 1,8 mm.
- Zigzag : aiguille alternee de part et d'autre du segment a +/- demi-largeur ; triple : positions successives le long du segment.
- Mode 2 : conversion en distance (bord = 1, croissant vers l'interieur), valeur max stockee par composante ; type de point determine par ce max a la place de N ; variantes citees : proportion de pixels au-dessus d'un seuil, moyenne des grandes valeurs.
- Aucune equation formelle dans le texte ; seuils ci-dessus uniquement.

(4) Cas limites : composantes d'epaisseur variable (un seul N par composante : perte d'information) ; connexite. Cout : lineaire.

(5) Pertinence : FAIBLE (technologie 1994, trop simple). Deja largement depasse par la DT + squelette d'OpenStitch. L'idee "epaisseur mesuree -> largeur de satin / type de point" est la plus proche de notre domaine ; NOTRE PROPOSITION : utiliser la DT du squelette pour proposer largeur de rail, sans satin automatique.

(6) Caveats : expire (2014), faible risque.

---

## 8. US5576968A (Brother : direction de couture et point de depart/fin d'une region)

(1) Identification : titre "Embroidery data creating system for embroidery machine". Inventeurs : Masahiro Mizuno, Masao Futamura, Yukiyoshi Muto. Cessionnaire : Brother Industries. Demande 08/394,633 deposee 1995-02-27 ; priorite JP6-117919, 1994-05-31 ; delivre 1996-11-19. Statut : Expired - Lifetime ; expiration anticipee 2015-02-27. Aucune famille listee ; cite par 25 documents (Brother, Janome, Softsight/Goldman).

(2) Revendications : indep. 1 (systeme, critere de distance), 11 (systeme, critere d'aire), 17 (procede). Idee : machine qui coud seulement dans deux sens opposes ; region fermee ; point d'arrivee (= depart) ; frontiere perpendiculaire a la direction de couture passant par ce point ; on choisit le cote dont le point le plus eloigne est le plus loin (rev. 1) ou dont l'aire est la plus grande (rev. 11), puis la direction de couture adaptee ; la region est coupee par une ligne reelle passant par le point avec le meme angle et des sens de couture opposes de chaque cote ; le depart coincide avec l'arrivee (une seule coupe de fil double). Rev. 20 : chaque sous-region cousue depuis le point de contour le plus eloigne du point d'arrivee.

(3) Algorithme (FROM PATENT, mode 1) : contour ferme P0..Pn ; point d'arrivee = point le plus bas en y ; ligne EL verticale ; LX = |x(arrivee) - x(point le plus a gauche)|, RX = |x(point le plus a droite) - x(arrivee)| ; si LX > RX direction A (gauche->droite, angle 45 degres sens horaire par rapport a la frontiere) sinon B (sens anti-horaire). Ligne NL a 45 degres passant par l'arrivee ; de chaque cote, trouver le point le plus eloigne, point droit de l'arrivee vers lui, puis remplissage finissant a l'arrivee. Mode 2 : meme chose avec les aires SL/SR (formule non reproduite dans le texte lu : "image" absente) ; si SL > SR direction A sinon B. Angle de couture de reference : 45 degres par rapport a la frontiere (esthetique).

(4) Cas limites : NL passant par une arete (une sous-region vide) ; polygones concaves (les partielles peuvent etre disjointes ; non traite en detail). Cout : O(n) pour les extremums.

(5) Pertinence : FAIBLE. Contrainte machine specifique (couture dans deux sens). Idee generalisable : choisir depart = arrivee pour reduire les coupes de fil, et couper en deux sous-regions de sens opposes ; marginal pour tatami moderne.

(6) Caveats : expire (2015).

---

## Synthese : ce qui est reellement nouveau et implementable pour OpenStitch

1. (ELEVEE) Choix de l'angle tatami en testant N angles et en minimisant le nombre de fragments (US8219238). Simple, deterministe, pur fonction d'un snapshot immuable -> compatible avec la regle "generation = fonction pure".
2. (MOYENNE-ELEVEE) Arbre de scission des fragments et ordre de couture a partir de l'historique de creation, trajet interne via ligne mediane (US8532810).
3. (MOYENNE) Classification epais/mince/regulier par statistiques de la DT sur le squelette (mu, sigma, max) comme avertissement ou suggestion (US6836695, US7016757). Pas pour auto-satin.
4. (FAIBLE-MOYENNE) Sommets fixes sur frontieres partagees lors de la simplification (US9200397), a verifier vs `vectorization`.
5. (FAIBLE) Heuristiques d'ancres satin (US6947808) : seulement comparaison avec notre code existant ; US5740056 et US5576968 : interet historique.

Points a verifier au PDF avant toute implementation : regle exacte de classification, formules 10^(...) des distances de recherche, details de la fragmentation et de l'ordre de couture, seuil de hauteur du filtre triangulaire, statuts juridiques (surtout US8219238 et US8532810).

---

# Suite : brevets Brother, Pulse et Shima Seiki (lot B2)


Source unique : pages Google Patents (/en) lues via WebFetch (résumeur automatique, donc pas le texte brut). Limites générales :
- Le texte renvoyé est un résumé produit par un petit modèle ; les formules rendues en image (##EQU##) sont absentes.
- Pour 6356648, 5343401, 4849902, 6937919, 7386361 la page dépasse 100 000 caractères ; j'ai lu la suite par offset, mais certaines parties restent non vérifiées (signalé ci-dessous).
- Tous les statuts affichés sont « Expired - Lifetime » (ou Fee Related) selon Google, qui précise que c'est une hypothèse et non une conclusion juridique.
- Convention : DU BREVET = fidèle à la source ; NOTRE PROPOSITION = idée d'implémentation indépendante, jamais attribuée au brevet.

Vue d'ensemble et correction de catégorie

| Brevet | Catégorie annoncée | Catégorie réelle |
|---|---|---|
| US5839380A | auto-digitizing | Semi-auto : bitmap -> découpe interactive en zones -> type de point par zone (fill vs ligne médiane). Pertinent. |
| US6356648B1 | auto-digitizing | Prétraitement bitmap : amincissement, suivi de contour, élagage des branches, détection de boucles. Pertinent. |
| US5283747A | auto-digitizing | PAS de la numérisation : ordonnancement/routage de points de bâti (running stitch) dans un graphe de sections polygonales pour éviter les fils croisés sur machine multi-aiguilles. Lié à l'optimisation d'ordre. |
| US5343401A | auto-digitizing | PAS de la numérisation : système de poinçonnage interactif (PC) et transformation de lettrage « bridge » (déformation de texte sur arc). Transformation de contours. |
| US4849902A | auto-digitizing | Numérisation manuelle assistée (1986) : image caméra, saisie de points de contour au crayon optique, découpe en blocs polygonaux, satin par blocs. Historique. |
| US7386361B2 | auto-digitizing | PAS de la numérisation auto : dessin à main levée à stylet, points d'aiguille générés en temps réel selon pression/courbure, plus simulation d'éclairage. |
| US6937919B1 | auto-digitizing | PAS de la numérisation : génération de motifs de remplissage concentriques/radiaux/elliptiques dans une région. Nouveau type de remplissage. |

Doublons de famille dans la liste : aucun doublon strict entre les 7. Liens de parenté : 5839380, 6356648 et 6937919 sont tous Brother ; 6356648 et 6937919 partagent l'inventeur Taguchi et 5839380 / 6937919 partagent Muto. 6937919 cite US6587745B1 (Wilcom, remplissage ligne courbe). US4849902A : voir réserve de fiabilité plus bas.

---

## 1. US5839380A — Method and apparatus for processing embroidery data

(1) Titulaire Brother Industries ; inventeur Yukiyoshi Muto. Priorité 27/12/1996 (JP 8-350275), dépôt 17/12/1997 (US 08/991,873), délivrance 24/11/1998. Statut : Expired - Lifetime (hypothèse Google), expiration prévue 17/12/2017. Famille : JPH10179964A (publié 07/07/1998). Classes D05B19/00-08. Cité par ~96 documents (dont Softfoundry, Goldman « Automatically generating embroidery designs from a scanned image »). Cite JPH07136361A (Brother, choix auto du type de point par transformée en distance + statistiques) et US5559711A.

(2) Claims utiles : indépendantes 1 (méthode) et 6 (appareil) ; 2-3 (fill = contour + points intérieurs ; ligne = amincissement + points le long), 4-5 (fill = satin ou tatami ; ligne = zigzag ou running), 7 (pixels connexes de même densité), 8-9 (division interactive par l'opérateur avec affichage des lignes de coupe), 10-13 (équivalents appareil). Figures 3, 4A/4B (extraction), 6, 7A/7B (drapeaux de frontière), 8A/8B, 9A/9B. Je n'ai pas le texte exact des revendications, seulement leur paraphrase.

(3) Algorithme (DU BREVET, sans aucune équation) :
- Bitmap binaire. Deux plans de drapeaux par pixel : « examiné » et « frontière ».
- L'opérateur trace des lignes de coupe (typiquement en travers des parties allongées) ; rastérisation par tracé de droite standard, drapeau frontière = 1 sur ces pixels.
- Extraction de composantes connexes par balayage gauche-droite/haut-bas : pixel noir, non examiné, non frontière = graine. Propagation récursive en 4-voisinage (variante 8-voisinage). Un pixel frontière est inclus dans la zone mais la propagation depuis lui ne continue que vers d'autres pixels frontière : la zone ne déborde pas de l'autre côté. Variante : mettre à 0 la densité des pixels de la ligne.
- L'opérateur affecte un type de point à chaque zone. Si type « remplissage » (satin/tatami) : suivi de contour -> chaîne fermée 1 pixel -> vectorisation par échantillonnage de points significatifs -> points de couture intérieurs (blocs de 4 points renvoyés à une méthode antérieure). Si type « ligne » (zigzag/running) : amincissement standard par suppression itérative de pixels de bord -> vectorisation -> points le long de la ligne médiane.
- Variante citée : choix automatique du type via transformée en distance et statistiques (renvoi à JPH07136361A, non lu).
- Pas de paramètres numériques autres que voisinage 4/8 et largeur 1 pixel.

(4) Cas limites/coût : récursion pixel par pixel (risque de dépassement de pile, à faire itératif). Division manuelle donc non déterministe vis-à-vis de l'utilisateur. Rien sur la qualité de la jonction entre zones adjacentes (chevauchements, trous).

(5) Pertinence OpenStitch : MOYENNE-FAIBLE. Les composantes connexes et le contour existent déjà ; l'idée « une zone allongée devient une ligne médiane, une zone épaisse devient un fill » est déjà couverte par l'extraction de squelette et la satinabilité. Ce qui est éventuellement nouveau : le mécanisme de coupe par ligne-frontière (couper une région par une polyligne utilisateur avant vectorisation) comme outil d'édition ; et la classification fill vs ligne par géométrie (la source d'origine est JPH07136361A, à lire séparément). NOTRE PROPOSITION : outil « diviser la région » opérant sur la géométrie vectorielle (Clipper2) plutôt que sur pixels.

(6) Juridique (préliminaire) : brevet expiré (fin 2017) selon Google. Pas de risque d'exploitation ; reste à ne pas copier le texte. Citations ultérieures ne créent pas de droits.

---

## 2. US6356648B1 — Embroidery data processor

(1) Brother ; inventeur Shoichi Taguchi. Priorité 20/02/1997 (JP 9-036673 / JPH10230088A), dépôt 18/02/1998 (US 09/025,569), délivrance 12/03/2002. Expired - Lifetime, expiration prévue 18/02/2018 ; frais payés aux années 4, 8, 12. Famille ID 12476385 : JPH10230088A. Cité par ~72 documents (Wilcom US6587745B1, Brother, DRAWstitch US10132018B2, Cimpress, Levi Strauss...). Voir aussi USRE38718E1 dans les citations.

(2) Claims : indépendantes 1 (appareil : unité d'amincissement, extracteur de région délimitée, unité de suivi de ligne fine, processeur de boucles fermées), 12 (méthode), 16 (support), 20 (appareil dont le processeur de boucle détache une boucle interne en supprimant sa branche de liaison). Dépendantes 2-5 (données de couture de région tatami + contour running/satin), 6-8 (détection et suppression des branches ; une branche = inversion de direction de 180 degrés ; branches reliant contour et boucle interne), 9-11 (boucle détectée quand un pixel est suivi deux fois ; découpe en contour interne ; décision selon les sens de suivi aller/retour). Figures 10-17. Lecture complète du corps du brevet : les derniers ~35 000 caractères de la description n'ont pas tous été vérifiés, mais les claims ont été lus.

(3) Algorithme (DU BREVET, sans équation) :
- Pipeline : amincissement (Hilditch, Deutsch, Tamura cités comme exemples, n'importe quelle méthode à ligne 1 pixel) -> vectorisation par suivi et échantillonnage (intervalle non précisé) -> région à remplir désignée par l'utilisateur -> extraction -> élagage des branches par suivi du contour.
- Suivi de contour sur ligne fine, 8-voisinage : point de départ P = pixel noir de coordonnées extrêmes (Xmax, Ymax, « en haut à droite »). Contours internes parcourus anti-horaire (premier essai vers la gauche), externes horaires (premier essai vers la droite).
- Ordre de recherche du prochain pixel : si le chemin précédent est orthogonal, candidats dans l'ordre avant-droite, avant, avant-gauche, arrière (4 essais, compteur jusqu'à 3) ; si le chemin précédent est diagonal : droite, avant-droite, avant, avant-gauche, gauche, arrière (6 essais, compteur jusqu'à 5). Le premier pixel noir trouvé est retenu.
- Arrêt quand on retombe sur P. Si le chemin suivant est l'inverse (180 degrés) du précédent : branche (cul-de-sac), le chemin précédent est supprimé (dépilé). Mémoires de chemins et de pixels visités.
- Boucle : si le pixel suivant a déjà été visité, comparer le chemin de sortie à celui de la première visite ; différence de 180 degrés = boucle fermée. Une boucle reliée au contour par un « cou » est détachée et traitée comme contour interne ; une boucle en contact direct est conservée.
- Exemple : moustaches et cils (branches) supprimés, nez (boucle) devenu contour interne, tatami généré ensuite.

(4) Cas limites : trous internes multiples, boucles à cou, spur pruning par retour arrière. Coût linéaire en nombre de pixels du squelette. Pas de seuil de longueur de branche : toute branche est supprimée (donc détails fins perdus).

(5) Pertinence : MOYENNE. OpenStitch fait déjà squelette + vectorisation sur régions, donc la chaîne exacte n'est pas utile telle quelle ; l'apport conceptuel est l'élagage topologique des branches et la détection de boucles à col sur un squelette 1 pixel (cela pourrait servir à nettoyer les squelettes satin, voir docs/source/satin.md pour savoir si déjà fait). NOTRE PROPOSITION : élagage par longueur de branche + conversion de petites boucles en trous, sur le graphe du squelette plutôt qu'en suivi de pixels. Peu de gain si le squelette est déjà un graphe.

(6) Juridique : expiré (2018). Faible risque ; ne pas reprendre la rédaction.

---

## 3. US5283747A — Embroidery pattern data processor

(1) Brother ; inventeurs Kyozi Komuro, Atsuya Hayakawa, Hideaki Shimizu. Priorité 28/06/1989 (JP 1-167875), dépôt 18/06/1990, délivrance 01/02/1994. Expired - Lifetime (expiration prévue 01/02/2011). Famille : JPH0684585B2, JPH0333255A, DE4020463A1/C2, GB2235991A/B. Cite 7 brevets, cité par ~12-15.

(2) Claims : 19 revendications, indépendantes 1 (processeur pour machine multi-aiguilles : détermination section extrémité/fourche, bâti running de la fourche vers une extrémité, puis broderie en retour vers la fourche), 7 (variante colonne principale / colonnes branches), 14 (méthode). Dépendantes : broderie après ses points de bâti avant tout autre point ; sections polygonales à sommets ; bâti terminé à un sommet de la section d'extrémité ; passage par le barycentre de chaque section intermédiaire. Figures 4-8 (aire fermée E découpée en sections a-q ; cartes mémoire).

(3) Algorithme (DU BREVET, sans équation) :
- Entrée : aire fermée découpée en sections polygonales (quadrilatères : sommets 1-2 côté début, 3-4 côté fin), ordre arbitraire ; données de densité.
- Adjacence : pour chaque section, chercher les autres sections partageant une paire de sommets (côté) -> liste d'adjacentes + données de bordure.
- Classification par nombre d'adjacentes : 1 = section d'extrémité ; 2 = intermédiaire ; 3 ou plus = fourche ; 0 = traitement standard.
- Parcours depuis la première section avec compteur CNT de fourches ouvertes et pile BB[CNT] ; drapeaux « traité », « bordure effacée ». Quand on atteint une extrémité de branche : calcul du bâti running passant par les barycentres des sections de la branche et finissant à un sommet de l'extrémité non partagé ; puis on brode la branche en sens inverse vers la fourche (échange des sommets début/fin pour garder la direction cohérente).
- Contrôle de validité à la fourche : une seule bordure non effacée doit rester, sinon l'aire non traitée serait scindée ; si échec, choisir une autre bordure adjacente.
- Exemple : colonne principale a-f, branches depuis les fourches c et j.
- Aucun paramètre numérique ; barycentre = moyenne des sommets (c'est la définition usuelle, non une équation du brevet).

(4) Cas limites : sections en anneaux (cycles) non traitées explicitement ; plusieurs fourches imbriquées via pile. Coût O(n) après adjacence (O(n^2) naïf).

(5) Pertinence : MOYENNE pour le routage des réseaux de satin multi-sections (auto_satin « multi-section networks » et routage dans satin.md) : l'idée « aller le long d'une branche en bâti puis broder au retour pour finir à la fourche, sans fil croisé » est un schéma d'ordonnancement d'arbre. À comparer avec ce qui existe déjà dans stitch_generation/optimization avant de décider ; je n'ai pas lu le code. NOTRE PROPOSITION : parcours en profondeur du graphe de sections avec pile de fourches.

(6) Juridique : expiré (2011) ; aucun risque notable.

---

## 4. US5343401A — Embroidery design system (Pulse Microsystems)

(1) Pulse Microsystems Ltd ; inventeurs Brian J. Goldberg, Anastasios Tsonis. Priorité = dépôt 17/09/1992 (US 07/946,753), délivrance 30/08/1994. Expired - Lifetime (expiration prévue 17/09/2012) ; garantie PNC 1999 sans lien. Lié à US5270939 (Method for Modifying Embroidery Design Programs, 1993). Cite Childs US4352334, Wilcom US4821662, Tokyo Juki US4720795, Melco US5056444, Goldman US4149246 ; cité par ~47-67 (Pulse, Brother, Zazzle, Melco).

(2) Claims (lus au second passage, paraphrase) : 1 = conversion d'un motif alphanumérique en format bridge (point de départ + second point pour la largeur + hauteur ; mise à l'échelle des points du contour ; l'utilisateur définit un arc ; l'échelle y de chaque point dépend de son x). 4 pourcentage de « bridge rise » signé ; 5 espacement entre caractères mis à l'échelle ; 6 chaînage de formats (vertical, rotation, arc, cercle, trois lettres, arc vertical, trois types de bridge) ; 9 version à partir d'un cercle et de deux points. L'essentiel du document (>150 000 caractères) décrit l'interface menu d'un poinçonneur PC, sans intérêt algorithmique ; les ~2 000 derniers caractères n'ont pas été lus.

(3) Algorithme : seules trois équations sont rendues (DU BREVET, telles qu'écrites) : Flat Top newy = ybaseline + ypcnt*(y - ybaseline) ; Flat Bottom newy = lefty - ypcnt*(lefty - y) ; Double newy = yhalfway + ypcnt*(y - yhalfway). ypcnt, xpcnt et Leftbrdg dépendent d'équations NON RENDUES (placeholders) : je ne peux pas les reconstruire, ne pas les inventer. Description verbale : mise à l'échelle X = largeur totale des lettres + espacements sur largeur voulue, Y = hauteur de police sur hauteur voulue ; le facteur y est interpolé entre un pourcentage au bord gauche et au bord droit de la lettre ; le côté de la lettre est traité comme une droite et non un vrai arc. Les contours sont stockés comme points regroupés en segments/groupes, les points de couture sont générés ensuite à taille/densité demandées (ce qui évite de transformer chaque point).

(4) Cas limites : approximation de l'arc par segment droit ; déformation en bordure de lettre.

(5) Pertinence : FAIBLE. Hors numérisation auto. Utile seulement si OpenStitch ajoute du texte déformé (enveloppes). La transformation d'objets contour avant génération de points est déjà l'architecture d'OpenStitch (points dérivés, jamais stockés).

(6) Juridique : expiré (2012). Les formats de menu et libellés de produit sont de l'expression, à ne pas imiter.

---

## 5. US4849902A — Stitch data processing apparatus for embroidery sewing machine

(1) Brother ; inventeurs Masaaki Yokoe, Yoshikazu Kurono, Kouji Hayashi, Miho Hashimoto. Priorité 21/11/1986 (JP 61-279434 et 61-286273), dépôt 19/11/1987 (US 07/122,765), délivrance 18/07/1989. Expired - Lifetime (expiration prévue 19/11/2007). Famille : DE3739647C2, GB2199165B. Cité par ~54.
RESERVE : le second passage de lecture (offset 100000) a renvoyé des données d'un autre document (Bernina US8219237B2 / US20080022910A1, citations Janome, Mitsubishi) ; ces éléments sont des « brevets similaires » du bas de page, à ignorer. Je n'ai donc pas pu confirmer la fin de la page, ni la présence d'équations.

(2) Claims : 1 (appareil : support d'original, lecteur, affichage, saisie de points de contour, affichage de la ligne de contour, saisie de sommets découpant l'aire en blocs polygonaux, saisie de densité, calcul des données de point à partir des sommets et de la densité, mémorisation), 2 (zoom), 3 (CRT + crayon optique), 4 (caméra TV), 6-7 (photo / motif déjà brodé), 8 (indépendante : direction de référence et ligne de référence affichée).

(3) Algorithme (DU BREVET ; équations EQU1/EQU2 rendues en image, donc exactes non disponibles) :
- Échelle : un point de référence F = origine ; rapports X/Y égaux ; exemple 400 points d'écran = 80 mm donc 0,2 mm par point ; point D1 (40,380) -> C1 (8,76) mm (multiplication par 0,2).
- Satin par bloc quadrangulaire (sommets 101-104, densité N fils/mm) : milieux M1-2 (101-102) et M2-3 (103-104) ; distance l_B1 entre eux ; nombre de divisions m = N x l_B1 ; diviser les côtés 101-103 et 102-104 en m parties ; parcours alternant 101 -> 102 -> 110 -> 121 -> 112 -> 104... (les points sont visités en zigzag d'un côté à l'autre).
- Bord courbe : polynôme f(X,Y) = A(X+aY+Ca)^n + B(X+bY+C_B)^(n-1) + ... + C ajusté par substitution des coordonnées des sommets (cubique ou plus avec trois points) ; deuxième calcul m2 = N x l' via milieux additionnels MA, MB, MC pour mieux estimer la longueur ; les courbes sont divisées en m1 ou m2 segments. L'énoncé « pentagonal » semble une erreur de transcription pour polynomiale.
- Méthode de la fente de référence : angle theta fixe ; une droite de référence passe par chaque point de contour choisi, on calcule ses intersections avec les autres segments, on ne garde que le segment jusqu'à la première intersection ; l'ensemble découpe la forme en blocs carrés ou triangulaires (triangle = deux sommets confondus) donnant une direction de couture uniforme.
- Mentionné comme alternative : détection de contour par saut d'intensité.

(4) Cas limites : blocs triangulaires, nombre m arrondi (non précisé), polynôme de haut degré lent.

(5) Pertinence : FAIBLE. Satin par division égale d'un quadrilatère = base de ce qu'OpenStitch fait déjà avec rails et barreaux. La « fente de référence » (découpe en blocs à direction constante) est proche des directional fills/guides auto. NOTRE PROPOSITION : rien de neuf à prendre.

(6) Juridique : expiré depuis 2007.

---

## 6. US7386361B2 — Embroidery data creation device, method, program (Shima Seiki)

(1) Shima Seiki Mfg. ; inventeurs Takeuchi Nobuyuki, Okubo Atsushi, Nishioka Hisataka. Priorité 15/10/2003 (JP2003-355163), PCT/JP2004/013108 (09/09/2004), US 10/575,787, délivrance 10/06/2008 (publ. US20070129840A1). Statut : Expired - Fee Related ; événements juridiques : brevet expiré par défaut de paiement de maintenance (07/2020, effet 10/06/2020). Remarque : le premier résumé annonçait une date ajustée 2024-11-05 qui contredit les événements juridiques ; je retiens l'expiration 2020, à vérifier au registre USPTO. Famille : WO2005038118A1, EP1676945B1, JP4153859B2, KR101099609B1, CN1867723B, US20070129840A1. Cité par (Nike, Janome, CreateMe...).

(2) Claims : 1 (appareil : dispositif de dessin donnant position, pression, vitesse, inclinaison ; unité de points d'aiguille générant des points au fil du tracé selon largeur/densité/angle ; détection de courbure et règle de correction croissante avec la courbure ; affichage en temps réel), 3 (largeur diminue avec la pression), 5 (interpolation selon la densité, aiguilles à angle et largeur donnés), 6 (ordre des traits, suppression de points dans les zones de recouvrement), 7-8 (simulation, éclairage monotone), 10 (méthode), 14 (support).

(3) Algorithme (DU BREVET, aucune équation) : échantillonnage du stylet (position, pression, inclinaison par décalage pointe/base, vitesse par différences) ; points intermédiaires insérés à espacement fixé par la densité (exemple 5 points entre P0 et P1) avec interpolation de la pression ; chaque point = centre d'un point de couture, deux aiguilles de part et d'autre à angle theta (exemple <= 30 degrés pour bord doux) et largeur modulée par la pression ; correction de courbe : courbure = taux de variation de la direction ; petit angle de tracé -> réduire la largeur côté intérieur (légère réduction extérieur) pour éviter l'amas, grand angle -> augmenter la largeur ; chevauchement de traits : numéro de trait, suppression des points de l'ancien ou du nouveau selon règle, points supprimés mémorisés, ajout de points si point de liaison trop long ; éclairage : source par hauteur theta et azimut phi, luminosité monotone le long du point.

(4) Cas limites : lissage optionnel (spline ou moyenne) des tracés tremblés ; recouvrement dans une boucle fermée.

(5) Pertinence : FAIBLE pour l'auto-digitizing ; MOYENNE si un mode « pinceau satin » à main levée ou la compensation de courbure du satin (réduire la largeur côté intérieur) est visé. La simulation d'éclairage est un apport de rendu seulement. NOTRE PROPOSITION : compenser la densité du satin sur courbure serrée côté intérieur (à comparer avec la gestion existante dans satin.md).

(6) Juridique : expiré selon l'historique (2020) ; brevets étrangers de la famille peuvent exister dans d'autres pays (EP, CN, KR, JP) ; statut non vérifié.

---

## 7. US6937919B1 — Embroidery data processing apparatus (Brother)

(1) Brother ; inventeurs Muto, Mizuno, Suzuki, Taguchi, Wakayama. Priorité 31/03/2004 (JP 2004-107034 / JP2005287763A), dépôt 01/02/2005, délivrance 30/08/2005. Expired - Lifetime, expiration prévue 01/02/2025 (si c'est bien le cas, déjà passée à la date d'aujourd'hui). Famille ID 34858533. Citée par US20070233309A1, US7693598B2 (Brother), US12091797B2 (Tajima). Cite JPH02133647A, US5775240A (Janome), US6587745B1 (Wilcom), US6629015B2 (Brother).

(2) Claims (32) : indépendantes 1 (concentrique), 10 (radial), 16 (elliptique), 17, 26, 32 (supports). Dépendantes : 2 (passage en coordonnées theta-R), 3-5 (espacement des cercles variable avec la distance au centre, monotone ou selon motif utilisateur), 6-9 (multi-couleurs, motifs différents, sans chevauchement, motifs inversés), 11-15 radial équivalents, 18-25 et 27-31 versions support. Figures 15-35 ; la figure 35 (équations de transformation polaire) n'est pas rendue.

(3) Algorithme (DU BREVET) :
- Entrées : région, centre C.
- Concentrique : l'utilisateur dessine une courbe de densité d = g(t), t dans [0,T] (densité = cercles par longueur). Densité moyenne d', distance G de C au point le plus éloigné de la région, nombre de cercles N = G x d'. Rayons Ri placés pour que chaque bande ait la même aire sous la courbe, R0 = 0 (la formule exacte n'est pas donnée, seulement ce critère).
- Méthode 1 : cercles autour de C, garder cercles/arcs dans la région, placer les points dessus (variante ellipses).
- Méthode 2 : contour passé en (theta, R) avec C pour origine ; cercles = droites parallèles à l'axe theta ; segments dans le contour transformé joints en une ligne L2 ; transformation inverse x = R cos(theta) + cx, y = R sin(theta) + cy ; points placés sur L.
- Méthode 3 : points placés sur L2 dans l'espace theta-R puis transformés.
- Méthode 4 : multi-couleurs, motifs décalés pour que les cercles de couleurs différentes ne se chevauchent pas.
- Radial : même schéma avec d = h(s), s dans [0,360], N = 360 x d', angles à aire égale, demi-droites depuis C par rapport à une direction de référence SD ; mêmes méthodes 1-4.
- Elliptique : ellipses déformées des cercles.

(4) Cas limites : point de départ/arrivée sur les arcs coupés (jonction des segments L2), région non étoilée par rapport à C (plusieurs segments par cercle), centre hors région, couture continue vs sauts.

(5) Pertinence : MOYENNE à ÉLEVÉE comme NOUVEAU type de remplissage (OpenStitch n'a que tatami/contour en autodigitize). Implémentable indépendamment : motif concentrique/radial/elliptique via intersection de courbes avec un polygone (Clipper2 + paramétrage polaire). Le compromis technique est la manière de lier les arcs en une couture continue. Reste utilitaire/décoratif, pas de l'auto-digitize. NOTRE PROPOSITION : ajouter un type « remplissage polaire » dans DirectionalFillParams avec centre et profil de densité. À noter : le remplissage curviligne existe déjà chez Wilcom (US6587745B1) mais n'est pas l'objet de ce brevet.

(6) Juridique : expiré ou en expiration (2025) ; faible risque. Vérifier la date exacte.

---

## Bilan

Seuls 5839380 et 6356648 touchent la chaîne image -> régions (déjà très couverte par OpenStitch). Les plus utiles à un développement : 6937919 (nouveau type de remplissage, haute pertinence) et 5283747 (ordonnancement par arbre de sections, moyenne). 5343401, 4849902, 7386361 : faible. Tous les statuts affichés sont expirés selon Google, hypothèse non juridique. Avertissement : texte non primaire ; formules rendues en image absentes (5343401 ypcnt/xpcnt, 4849902 EQU1/2, 6937919 fig. 35) et je n'ai pas inventé leur forme.
