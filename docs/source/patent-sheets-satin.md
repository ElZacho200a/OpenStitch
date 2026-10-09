# Fiches brevets : satin, squelette et jonctions

> Fiches techniques de travail, issues de la lecture de pages Google Patents (résumés automatiques, pas les PDF). Statuts juridiques préliminaires. Voir l'index et les priorités dans [Recherche brevets](patent-research.md).


Méthode : texte lu via Google Patents (WebFetch, résumé automatique du texte de la page, pas le PDF). Limites : les pages donnent le texte sans numéros de paragraphes ; les équations de US6390005 sont issues d'un OCR dégradé ; les 10 k derniers caractères de US6397120 et ~35 k de US6587745 n'ont pas été relus en intégral (la partie 2 de US6587745 a été relue, elle contient les revendications). Tout ce qui est marqué "FROM PATENT" est une formule présente dans le texte ; "OUR PROPOSAL" est une idée à nous, jamais attribuée au brevet. Statuts légaux = affichage Google, PRÉLIMINAIRE, pas un avis juridique.

## Constat transversal important (catégories)

- Les deux brevets censés motiver le "moteur satin guidé par squelette" ne disent PAS ce que la note de cadrage leur prête :
  - EP0761860B1 (Shima Seiki) : axe médian + points de branchement + découpe en "closures" + orientation perpendiculaire à l'axe, interpolée. Pas de pas d'échantillonnage, pas de Lmax, pas de modulo 180, pas de 0 degré par défaut, pas de "guides" (au sens courbes utilisateur).
  - US6390005B1 (Pulse) : ce n'est PAS un brevet de squelette ; il traite l'espacement inter-points constant d'un satin sur courbe (inset de densité). Catégorie à corriger : "calcul de la position des points d'un satin tournant / compensation de densité dans les courbures".
- US6397120B1 : pas de la "généralisation de squelette" mais de l'UI de gestion des singularités (jonctions) + base d'apprentissage. Catégorie : interprétation des jonctions du squelette / UX.
- US6253695B1 : modification de densité sur un jeu de points existant (post-traitement). Catégorie : édition de densité.
- US6587745B1 : remplissage tatami à lignes courbes par transformation bilinéaire XY<->UT. Catégorie : fill à direction courbe (pas satin).
- US6690988B2 : bitmap -> squelette (Zhang-Suen) -> objets par parcours de graphe. Catégorie correcte.
- Doublons de familles : US6010238A = contrepartie US d'EP0761860B1 (même famille, ne pas compter deux fois). US6587745B1 cite US6390005B1. Aucun autre doublon entre les six.

---

## 1. EP0761860B1 - Embroidery data generating system

(1) Biblio. Titulaire Shima Seiki Mfg Ltd ; inventeur Kenji Kotaki. Priorité JP 213128/95 du 22-08-1995 ; dépôt 22-08-1996 (EP96306137) ; délivrance 22-03-2000. Désignés : DE, ES, FR, GB, IT. Statut Google : Expired - Lifetime, expiration prévue 22-08-2016 ; déchéances pour taxes dans plusieurs États. Famille : EP0761860A2/A3, US6010238A, KR100406774B1, AU711370B2, DE69607268T2, ES2143720T3, TW396227B. Cité par US6836695B1, US8095232B2, JP2007175087A, JP2013146366A, US9164503B2.

