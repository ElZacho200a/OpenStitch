# Fiches brevets : stippling, analyse de points et appliqué

> Fiches techniques de travail, issues de la lecture de pages Google Patents (résumés automatiques, pas les PDF). Statuts juridiques préliminaires. Voir l'index et les priorités dans [Recherche brevets](patent-research.md).


Source unique : Google Patents (pages /en), lues via un outil qui résume la page (pas de texte brut intégral ; pour US6167823B1 les 14 000 derniers caractères n'ont pas été lus ; les figures ne sont pas visibles, seulement décrites par le texte). J-PlatPat non consulté. Tout ce qui est marqué NOTRE PROPOSITION n'est pas dans les brevets.

## Corrections de catégorie
- US6167823B1 n'est PAS un brevet de « remplissages décoratifs ». C'est un brevet d'ANALYSE/ÉDITION de données de points via graphiques (points en fonction du temps). Il n'y a aucune génération de remplissage.
- US6968255B1 n'est PAS de l'analyse : c'est la dérivation automatique de motifs de STIPPLING (remplissage décoratif de type méandre fractal) par clipping dans un polygone.
- US5438520A et JP3769602B2 : appliqué (deux brevets Barudan), conforme.

---
## 1. US6167823B1 — Method and system for computer aided embroidery
**(1) Biblio.** Buzz Tools, Inc. ; inventeurs John S. Laufer, Lisa A. Laufer. Priorité et dépôt 21/07/1999 ; délivré 02/01/2001. Statut Google : expiré (échéance estimée 21/07/2019, hypothèse non juridique). Famille : PCT/US2000/019976 (WO2001007700A1), AU6364400A, continuation US 09/707,668 (US6502006B1). CPC D05B19/xx. Citations non lues (tronquées).

**(2) Revendications clés.** 13 revendications. 1 : affichage avec le numéro de point (ou le temps) sur un axe et la longueur de point sur l'autre. 2 : idem avec l'angle de point. 3 : méthode de sélection d'une région dans le graphe temporel, où des groupes de points sont séparés par un long point ; sélection au curseur. 4-5 : surbrillance correspondante dans une vue « points dans l'espace », en couleur différente. 6 : sélection par glissé. 9-10 : graphes de propriétés dérivées (angle) ; 11 : barre de couleurs proportionnelle au nombre de points ; 12-13 : éléments adjacents et événement de changement de couleur.

**(3) Algorithme (FROM PATENT, sans équation formelle sauf l'angle).**
- Modèle : suite ordonnée de coordonnées 1..n entrelacée de codes de contrôle (changement de fil, saut, fin).
- Graphe de longueur : une barre par point, hauteur proportionnelle à la longueur (ou à son log), couleur du fil. Les longs sauts marquent les frontières d'éléments.
- Graphe d'angle : chaque point est un vecteur vers le suivant ; deux vecteurs consécutifs ramenés à une origine commune, angle entre eux ; si > 180°, remplacé par 360° - angle, soit [0,180]. Signatures décrites : points de course au plancher, zigzag vers 90°, satin vers 180°, tatami en motif complexe avec demi-tours.
- Barre de couleurs : un rectangle par série de points de même couleur, longueur proportionnelle au nombre de points.
- Sélection « accrochée aux longs points » : longueur-curseur L déduite de la position verticale ; chercher en avant le point le plus proche de longueur > L (distance B) et en arrière (distance D) ; le texte rapporté dit « si B>D choisir A, sinon C », ce qui semble inversé par rapport à « le plus proche » : AMBIGU, à vérifier sur le texte intégral. Idem pour le point final. Tous les points entre les deux bornes sont sélectionnés, surlignés dans les deux vues.
- Sélection alternative par glissé (indices min/max) et par couleur (voisin le plus proche de couleur différente).
- Manipulations sur la sélection : couleur, transformation, suppression, réordonnancement, insertion d'arrêt couleur.
**NOTRE PROPOSITION** : rien d'autre n'est donné ; implémentation naïve O(n) par graphe.

**(4) Cas limites / coût.** Sélection O(n) par balayage. Cas non traités dans le texte lu : design sans long point, doublons de longueur. Le terme « temps » dépend de la vitesse machine (modèle non donné).

**(5) Pertinence OpenStitch : MOYENNE-BASSE.** Idée utilisable : un panneau d'analyse (longueur/angle par point, détection de frontières d'éléments) pour `stitch_analysis` et le débogage, côté `libs/` sans Qt (calcul) + widget. Principalement de l'UI ; le calcul est trivial (longueurs, angle entre vecteurs). Rien de nouveau à générer. Le dépôt n'a pas de graphe de ce type (non vérifié en détail).

**(6) Juridique (préliminaire).** Expiré (2019) selon la source ; revendications d'interface et de méthode d'affichage. Rien n'interdit l'idée aujourd'hui, mais ne pas copier de texte ; vérifier le statut des continuations (US6502006B1) si on s'inspire de ses revendications.

---
## 2. US6968255B1 — Stippling automatique (Pulse Microsystems / Tajima Software Solutions)
**(1) Biblio.** Inventeurs Christos Dimaridis, Niranjan Mayya, Thanasis Triantafyllidis, Yanchun Wang. Assignataire initial Pulse Microsystems ; actuel Tajima Software Solutions (fusion 2022). Priorité provisoire 22/10/2004 (60/621,340) ; dépôt 27/10/2004 (US10/974,839) ; délivré 22/11/2005. Statut Google : expiré, échéance ajustée 07/11/2024 (hypothèse). Famille : un seul membre listé. Cite US5430658A (Pulse, fractal auto-généré) et US6807456B1.

**(2) Revendications clés.** 20 revendications, indépendantes 1, 12, 16, 20. 1 : chemin unique sans auto-intersection dans une frontière géométriquement symétrique, conformé à un polygone asymétrique, points sélectionnés, stockés, segments de points créés. 3 : chemin issu d'une forme fractale itérée. 12 : choix d'une forme fractale, bordure symétrique, itérations, conformation, conversion en données de broderie. 13 : déplacement aléatoire des points. 14-15 : courbes de Bézier puis points de course. 16 : intersections avec le contour, découpe des segments, conservation de ceux à l'intérieur. 20 : combinaison. Remarque : la revendication 11 cite une « bordure de polygone édité » sans antécédent clair.

**(3) Algorithme.**
1. Boîte englobante du polygone avec marge prédéterminée (FROM PATENT).
2. Chemin : fractale définie par axiome + règles, n itérations ; description à base de Hilbert, segments selon deux directions orthogonales, chaque point visité une fois ; densité réglée par n (FROM PATENT).
3. Clipping (Table 1) : intersections entre polyligne motif et courbes de forme par droites paramétriques L1(t)=A+t·V0, L2(m)=B+m·V1 ; cas non parallèle : m = N0·(A-B)/(N0·V1), t = N1·(B-A)/(N1·V0) avec N0, N1 perpendiculaires à V0, V1, accepté si 0<=t<=1 et 0<=m<=1 (FROM PATENT, équations rapportées par la source). Cas parallèle/colinéaire par projections par produit scalaire, bornées à [0,1]. Trier les paires (k,t), supprimer les doublons, couper le motif, garder l'intérieur, conserver les informations de connexion.
4. Points sur/près du chemin ; longueur max de point définie par l'utilisateur (exemple 2 mm), subdivision au-delà.
5. Aléa (Table 2) : dx = m_len·(rand/RAND_MAX - 0.5)·RandomFactor, idem dy ; « m_len » est illisible dans la source, probablement une longueur de segment : INCERTAIN.
6. Lissage Bézier : pour chaque point intérieur, v1=P[i-1]-P[i], v2=P[i+1]-P[i], angle θ ; points de contrôle donnés par formules (1/3 P[i-1] + 2/3 P[i]) puis divisés par 3, qui paraît une erreur de transcription ; θ' = π/2 - θ/2 (corrigé de π si |θ'|>=π/2) ; rotation de ±θ' autour de P[i]. À ne PAS reprendre telle quelle : le texte est corrompu.
7. Re-clipping de la courbe lissée à l'intérieur du polygone.
La source mentionne aussi du texte parasite (« nutritional blend ») et deux numéros de brevet pour le même renvoi : texte de qualité faible.

