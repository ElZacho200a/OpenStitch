# Phase 0 — Analyse du problème et définition du MVP

Projet : **OpenStitch Studio** (nom temporaire, remplaçable — voir ADR-001)

## 1. Analyse du problème

### 1.1 Ce que fait réellement un logiciel de numérisation broderie

La numérisation broderie n'est pas une conversion d'image : c'est la **planification d'un processus physique**. Le fil tire sur le tissu, le tissu se déforme, l'aiguille perfore. Un logiciel de numérisation transforme une intention visuelle (image) en un **programme machine** (séquence de pénétrations d'aiguille et de commandes) qui, exécuté sur un textile réel, produira un résultat visuellement proche de l'intention.

La chaîne complète se décompose en étapes aux natures très différentes :

| Étape | Nature du problème | Domaine technique |
|---|---|---|
| Import / prétraitement image | Traitement d'image classique | Vision par ordinateur |
| Quantification / segmentation | Clustering couleur, régions connexes | Vision + colorimétrie perceptuelle |
| Vectorisation | Extraction/simplification de contours | Géométrie computationnelle |
| Objets de broderie | Modélisation métier | Modèle de document |
| Génération de points | Algorithmes de remplissage/parcours | Géométrie + heuristiques textiles |
| Compensation | Physique textile approximée | Heuristiques paramétrables |
| Ordre de couture | Optimisation sous contraintes | Graphes, TSP-like |
| Analyse | Validation par règles | Moteur de règles |
| Export DST | Encodage binaire contraint | Format de fichier |

### 1.2 Difficultés structurantes identifiées

1. **La séparation géométrie éditable / points générés** est le cœur du modèle. Un DST ne contient que des points ; un objet de broderie contient l'intention (géométrie + paramètres). Perdre cette séparation = perdre l'éditabilité. Tout le modèle de document en découle.
2. **La robustesse géométrique** est le risque technique n°1. Les polygones issus de vectorisation sont sales (auto-intersections, micro-arêtes, trous dégénérés). Les opérations d'offset et de découpe doivent être robustes → coordonnées entières et Clipper2 plutôt que du flottant naïf.
3. **Le satin est l'algorithme différenciant**. Le tatami (hachures parallèles) est bien documenté ; le satin (colonne à deux rails, sections transversales, gestion des virages) demande une modélisation soignée. C'est là que les logiciels amateurs échouent.
4. **Les unités** doivent être physiques dès le départ. Une broderie est en millimètres, pas en pixels. Introduire les mm après coup est une refonte garantie.
5. **L'ordre de couture et les compensations** sont des heuristiques, pas des vérités : elles doivent être isolées, remplaçables, et jamais appliquées silencieusement.

### 1.3 Références légitimes (formats documentés, projets open source)

- **Ink/Stitch** (GPLv3, Python) : preuve qu'un numériseur open source complet est faisable ; source d'inspiration *conceptuelle* (pas de copie de code — Python→C++ de toute façon, et licence à respecter).
- **libembroidery** (Embroidermodder, zlib) : documentation de fait des formats machine, dont DST.
- **EduTech Wiki / documentation communautaire du format DST** : le format Tajima est documenté publiquement depuis des décennies.
- Publications classiques : extraction de contours (Suzuki-Abe, implémenté dans OpenCV), simplification (Douglas-Peucker, Visvalingam), quantification (median cut, k-means), remplissage par balayage (scanline), offset de polygones (Clipper).

Aucune ingénierie inverse de logiciel commercial n'est nécessaire ni autorisée dans ce projet.

## 2. Définition réaliste du MVP

### 2.1 Périmètre strict (conforme §31 du cahier des charges)

Le MVP est : **« une image simple devient un fichier DST brodable, avec contrôle manuel à chaque étape »**.

Inclus :

- Import PNG/JPEG ; affichage sur canevas avec taille physique en mm ; zoom/pan ; règles.
- Quantification simple (median cut ou k-means, en CIELAB) avec choix du nombre de couleurs.
- Régions connexes sélectionnables ; fusion/suppression/recoloration ; nettoyage des petites zones.
- Extraction de contours (extérieurs + trous), simplification, édition basique de nœuds.
- Deux générateurs de points : **point droit / triple** (contours) et **tatami simple** (régions).
- Changements de couleur ; ordre de couture manuel (liste réordonnables).
- Affichage des points générés ; statistiques (nb points, sauts, couleurs, dimensions, longueur de fil estimée).
- Analyse minimale : points trop courts/longs, sauts trop longs, objet hors cadre, objet vide.
- Export DST + import DST + test aller-retour.
- Sauvegarde/chargement du projet (format interne documenté) ; undo/redo.

### 2.2 Explicitement hors MVP

Satin (Phase 8), sous-couches, compensation de tirage, ordre automatique, simulation animée, remplissages courbes/motifs, PES/JEF/EXP, profils machine avancés, aperçu 3D, autonumérisation. Le satin est la première extension majeure post-MVP, l'architecture doit le prévoir (le modèle d'objet et le registre de générateurs sont conçus pour l'accueillir sans refonte).

### 2.3 Critère de succès du MVP

Un utilisateur prend un logo bicolore simple en PNG, le réduit à 2 couleurs, convertit les régions en tatami + contours en point triple, exporte un DST, le recharge dans le logiciel (et idéalement dans un visualiseur tiers), et le fichier est cohérent : mêmes limites, même nombre de points, séquence de couleurs correcte, aucun déplacement > 12,1 mm non subdivisé.