(2) Revendications pertinentes (10) :
- 1 (appareil, indépendante) : générateur de contour, générateur de "closures", déterminateur d'orientation, déterminateur de points d'aiguille ; ajoute calcul de lignes caractéristiques internes, recherche des points de branchement/extrémités, lignes de division près des branchements, orientations déduites des lignes caractéristiques.
- 2 : lignes caractéristiques = lignes médianes. 3 : lignes de division = bissectrices des espaces entre branches de l'axe médian près du branchement. 4 : intersections lignes de division / contour. 5 : si la 1re intersection n'est pas le contour (autre ligne), on fait pivoter la ligne de division autour du point de branchement jusqu'au contour (Fig. 7, mode 3).
- 6 : orientations fixées en au moins 2 points de l'axe d'une closure, au moins une dérivée de l'axe voisin, les autres interpolées. 7 : orientation ~ perpendiculaire à l'axe. 8 : première orientation perpendiculaire à l'axe en un point, seconde parallèle à la droite joignant les deux intersections contour près du branchement, reste interpolé.
- 9 : version méthode ; 10 : méthode de broderie.
Figures : Fig. 2 (étapes s1-s11), Fig. 3 (parcours de l'axe, t1-t12), Fig. 4 (1 branchement, 3 closures A1-A3), Fig. 5 (2 branchements, closures B1-B3), Fig. 6 (axe circulaire), Fig. 7 (collision des lignes de division).

(3) Algorithme (reconstruit, sans équation dans le brevet) :
- Contour obtenu depuis l'image (s2-s3). Axe médian M (s4) : l'exemple donné est "bandes étroites posées vers l'intérieur depuis le contour jusqu'à contact" ; l'axe est à égale distance des portions de contour opposées, son orientation étant la moyenne des leurs. Variante : rapports de pas non centrés (ex. 2:1) par région, sinon axe médian.
- Parcours de l'axe depuis un sommet de départ pour repérer branchements P et extrémités (Fig. 3).
- Closures (s7) : à chaque branchement, ligne de division = bissectrice de l'espace entre branches ; intersection avec le contour ; on relie ces intersections au point P. Cela découpe la forme en sous-régions simples (une closure = une région sans fourche). L'utilisateur peut décaler le point de départ d'une ligne de division. Points de coude traités comme branchements ou points d'orientation utilisateur (exemple lettre F).
- Orientations (s8) : perpendiculaire à l'axe aux extrémités (inclinaison permise ; tolérance citée ~ +/-10 degrés) ; au branchement, parallèle à la droite joignant les deux intersections contour (ou perpendiculaire à partir de P au contour) ; autres positions interpolées. Axe circulaire (Fig. 6) : plusieurs points d'orientation, un nouveau chaque fois que l'orientation change de plusieurs dizaines de degrés.
- Points d'aiguille (s9) : à partir closures + orientations + "embroidery density data" en RAM (aucune formule).
FROM PATENT : aucune équation. L'interpolation est mentionnée en mots seulement ("interpolate", "smoothly interpolated") ; la forme (linéaire ou autre) n'est pas donnée.

Réponse précise aux points demandés :
- Squelette comme axe de référence : OUI (axe médian, revendications 1-3), mais le brevet ne parle pas d'échantillonnage régulier de l'axe.
- Croisements orientés = intersection ligne orientée / région, longueur de point : NON explicite. Le brevet détermine "orientation" et "points d'aiguille" sans détailler la génération des croisements.
- Angle par défaut perpendiculaire à la tangente pour squelette simple : OUI en substance (revendication 7, extrémités). Angle absolu 0 degré pour squelette complexe : NON, absent. À la place : découpage en closures (chaque closure est simple) + orientation parallèle à la corde entre intersections au branchement.
- Guides = fonction d'angle par interpolation linéaire, périodique mod 180 : PARTIELLEMENT. Interpolation entre orientations en au moins 2 points (rev. 6, 8) ; "linéaire" non précisé ; modulo 180 non mentionné ; "guides" utilisateur au sens courbes absents (seuls les points d'orientation utilisateur existent).
- Lmax / découpage en points de longueur y : NON. Pas de longueur de point.
- Pas h adaptable : NON (seulement "embroidery density data").

(4) Cas limites : collision de lignes de division (rotation autour de P, rev. 5) ; axe circulaire ; coudes. Coût : non donné ; de l'ordre calcul d'axe + parcours linéaire de l'axe.

(5) Pertinence OpenStitch : MOYENNE. Nouveau pour nous : (a) découpage systématique en closures par bissectrices aux branchements avec rotation anti-collision ; (b) orientation interpolée entre une valeur "perpendiculaire à l'axe" et une valeur "parallèle à la corde" au branchement. On a déjà squelette, ancrage de jonctions, rails/guides. Ce qui motive le moteur guidé par squelette est surtout le principe global ; les détails (Lmax, mod 180, 0 degré) sont notre proposition.
OUR PROPOSAL (à ne pas attribuer au brevet) : interpolation linéaire de l'angle sur l'axe avec différence minimale modulo 180 ; 0 degré par défaut pour squelette complexe ; découpe Lmax ; pas h adaptable.