**(4) Cas limites / coût.** Coût O(S·C) naïf (segments du motif × segments du contour) ; accélérable par grille. Problèmes : le clipping coupe le chemin continu en plusieurs morceaux, donc il faut relier les morceaux (travel/jump) : le brevet mentionne des « informations de connexion » mais sans détail lu. Polygones à trous non discutés (dans le texte lu).

**(5) Pertinence : MOYENNE** (HP-STI-013 stipple/méandre, P2, À faire ; HP-STI-006 motif fill). Nouveau et implémentable : motif de remplissage = courbe de remplissage d'espace (Hilbert/Peano ou autre) clippée au polygone, avec jitter déterministe (graine fixe pour respecter le déterminisme exigé) et lissage. Mais la plupart des briques sont génériques (clipping segment/polygone via Clipper2 dans `geometry`, courbe de Hilbert classique). NOTRE PROPOSITION : utiliser Clipper2 pour le clipping, une graine PRNG explicite, et un chaînage glouton des fragments en minimisant les sauts.

**(6) Juridique (préliminaire).** Expiré selon la source (2024) ; Hilbert et clipping sont de l'art public ; ne pas copier les formules Bézier douteuses. Revérifier avec un juriste si on se rapproche des revendications 1/16 de façon littérale (aucun risque attendu après expiration, sous réserve de la fiabilité du statut Google).