(6) Légal : expiré (taxes + terme 2016) selon Google ; idée seulement, ne rien copier du texte. Préliminaire.

---

## 2. US6390005B1 - Satin à espacement inter-points constant (Pulse)

(1) Biblio. Inventeurs Benito Chia, Brian Goldberg, Niranjan Mayya, Anastasios Tsonis ; cessionnaire courant Pulse Microsystems (cession 2001-04-02). Priorité/dépôt 1998-05-14 (US 09/079,011) ; délivré 2002-05-21 ; Google : Expired - Lifetime, expiration 2018-05-14. Aucune autre famille listée. Cité par US6587745B1 (Wilcom). Titre exact : "Method of filling an embroidery stitch pattern with satin stitches having a constant interstitch spacing".

(2) Revendications (24 ; indépendantes 1 et 17) : 1 = remplir une forme courbe en satin tournant en gardant un espacement prédéfini, en faisant varier dynamiquement l'inset de densité de chaque point suivant ; 2 = inset fonction de la longueur et de l'angle du point ; 9 = inset dépendant du point précédent et de la variation de forme entre extrémités ; 17 = chaque point créé avec son extrémité à une distance perpendiculaire prédéfinie de la droite du point précédent, mesurée le long de la courbe ; 24 = distance constante. Figures : 3-8 art antérieur, 9-11 boucle, 12-13 modèle de distance, 14-19 inset, 20-22 spirale test.

(3) Algorithme :
- Boucle (Fig. 9-11) : point initial avec extrémité p1 sur la courbe ; fixer isd (inter-stitch distance) ; résoudre le paramètre t ; si t dépasse la fin, arrêt ; sinon poser le point suivant avec extrémité p2 à isd de p1 ; recommencer. isd constant ou variable par paire.
- FROM PATENT (texte OCR, à revérifier sur le PDF) : (x1,y1,x2,y2) = f(t), t=0 = point précédent ; fx1(t)=a t^3+b t^2+c t+d, fy1=e t^3+f t^2+g t+h, fx2=i t^3+j t^2+k t+l, fy2=m t^3+n t^2+o t+p ; isd = sqrt(p*x1+q*y1+r) (distance perpendiculaire de (x1,y1) au segment précédent) ; isd^2 = (pa+qe)t^3+(pb+qf)t^2+(pc+qg)t+(pd+qh+r) ; résolution cubique directe ou approximation par quadratique/linéaire (isd^2 = m t + b). Remarque : les symboles p,q,r sont réutilisés pour deux choses dans le texte (coefficients de distance et coefficient de f), ambiguïté à signaler.
- Inset de densité : exemples du brevet pour d=0,4 mm : longueur 0,4 mm / angle 30 deg -> 26,8 % ; 0,28 mm / 45 deg -> 58,6 % ; 0,23 mm / 60 deg -> 100 %. Aucune formule générale lue ; ne pas en déduire une.
- Courbe test : spirale (t cos t, t sin t) (Fig. 20-22).

Réponse aux points demandés : squelette, angle par défaut, guides, interpolation, Lmax, découpage : TOUS ABSENTS. Le brevet ne parle que de position des points d'un satin sur contour paramétré et d'espacement. Seul lien : pas h variable/adaptatif = contrôle d'espacement.