---
## 3. US5438520A — Method of creating applique data (Nippon Denpa / Barudan)
**(1) Biblio.** Assignataires d'origine Nippon Denpa Co., Ltd. et Barudan ; inventeurs Masaaki Satoh, Akinori Kuroda, Masashi Asai ; Google affiche Raytheon comme cessionnaire actuel (données douteuses selon Google). Priorité JP 02/04/1993 (JP5-100442) + priorités 17/04/1993, 22/04/1993, 14/03/1994 ; dépôt US 01/04/1994 (08/221,926) ; délivré 01/08/1995. Statut : expiré, échéance 01/04/2014 ; taxes de maintien payées. Famille : DE4411364C2, JPH06294065A, JP3810095B2, JP3345614B2, JP3878228B2.

**(2) Revendications clés.** 14. 1 (indépendante) : contour synthétique + bords de chevauchement de deux pièces, contours individuels, données de broderie par pièce. 2 : données de coupe. 3 : décomposition en points de positionnement provisoires, bâti, satin. 4 : départ des points provisoires aux intersections avec les bords de chevauchement. 5-6 : circonférences intérieure/extérieure pour bâti/satin. 7 : changement de fil sur la pièce supérieure entre bord de chevauchement et reste. 8 : choix automatique du côté par lignes de recherche et comptage d'intersections. 9 : zones à deux courbes (ligne frontière, rotation progressive). 10-14 : arrêt arrière (>= 4 points de retour), pas constant, saut, pas grossier, point de fin qui revient sur le dernier retour.

**(3) Algorithme (aucune équation dans le brevet).**
- Entrée : points essentiels R sur le contour combiné ; départ S un peu avant, fin E un peu après pour que la coupe se recouvre. Bords de chevauchement Da (pièce basse), Db (haute). D1a = Dt coupé à Da ; D1b = Dt coupé à Db. Coupe D2a/D2b.
- Positionnement provisoire (courses) D3 suivant le contour, avec départ aux intersections des bords de chevauchement ; bâti D5 (E-stitch en zone libre, course aux bords) ; direction et nombre de points saisis au clavier.
- Circonférences intérieure D4 et extérieure D6 par décalage ; satin D7 par segments élémentaires perpendiculaires aux courbes, échantillonnés sur les points du contour.
- Choix automatique du côté intérieur : lignes de recherche depuis un point du contour, comptage du nombre X d'intersections avec le contour ; parité (0/pair vs impair) combinée à la direction de parcours (montante/descendante) donne le côté (Table 1 : montante + pair => intérieur à droite pour recherche vers la gauche ; impair inverse ; descendante inverse ; recherche vers la droite inverse). C'est un test point-dans-polygone par parité d'intersections. FROM PATENT.
- Zones à deux courbes : intersection D4bx / D6bx, ligne fantôme frontière 43 ; la direction du satin devient progressivement parallèle à cette frontière à son approche, ailleurs perpendiculaire au contour.
- Échange de fil : bord de chevauchement de la pièce haute dans la couleur de la pièce, reste en couleur contrastée.
- Arrêt arrière : aller vers le bord puis retour d'au moins 4 points (modes d'exemple : 15, 15 et 9 points ; 9-15 dans la description), 3 variantes (pas constant ; pas constant + 1 saut ; pas constant puis pas grossier), puis point final qui revient partiellement sur le dernier retour. La pièce dépasse du bord de 1 à 2 mm (jusqu'à ~4 mm) pour accrocher le tissu. Valeurs FROM PATENT, exemples.
- Séquence de couture : coupe, positionnement, bâti, satin (bord de chevauchement de la haute d'abord).

**(4) Cas limites / coût.** Pièces concaves ou à contour non simple : d'où la parité ; décalage de courbes à angles vifs non traité dans le texte lu ; deux pièces seulement (superposées). Coût linéaire ou quadratique naïf pour le test de parité.

**(5) Pertinence : HAUTE pour l'appliqué** (HP-SPEC-001 P1, HP-SPEC-002 P2 : « rien » existe dans le code métier, vérifié par le roadmap l.1693 ; grep des libs sur « motif/appliqué » ne retourne que des faux positifs « applique »). Nouveau et implémentable : type d'objet appliqué avec passes ordonnées ligne de placement, bâti/tack-down, satin de couverture, arrêts machine (`Stop`), export du contour de coupe via `formats`. L'idée « une saisie, plusieurs sorties » est exactement l'objectif du roadmap. Les détails d'arrêts arrière sont en revanche anciens et spécifiques aux machines Barudan ; peu utiles. Le choix du côté par parité est inutile chez nous (Clipper2 gère l'orientation et les offsets signés).

**(6) Juridique (préliminaire).** Expiré en 2014 selon la source. Techniques d'appliqué (placement/tack-down/satin) sont communes dans l'industrie. Faible risque ; revalider si on cite le texte.

---
## 4. JP3769602B2 — Dispositif de création de données de coupe et de broderie d'appliqué (Barudan)
**(1) Biblio.** Barudan Co., Ltd. ; inventeur Shin Iwata (岩田 晋). Priorité 25/01/1993 (JP5-29945) ; dépôt 30/01/1993 (JP03424093A) ; publication A 27/09/1994 (JPH06272151A) ; délivré 26/04/2006. Statut : expiré, échéance 2021-04-26 (hypothèse Google). Famille : AU659042B2, DE4401948C2, US5740055A (publié 14/04/1998). Traduction automatique Google : page lue, original japonais non consulté (J-PlatPat non vérifié).

**(2) Revendication.** Une seule revendication indépendante (dispositif) : moyen d'entrée du contour du tissu d'appliqué (D1) ; une unité commune qui crée automatiquement les données de coupe et de broderie ; les données de broderie sont dérivées des données de périphérie intérieure (D4) et extérieure (D5), puis des données de point satin (D7).

**(3) Algorithme (procédural, sans équation).** Une seule saisie du contour D1 (coins C1-C6, arcs R1-R3, S avant C1 et E après C6 pour chevauchement de coupe) → coupe D2 → bâti de positionnement D3 à pas fixe → périphérie intérieure D4 → extérieure D5 → points de fixation D6 (E-stitch ; direction et nombre saisis) → satin D7 en divisant la bande entre D4 et D5 en sections avec direction variant progressivement pour éviter les chevauchements aux coins. Sortie : disquette FD1 coupe, FD2 broderie. Ordre de couture : bâti, fixation, satin.

**(4) Cas limites.** Coins : variation progressive de direction par section ; pas de détail numérique. Coupe : chevauchement S/E.

**(5) Pertinence : HAUTE** comme pendant simplifié du précédent (un seul tissu) ; confirme le flux « contour unique → découpe + broderie ». C'est le plus proche de HP-SPEC-001. Rien de plus que US5438520A, sauf la décomposition de la bande satin en sections à direction progressive aux coins (idée déjà couverte par la logique satin du dépôt, cf. docs/source/satin.md, non relue ici).

**(6) Juridique (préliminaire).** Expiré (2021 estimé) ; le brevet US5740055A de la famille n'a pas été lu et son statut non vérifié ; l'idée générique est ancienne.

---
## Synthèse rapide
- Vraiment nouveau et implémentable : (a) objet appliqué multi-passes avec arrêts + export de contour de coupe (brevets 3 et 4) ; (b) stipple par courbe d'espace clippée avec jitter déterministe (brevet 2) ; (c) panneau d'analyse longueur/angle (brevet 1), faible priorité.
- Limites de ce travail : résumés par outil (pas de texte brut), figures non vues, citations de 6167823 non lues, texte de 6968255 corrompu à plusieurs endroits.


---

## Errata après lecture des PDF (2026-10)

Ces fiches ont été écrites à partir de résumés de pages web. Les 32 brevets ont ensuite été relus sur leur **texte primaire** (PDF Google Patents ; équations et tableaux abîmés par l'OCR relus sur rendu image ; scans japonais lus visuellement). Les points ci-dessous **corrigent ou précisent** le corps de cette page ; en cas de conflit, ils priment. Les figures n'ont été regardées que lorsqu'une équation en dépendait.

- **US6167823B1** : l'ambiguïté « B>D » est levée : le signe est « > » partout (colonnes 10 et 12, figure 11C), mais la règle imprimée choisit le point le plus éloigné alors que le texte dit « le plus proche ». C'est une erreur du brevet ; à corriger côté implémentation. Le double-clic entre deux longs points (figure 11E) et les manipulations sur la sélection manquaient. L'accrochage n'est pas revendiqué (revendications 1 à 13).
- **US6968255B1** : le texte imprime Pulse Microsystems seul (« actuel Tajima » non confirmé). Les formules Bézier sont imprimées exactement comme transcrites (avec le `/3` final et `theAngle += -PI`) : **ce n'est pas une corruption d'OCR, c'est probablement une bévue du brevet**, donc à ne pas reprendre. `m_len` est lisible mais **non défini** dans le texte (lecture plausible, confiance faible : une longueur de référence qui met l'amplitude de l'aléa à l'échelle). Le cas parallèle de la table 1 est mal parenthésé à l'impression.
- **US5438520A** : le texte imprime « Ippon Denpa » (la fiche écrivait Nippon) ; Raytheon n'y apparaît pas. Quatre priorités JP lues sur l'image. Retour d'au moins 4 points, de préférence 6 puis 10 (description). La revendication 1 inclut la broderie effective. **Aucune distance de décalage D4/D6** n'est donnée. Tables 1 et 2 cohérentes avec la fiche.
- **JP3769602B2** : enregistrement le 17/02/2006 (bulletin du 26/04/2006) ; un recours contre rejet (不服2003-7905, 07/05/2003) était omis. La revendication unique va de D1 à D4 et D5 puis D7 ; elle ne mentionne ni bâti, ni arrêt, ni E-stitch. L'arrêt D6 vient de D4/D5 avec direction et nombre de points saisis au clavier. Aucun pas, largeur ni nombre de sections. Le rendu image de la description est blanc (polices CJK non embarquées) ; le texte a été lu dans la couche texte du PDF.