(4) Cas limites : forme où t dépasse la fin ; racines multiples du cubique (choix de racine non détaillé lu) ; virages serrés (inset jusqu'à 100 %). Coût : une résolution polynomiale degré <= 3 par point.

(5) Pertinence : MOYENNE-BASSE pour le moteur de squelette, MOYENNE pour la qualité de satin dans les courbes : idée de placer chaque nouveau point à distance perpendiculaire constante du précédent (au lieu d'un pas le long du rail), ce qui évite la sur-densité du côté intérieur. Notre satin a probablement un pas le long des rails ; vérifier nos rails/rungs avant d'adopter. Nouveau : critère de distance perpendiculaire à la droite du point précédent, inset variable.

(6) Légal : expiré (terme 2018) selon Google ; préliminaire.

---

## 3. US6690988B2 - Description objet depuis un bitmap (VSM / Kaymer, Bysh)

(1) Biblio. Titulaire d'origine VSM Group AB (courant KSIN Luxembourg II selon Google). Priorité GB0120472A 2001-08-22 ; dépôt 2002-08-22 ; publication demande US20030074100A1 2003-04-17 ; délivrance 2004-02-10. Expiré (taxes 2016). Famille : US20030074100A1, GB2379454A/B.

(2) Revendications (22) : 1 = squelette du bitmap, analyse en noeuds et chemins, parcours, série d'objets. 2 = parcours depuis un noeud, chaque chemin parcouru deux fois. 3-5 = 1er passage type de point A (linéaire), 2e passage type B (linéaire ou remplissage). 6 = contour définissant une partie de la frontière des objets du second type. 7-8 = amincissement Zhang-Suen. 9 = agrandissement préalable du bitmap. 15-22 = processeur équivalent.

(3) Algorithme :
- Isoler le sujet, bitmap binaire. Agrandir (x5, minimum x3) par interpolation préservant la courbure pour que le squelette reste loin du contour.
- Contour : pixel actif avec voisin 4-connexe inactif ; suppression des redondances d'escalier.
- Squelette : suppression des pixels "fluff" (connectivité < 2 et < 3 voisins actifs), puis Zhang-Suen à deux sous-passes (conditions de connectivité 1, 2-6 voisins actifs, et deux tests de voisins inactifs ; deuxième sous-passe symétrique) jusqu'à stabilité ; plafond d'itérations (sujet trop épais -> abandon). Re-suppression d'escaliers.
- Noeuds/chemins : matrices 3x3 de noeuds (jonctions 3 et 4 branches, noeud isolé/extrémité), chemins stockés en tableaux de pixels.
- Filtrage d'artefacts (Fig. 6-8) : despiking (courts chemins vers noeud isolé partant d'un noeud de valeur 3 près d'un angle vif du contour), debowing (chemins qui dévient puis convergent vers un angle vif, distances au contour divergentes), deforking (deux dents + noeud commun supprimés, chemin prolongé jusqu'au contour). Prolongement des chemins terminaux jusqu'au contour ; suppression des chemins courts insignifiants.
- Points de contrôle (Fig. 9) : à chaque jonction, intersections du contour avec les bissectrices des chemins, changements de direction brusques proches, points du contour les plus proches ; objets bornés par segments de contour + segments reliant les points de contrôle au noeud ; deux points de contrôle joints directement -> objet triangulaire.
- Parcours récursif : choisir un chemin non parcouru, le suivre en génération running ; quand tous les chemins du noeud ont été faits, repasser le dernier chemin en satin/remplissage. Ainsi chaque chemin est vu deux fois et l'aiguille reste dans le contour.
FROM PATENT : aucune équation, que des conditions de pixels.

(4) Cas limites : sujet trop épais (plafond d'itérations), artefacts de squelette (pointes, arcs, fourches), forme avec jonctions de 4. Coût : non indiqué ; Zhang-Suen O(N x itérations) sur bitmap agrandi x5 (donc 25x de pixels).

(5) Pertinence : MOYENNE (squelette déjà fait). NOUVEAU : (a) double parcours de chaque arête (running aller, satin/fill retour) pour un trajet sans coupe ; (b) filtres explicites despike/debow/defork ; (c) points de contrôle de jonction par bissectrices -> même idée que les closures d'EP0761860 ; (d) agrandissement x5 avant amincissement. Comparer à notre élagage SkeletonGraph.

(6) Légal : expiré (taxes) selon Google. Préliminaire.

---

## 4. US6397120B1 - Gestion des singularités (Goldman / Soft Sight)

(1) Biblio. Inventeur David A. Goldman ; cédé à Soft Sight (1999-12-30), puis Vistaprint/Cimpress. Dépôt/priorité 1999-12-30 (US 09/476,245) ; délivré 2002-05-28 ; Expired - Lifetime, expiration 2019-12-30. Incorpore la demande mère 09/134,981 (non lue, non couverte ici). Pas d'autre famille listée. Cité par US7587256B2, US8706286B2.

(2) Revendications (20 ; indépendantes 1, 7, 13) : détection d'une singularité, génération d'une interprétation initiale via un mécanisme SPM, modification pour meilleur rendu (1) ; validation temps réel (2-3) ; interface "choix suivant" (5) ; base d'apprentissage (6, 17-20) ; ensemble d'interprétations ordonné (15) ; métriques structurelles (16).

(3) Algorithme (aucune équation, tout en prose) :
- Pipeline (Fig. 1a, méthode 400) : lissage, segmentation couleur (objets < 6 pixels fusionnés), code chaîne, classification mince/épais par moyenne et écart-type de la transformée de distance le long du squelette (faible écart + faible épaisseur = trait satin), branche épaisse : choix parmi 16 angles de balayage celui qui donne le moins de fragments, puis ordonnancement minimisant les coupes ; branche mince : lignes ajustées, squelette en branches, étiquetage contours extérieur/intérieur/squelette, SPM, lissage des colonnes (variation des normales), points satin aux extrémités des normales.
- SPM (Fig. 1b) : à une singularité (jonction), toutes les paires de régions régulières donnent des interprétations candidates ; reconstruction des frontières occultées par minimisation d'énergie (transition lisse) ; classement ; la première est par défaut. Métriques extraites : nombre de branches, vecteurs squelette -> points caractéristiques de contour (CEP), différences angulaires successives, angles internes et excentricité du polygone des CEP, angles de concavité (Fig. 4), épaisseur, stats. Récupération de cas stockés par index, fusion avec les candidats selon similarité et compteur d'usage ; dédoublonnage. UI : icône "suivant" (Fig. 2a), édition de frontière avec validation en temps réel (Fig. 2b), stockage normalisé des corrections.
- Idée réutilisable pour nous : choix de 16 angles de balayage minimisant le nombre de fragments (fill), et le critère mince/épais par stat de la distance transform.

(4) Cas limites : interprétations multiples d'une jonction (lettre B, Fig. 2a/3) ; frontières hors contour (rubber-band). Coût : combinatoire en nombre de branches (paires).

(5) Pertinence : FAIBLE-MOYENNE. Nouveau : énumération d'interprétations ordonnées des jonctions + UI "suivante" ; heuristique mince/épais ; sélection d'angle de fill par comptage de fragments. Pas d'algorithme exact reproductible pour l'énergie (non donnée). Apprentissage = hors périmètre pour l'instant.

(6) Légal : expiré (terme 2019) selon Google, mais assignation à Vistaprint ; préliminaire. Une partie du texte (fin de page) non relue.

---

## 5. US6253695B1 - Changer la densité d'un groupe de points

(1) Biblio. Inventeurs Tik Yuen Chan, King Wa Leung, Wei Wang ; "Individual". Priorité provisoire 60/099,341 du 1998-09-08 ; dépôt 1999-09-08 (09/391,392) ; délivré 2001-07-03 ; Expired - Fee Related (déchéance 2005), expiration 2019-09-08. Pas d'autre famille.

(2) Revendications (7) : 1 (indépendante) reconnaissance de points frontière gauche/droite/normaux, classement, séparation en groupes, calcul d'une lecture de densité ; 2 normal/running ; 3 copier les lignes relatives voisines pour augmenter ; 4 supprimer pour diminuer ; 5 déplacer ; 6 réarranger les groupes running ; 7 variation d'angle de couture pour préserver la forme.

(3) Algorithme : prose seulement ; pas d'équation ; formule de densité non donnée (dimensions du groupe et nombre de rangées). Les détails des Fig. 6-7 n'étaient pas lisibles dans le texte.
(4) Cas limites : points de changement d'angle protégés. Coût non donné.
(5) Pertinence : FAIBLE. Opération de post-traitement sur points déjà générés ; or nos points sont dérivés (jamais stockés), donc l'équivalent chez nous est un paramètre de densité en amont. Rien de génériquement nouveau.
(6) Légal : expiré. Préliminaire.

---

## 6. US6587745B1 - Remplissage à lignes courbes (Wilcom)

(1) Biblio. Wilcom Pty Ltd ; inventeurs Alexander Polden, William Brian Wilson. Priorité AU AUPP5766A 1998-09-07 ; dépôt 1999-09-07 (US 09/786,799) ; délivré 2003-07-01 ; Expired - Fee Related (déchéance 2007), expiration 2019-09-07. Famille : WO2000014319A1, EP1112402A4, JP2002524170A. Cité par WO2005061774A1 (Wilcom, fill tournant) etc. Cite US6390005B1.

(2) Revendications (53 ; indépendantes 1 méthode, 19 système, 34 commandes de points) : 1 = zone, type de remplissage, courbe de définition de point ; lignes de remplissage et lignes de motif courbées par la courbe, pas par la forme de la zone. 5-11 = seconde courbe (indépendante, générée par décalage/translation/dilatation, ou dérivée d'une seule courbe), plus de deux courbes pour régions différentes. 12/29 = remplir une zone plus grande puis rejeter. 13-16 = lignes droites calculées puis transformées ; transformation vers un rectangle. 17 = ajustement post-traitement. Figures 3-9 (exemple 1), 10-15 (propriétés), 16-19 (effets, trou triangulaire, courbe unique, trois courbes).

(3) Algorithme (Exemple 1, deux courbes) :
1. Type + paramètres (ex. tatami, longueur 4,0 mm, espacement 2,0 mm, décalages A,B = 0,25).
2. Contour fermé, trous possibles.
3. Deux courbes encadrant la zone.
4. Découpage en quadrilatères non croisés entre les courbes (droites joignant premiers/derniers points et lignes intermédiaires).
5. Contour converti en petits segments avec sommets aux intersections de chaque côté de quadrilatère.
6. Chaque quadrilatère XY -> rectangle UT : U longueur le long de la 1re courbe, T distance le long de la ligne de jonction ; hauteur commune Tmax (moyenne pondérée des hauteurs de tranches), largeur = moyenne des largeurs haut/bas.
7. Contour transformé en UT ; 8. remplissage droit standard en UT (tatami, program split, motif) ; 9. transformation inverse : les droites deviennent des courbes.
Équations (texte source dégradé, reconstitué par le résumeur ; à vérifier sur le PDF) :
- FROM PATENT : u* = (u-u0)/(u1-u0), t* = (t-t0)/(t2-t0) ; g(u*,t*) = q0 + u*(q1-q0) + t*(q2-q0) + u*t*((q0+q3)-(q1+q2)) (inverse UT -> XY, interpolation bilinéaire du quadrilatère q0..q3).
- Transformation directe XY -> UT : pas de forme close ; affine A envoyant q0,q1,q2 vers (1,0),(0,0),(0,1) ; A(g)=(u*+k u*t*, t*+l u*t*) avec (1+k,1-l)=A(q3) ; u*t* solution d'une quadratique ; puis u*, t*. Les définitions de k, l et le choix de racine doivent être confirmés sur le PDF.
- Courbe unique : F(t)+h((1-a)n(t)+a e), a dans [0,1] (a=0 décalage pur, a=1 translation), e = médiane du champ de normales ; dilatation P+(1+a)(F(t)+b n(t)-P). FROM PATENT.
Compensation de distorsion : pour program split, recalcul des points de coupe comme intersections avec les lignes inverse-transformées.
Non couvert dans les parties lues : traitement exact des trous, pondération Tmax, validité des quadrilatères non convexes.

(4) Cas limites : courbes qui doivent encadrer la forme ; continuité entre quadrilatères adjacents (Fig. 15) ; distorsion de longueurs de point. Coût : transformée par sommet O(1), quadratique pour l'inverse.

(5) Pertinence : MOYENNE-HAUTE pour notre fill directionnel (direction par courbe), BASSE pour le satin. NOUVEAU : paramétrage UT par grille curviligne + remplissage droit puis transformation (réutilise tout notre tatami) ; courbes F(t) +/- h n(t). Un point d'attention : la longueur de point change après transformation (compensation à prévoir).

(6) Légal : expiré (2007 déchéance, terme 2019) selon Google ; ne pas copier ; préliminaire.

---

## Synthèse pour le moteur satin guidé par squelette

Ce que les brevets soutiennent réellement : EP0761860B1 (axe médian, bissectrices aux branchements, orientation perpendiculaire interpolée) et US6690988B2 (jonctions + points de contrôle sur le contour, double parcours). Ce qu'ils ne soutiennent pas : pas d'échantillonnage régulier de l'axe, angle 0 degré, modulo 180, guides à interpolation linéaire, Lmax / découpage en points, pas h adaptatif (seul US6390005 touche à l'espacement, sur un autre mécanisme). Ces éléments restent donc notre conception propre (OUR PROPOSAL), ce qui est bon pour la liberté d'exploitation mais ne s'appuie sur aucune source.


---

## Errata après lecture des PDF (2026-10)

Ces fiches ont été écrites à partir de résumés de pages web. Les 32 brevets ont ensuite été relus sur leur **texte primaire** (PDF Google Patents ; équations et tableaux abîmés par l'OCR relus sur rendu image ; scans japonais lus visuellement). Les points ci-dessous **corrigent ou précisent** le corps de cette page ; en cas de conflit, ils priment. Les figures n'ont été regardées que lorsqu'une équation en dépendait.

- **EP0761860B1 (texte complet lu)** : la fiche est confirmée. Précisions : l'axe médian est obtenu en posant des bandes étroites depuis le contour jusqu'à contact ([0017]) ; un cercle parfait réduit l'axe à un point et l'orientation devient radiale ([0019]) ; une tolérance d'environ ±10° autour de la perpendiculaire est jugée sans importance car la couture est en zigzag ([0022]) ; hors de l'extrémité, le changement d'orientation peut être extrapolé ([0022]) ; pour un axe circulaire (trou), l'utilisateur désigne des points d'orientation ou on en crée un nouveau à chaque variation de plusieurs dizaines de degrés ([0066]). Le mot « linéaire » n'apparaît jamais ; aucun modulo 180°, échantillonnage, longueur de point ni pas.
- **US6390005B1 (texte complet lu)** : confirmé. `isd` est la distance perpendiculaire entre l'extrémité du nouveau point et la droite du point précédent ; `t` se résout par un cubique (approximation quadratique ou linéaire possible). Les exemples 26,8 % / 58,6 % / 100 % (30° / 45° / 60°) ne sont accompagnés d'aucune formule générale. La notation `sqrt(p·x1+q·y1+r)` réutilise p, q, r pour deux usages : ne pas la transcrire telle quelle.
- **US6587745B1** : l'application affine `A` envoie `q0, q1, q2` sur `(0,0), (1,0), (0,1)` (la fiche écrivait `(1,0), (0,0), (0,1)`). `A(q3) = (1+k, 1+l)` : le « 1−l » imprimé est une coquille du brevet. La formule bilinéaire `g(u*,t*)` est confirmée ; la formule imprimée de `A` a un signe faux, il vaut mieux résoudre un système linéaire 2×2. Sommets en ordre Z (`q0` et `q3` opposés). Absents du brevet : choix de la racine de la quadratique, poids de `Tmax`, quadrilatères non convexes, coût. La fiche omettait le critère de translatabilité (champ de tangentes tournant de moins d'un demi-tour), les revendications 18 et 44-53 et la compensation de distorsion.
- **US6690988B2** : la deuxième sous-passe de Zhang-Suen imprimée ne répète pas les tests de connectivité (omission probablement rédactionnelle). Seuils absents : plafond d'itérations, longueur « courte » du despike. Critères omis : despike = chemin parti à mi-angle entre deux autres et d'épaisseur décroissante vers le noeud isolé ; defork = segment entre les dents entièrement dans le contour. Le parcours est un retour arrière. La mention GB2379454 n'est pas imprimée sur la page de titre. Revendications 10 à 14 omises.
- **US6397120B1** : aucun cessionnaire imprimé (« Soft Sight / Vistaprint » vient de Google, non confirmé). La « priorité 1999-12-30 » est la date de dépôt ; la mère 09/134,981 n'est qu'incorporée par référence. Seules constantes : 6 pixels et 16 angles.
- **US6253695B1** : la densité d'origine est estimée par « analyse statistique de l'espacement entre rangées » (figure 7), sans formule.
