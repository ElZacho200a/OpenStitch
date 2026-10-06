# Guide utilisateur détaillé

Public : utilisateur débutant et avancé. Ce chapitre documente chaque menu,
outil et raccourci **réellement présents** dans `apps/desktop/`.

## Disposition générale

L'interface place le **canevas au centre** et l'entoure de zones fonctionnelles :

```
Menus
Barre d'outils principale
Barre contextuelle (suit la sélection)
[Palette d'outils] [ CANEVAS ] [ Inspecteur ]
[Document / Ordre] [ Workflow ]
Barre de simulation (zone réservée)
Barre d'état
```

Tous les panneaux sont des **docks** redimensionnables, déplaçables et masquables
(**Ctrl+Shift+P** = mode canevas). Leur disposition, la géométrie de la fenêtre,
le **thème** et la **densité** sont mémorisés d'une session à l'autre (préférences
d'interface, distinctes du projet `.osp`).

## État d'accueil

Tant qu'aucun document n'est ouvert, le centre du canevas propose **Ouvrir une
image**, **Ouvrir un projet** et **Importer un DST**, avec une courte explication.

## Le canevas

La zone centrale est un canevas dont l'unité est le **millimètre** (origine au
centre, Y vers le haut). Des **règles** graduées en mm bordent le haut et la
gauche ; une **grille** adaptative et le **cadre** de broderie (rectangle rouge,
défini par l'utilisateur, cf. *Taille du cadre*) sont dessinés. La position du
curseur en mm apparaît dans la barre d'état.

Le **mode d'interaction** vient de la palette d'outils (à gauche) :

- **Sélection** (`V`) : sélectionner une région/un objet ; le glisser déplace la vue.
- **Déplacer la vue** (`H`) : déplacement pur (le clic ne sélectionne pas).
- **Rectangle / Recadrage** (`M`) : sélection rectangulaire pour recadrer l'image.
- **Zoom** : molette (ancrée sous le curseur) ou barre d'outils / menu Affichage.
- **Échap** : revient à la Sélection et annule le mode fusion en cours.

La sélection d'un objet est tracée en **double contraste** (halo clair + trait
d'accent), lisible sur tout fond. Le rendu est organisé en deux couches
(image/vecteurs/régions d'une part, points d'autre part) pour rester fluide sur
de gros motifs.

## Souris et clavier

Le comportement de la souris et du clavier dans le canevas suit une **table
unique** (`apps/desktop/interaction_map.cpp`) : c'est elle qui pilote le
comportement du canevas, la ligne d'indications de la barre d'état et le
tableau ci-dessous. Principes : la **molette** zoome sous le curseur (**Ctrl + molette** aussi ;
c'est ainsi que Windows livre le pincement d'un pavé tactile), le **clic
molette** ou **Espace** + glisser déplace la vue, **Maj** ajoute à la sélection,
**Ctrl** ajoute ou retire, un **appui long** ou **Alt + clic** ouvre
« Sélectionner dessous ». La correspondance est exacte : Maj + clic n'est pas un
clic simple, et le panoramique reste disponible pendant un outil de dessin. Sous
Windows, un pavé tactile se distingue mal d'une molette : le préréglage de
navigation « Pavé tactile » (Affichage ▸ Navigation) y met en avant
Espace + glisser et Ctrl + molette.

Le tableau est **généré** par `openstitch_gesture_table --markdown` ; ne pas le
modifier à la main (le test `docs_gestures_in_sync` échoue si la table du code
et ce bloc divergent). Pour le régénérer, recopier la sortie de la commande
entre les deux marqueurs.

<!-- GESTURES:BEGIN -->
| Réf. | Contexte | Geste | Action |
|---|---|---|---|
| G1 | Partout | Molette | Zoom au curseur |
| G2 | Partout | Ctrl + Maj + clic molette + glisser | Zoom continu |
| G3 | Partout | Clic molette + glisser | Panoramique |
| G4 | Partout | Double-clic molette | Cadrer le design |
| G5 | Partout | Espace + glisser | Panoramique (sans clic molette) |
| G6 | Partout | Maj + molette | Défilement horizontal |
| G7 | Partout | Alt + molette | Défilement vertical |
| G8 | Partout | Défilement à deux doigts | Panoramique |
| G9 | Partout | Pincement | Zoom au curseur |
| G10 | Sélection | Clic droit | Menu contextuel |
| G10b | Déplacement | Clic droit | Menu contextuel |
| G10c | Édition de nœuds | Clic droit | Menu contextuel |
| G10d | Édition de points | Clic droit | Menu contextuel |
| G11 | Partout | Suppr | Supprimer la sélection |
| G12 | Partout | Échap | Annuler l'outil en cours |
| G13 | Partout | Ctrl + molette | Zoom au curseur |
| G14 | Partout | F | Ajuster au canevas |
| G15 | Partout | Flèches | Déplacer l'objet de 0,1 mm |
| G15b | Partout | Maj + Flèches | Déplacer l'objet de 1 mm |
| P1 | Déplacer la vue | Glisser | Panoramique |
| S1 | Sélection | Clic | Sélectionner (le vide désélectionne) |
| S2 | Sélection | Maj + clic | Ajouter à la sélection |
| S3 | Sélection | Ctrl + clic | Basculer dans la sélection |
| S3b | Sélection | Ctrl + Maj + clic | Basculer dans la sélection |
| S4 | Sélection | Appui long | Sélectionner dessous |
| S5 | Sélection | Alt + clic | Sélectionner dessous |
| S6 | Sélection | Glisser | Sélection par rectangle (vers la droite : englobe, vers la gauche : croise) |
| S7 | Sélection | Maj + glisser | Rectangle : ajouter à la sélection |
| S8 | Sélection | Ctrl + glisser | Rectangle : basculer dans la sélection |
| S8b | Sélection | Ctrl + Maj + glisser | Rectangle : basculer dans la sélection |
| S10 | Sélection | Double-clic | Entrer en édition de l'objet |
| S11 | Sélection | Survol | Surbrillance de pré-sélection |
| M1 | Déplacement | Glisser | Déplacer l'objet |
| M2 | Déplacement | Maj + glisser | Verrouiller l'axe |
| M3 | Déplacement | Ctrl + glisser | Suspendre l'accroche |
| M4 | Déplacement | Alt + glisser | Dupliquer en déplaçant |
| D1 | Dessin (clics) | Clic | Ajouter un point |
| D2 | Dessin (clics) | Double-clic | Terminer le tracé |
| D2b | Dessin (clics) | Entrée | Terminer le tracé |
| D3 | Dessin (cadre) | Maj | Contraindre la forme (carré, cercle) |
| D4 | Dessin (cadre) | Alt | Dessiner depuis le centre |
| D5 | Dessin (clics) | Ctrl | Suspendre l'accroche |
| D6 | Dessin (clics) | Retour arrière | Retirer le dernier point |
| D7 | Dessin (clics) | Échap | Annuler l'outil en cours |
| N1 | Édition de nœuds | Glisser | Déplacer le nœud |
| N2 | Édition de nœuds | Maj + glisser | Verrouiller l'axe |
| N2b | Édition de nœuds | Ctrl + glisser | Suspendre l'accroche |
| N4 | Édition de nœuds | Suppr | Supprimer les nœuds sélectionnés |
<!-- GESTURES:END -->

## Menu Fichier

| Action | Raccourci | Effet |
|---|---|---|
| Nouveau projet | Ctrl+N | Repart d'un document vierge (voir ci-dessous) |
| Ouvrir une image… | Ctrl+O | Charge PNG/JPEG/BMP/TIFF puis demande la taille physique |
| Enregistrer le projet | Ctrl+S | Réécrit le `.osp` courant (demande où enregistrer la première fois) |
| Enregistrer le projet sous… | Ctrl+Maj+S | Écrit le document dans un nouveau `.osp`, qui devient le fichier courant |
| Ouvrir un projet… | — | Recharge un `.osp` |
| Exporter en DST… | — | Écrit un fichier `.dst` (points uniquement) |
| Importer un DST… | — | Relit un `.dst` comme séquence de points |
| Quitter | Ctrl+Q | Ferme l'application |

**Nouveau projet** : si le document courant a été modifié, une garde propose
d'enregistrer, d'abandonner les modifications ou d'annuler — la même que celle
de la fermeture de la fenêtre. Le document repart à vide (aucune image, aucun
objet, aucune région), la pile Annuler/Rétablir est vidée, et **tout l'état
d'édition de la fenêtre est relâché** : sélections, mode d'édition des points,
modes satin (barreaux et rails), guides de direction, tracé en cours
(polygone, main levée, colonne satin, Bézier) et simulation. L'outil revient à
Sélection. Cette réinitialisation est partagée par tous les chemins qui
remplacent le document (ouvrir une image, un SVG, un projet, importer un DST).

**Import** : le dialogue affiche un aperçu, les dimensions en pixels, la
résolution **mm/pixel** en direct, la taille du cadre, et **alerte si l'image
dépasse le cadre**. **Export DST** : un **résumé** (dimensions, points, sauts,
coupes, changements de couleur, fil estimé, cadre, dépassement éventuel) est
présenté avant écriture, avec un rappel que le DST ne conserve pas les objets.

**Enregistrer** : le document retient le fichier `.osp` auquel il est rattaché
(celui qu'on vient d'ouvrir ou d'enregistrer). Ctrl+S réécrit ce fichier
directement ; le sélecteur n'apparaît que pour un document encore sans fichier,
ou via **Enregistrer sous…** (Ctrl+Maj+S), qui adopte ensuite le nouveau
fichier. Tout changement de document (Nouveau, ouvrir une image/un SVG/un
projet, importer un DST) oublie ce rattachement, pour qu'un Ctrl+S ne puisse
jamais écraser le projet précédent. L'écriture est **atomique** (fichier
temporaire puis renommage) : une coupure en cours d'enregistrement ne détruit
pas le `.osp` existant.

Le titre de la fenêtre affiche le nom du fichier courant (ou « Sans titre ») et
un indicateur **modifié** (`*`) ; quitter avec des modifications non
enregistrées propose **Enregistrer / Ignorer / Annuler**.

## Menu Édition

| Action | Raccourci | Effet |
|---|---|---|
| Annuler | Ctrl+Z | Défait la dernière opération (le libellé nomme l'action) |
| Rétablir | Ctrl+Y | Refait l'opération annulée |

L'annulation couvre les opérations d'image, la segmentation (segmenter, fusionner,
supprimer, recolorer), la création et le déplacement de nœuds vectoriels, la
création d'objets de broderie, le **changement de type**, l'**orientation** d'un
remplissage, la conversion satin→tatami et le réordonnancement.

## Menu Image

Toutes ces opérations sont non destructives (pile de transformations sur
l'original) :

- Niveaux de gris ;
- Luminosité/contraste… (aperçu en direct) ;
- Débruitage léger / moyen (médian) ;
- Quantifier les couleurs… (k-means, nombre de couleurs 2–64) ;
- Symétrie horizontale / verticale ;
- Rotation 90° horaire / antihoraire ;
- Recadrer (sélection) — dessinez un rectangle sur le canevas.

## Menu Segmentation

- Segmenter l'image… (nombre de couleurs, taille min de région) ;
- Afficher la carte des régions (bascule) ;
- Fusionner avec… (puis clic sur la région cible) ;
- Supprimer la région sélectionnée (Suppr) — la région redevient du fond ;
- Recolorer la région sélectionnée… ;
- Convertir la région en objet vectoriel.

Sélection : cliquez une région ; ses statistiques (pixels, mm², RGB) s'affichent.

## Menu Broderie

- Numérisation automatique — crée des objets pour toutes les régions. Les zones
  remplissables deviennent des **tatami** (le satin automatique naïf, qui
  débordait, est désactivé par défaut) ;
- Créer un objet de point de contour… (longueur, type simple/double/triple) ;
- Créer un remplissage tatami… (espacement, longueur, angle) ;
- Créer une colonne satin… (densité, compensation, sous-couche centrale) ;
- **Orientation du remplissage…** — change l'angle des fils du tatami sélectionné
  (aussi réglable à la souris, voir *poignée de rotation* plus bas) ;
- **Convertir les satins auto en tatami** — répare un projet dont les satins
  automatiques débordent ;
- Statistiques… (points, sauts, coupes, changements de couleur, dimensions,
  longueur de fil).

Avertissement : si une colonne satin dépasse la largeur recommandée, un dialogue
propose de continuer ou de préférer un remplissage tatami.

## Menu Affichage

Le sous-menu **Calques** regroupe des interrupteurs indépendants :

| Calque | Effet |
|---|---|
| Image | Affiche/masque l'image de travail |
| Carte des régions | Affiche/masque la segmentation |
| Vecteurs | Affiche/masque les objets vectoriels |
| Broderie (points) | Affiche/masque les points générés |

| Action | Raccourci | Effet |
|---|---|---|
| Zoom avant | Ctrl++ | Agrandit |
| Zoom arrière | Ctrl+- | Réduit |
| Ajuster au canevas | Ctrl+0 ou F | Cadre la vue sur le canevas |
| Taille du cadre… | — | Définit la zone physique de broderie (voir ci-dessous) |
| Thème | — | Clair / Sombre |
| Densité | — | Confortable / Compact |
| Masquer les panneaux | Ctrl+Shift+P | Mode canevas (masque puis restaure les docks) |

**Taille du cadre** : largeur/hauteur en mm (10–500). Le cadre est une donnée du
projet (persistée dans le `.osp`, annulable) ; l'analyse « hors cadre » et le
rectangle rouge s'y réfèrent. Accessible aussi via le bouton **« Cadre : W×H
mm… »** de la barre contextuelle quand rien n'est sélectionné.

Les points cousus sont tracés **dans la couleur de fil de chaque objet** (un fil
très clair est légèrement assombri pour rester visible) ; les **sauts** en
pointillés orange ; des pastilles marquent les pénétrations (masquées au-delà de
4000 points pour la fluidité, et pendant la simulation).

## Menu contextuel (clic droit)

Un **clic droit** sur une forme ouvre un menu :

- **Type de points** ▸ Contour cousu / Remplissage tatami / Colonne satin (le
  type courant est coché) — bascule instantanée et annulable ;
- **Orientation du remplissage…** (si la forme est un tatami) ;
- **Calques** — les mêmes interrupteurs que le menu Affichage.

## Poignée de rotation (orientation à la souris)

Quand un remplissage tatami est sélectionné, un **axe bleu avec une poignée**
apparaît en son centre. Faites glisser la poignée pour tourner l'orientation des
fils ; l'angle est appliqué au relâchement (annulable). C'est l'équivalent
visuel de *Orientation du remplissage…*.

## Panneau Filtres d'affichage

Dock (à droite, dès qu'il existe des objets) pour **isoler** ce qu'on regarde :

- **Types de points** : cases Contour / Tatami / Satin ;
- **Taille min. des zones** : masque les zones dont l'aire source est sous le
  seuil (mm²) — utile pour cacher les micro-régions ;
- **Couleurs de fil** : une case par couleur distincte, pour n'afficher que
  certains fils.

Ces filtres n'affectent que **l'affichage** (pas l'export ni les points générés).

## Barre d'outils principale

Actions fréquentes, icônes monochromes avec infobulle : ouvrir image/projet,
enregistrer, annuler/rétablir, zoom −/ajuster/+, analyser, aperçu des points,
exporter DST. Les actions indisponibles dans le contexte courant sont désactivées.

## Barre d'outils contextuelle

Sous la barre principale, son contenu **suit la sélection** :

- **objet de broderie** : bascule rapide du type (Contour/Tatami/Satin) +
  *Orientation…* si tatami ;
- **objet vectoriel** : boutons de création rapide (Contour/Tatami/Satin) ;
- **région** : aire + *Fusionner* / *Supprimer* / *Vectoriser* ;
- **aucune sélection** : résumé du motif + bouton *Cadre…*.

## Inspecteur de propriétés

Dock (droite) affichant l'élément sélectionné. Pour un **objet de broderie**, il
expose ses **paramètres de couture éditables après création** (contour :
longueur/min/passages ; tatami : espacement/longueur/angle/retrait/décalage ;
satin : densité/compensation/sous-couche). Chaque changement passe par une
commande **annulable** et régénère les points. Une région ou un objet vectoriel
y affiche ses informations en lecture seule.

## Panneau Document

Dock (gauche) à deux onglets, **Objets** et **Régions**, tabifié avec l'**Ordre
de couture**. Sélectionner une ligne met l'élément en évidence au canevas et dans
l'inspecteur ; une sélection au canevas surligne la ligne correspondante
(synchronisation bidirectionnelle).

## Indicateur de workflow

Bandeau (sous Document) listant les étapes **Image → Régions → Vecteurs →
Broderie → Vérification → Export**. L'état de chaque étape (à faire / disponible /
en cours / terminé / attention) est déduit du document et rendu par pastille +
libellé + mot d'état. Un clic rappelle en barre d'état l'action à faire.

## Menu Analyse et panneau

**Analyser le motif** (F5) remplit le panneau *Analyse* (dock) avec les problèmes
détectés, triés par gravité. Un double-clic sur un problème centre la vue sur sa
localisation.

## Panneau Ordre de couture

Onglet du panneau Document listant les objets de broderie dans l'ordre. Vous
pouvez monter/descendre un objet, le verrouiller (il ne bougera plus lors de
l'optimisation), et appliquer une stratégie (ordre du document, par couleur, par
proximité, couleur puis proximité). Un libellé affiche le coût estimé.

## Barre de simulation

Boutons de lecture/pause et un curseur qui révèle la couture jusqu'à un index de
point, avec un repère d'aiguille. La barre occupe une **zone réservée** (toujours
visible, contrôles grisés tant qu'aucune séquence n'existe) pour ne pas faire
sauter la mise en page.

## Erreurs et messages

Les opérations impossibles (recadrage hors image, satin non constructible,
export d'une séquence vide…) affichent un message clair et **n'appliquent rien**.
Les erreurs connues et les entrées invalides **couvertes par les tests**
renvoient une erreur structurée au lieu de provoquer un arrêt du programme.
Aucun plantage n'a été observé dans le corpus de tests actuel — ce qui ne
constitue pas une garantie absolue en version 0.1.0.

## Menu Aide

- **Raccourcis clavier…** : la liste ci-dessous.
- **À propos** : nom, version, licence.

Le nouveau menu Aide (Guide de prise en main, Gestes souris et clavier (F1),
À propos) est prévu par la tâche T4 du lot L5 : il n'est pas encore dans
l'application.

## Raccourcis

| Raccourci | Action |
|---|---|
| Ctrl+N | Nouveau projet (garde des modifications non enregistrées) |
| Ctrl+O / Ctrl+S | Ouvrir une image / Enregistrer le projet (sans redemander le chemin) |
| Ctrl+Maj+S | Enregistrer le projet sous… |
| Ctrl+Z / Ctrl+Y | Annuler / Rétablir |
| Suppr | Supprimer la région sélectionnée |
| Ctrl++ / Ctrl+- / Ctrl+0 | Zoom avant / arrière / ajuster |
| F | Ajuster au canevas |
| F5 | Analyser le motif |
| V / H / M | Outils : Sélection / Déplacer la vue / Rectangle |
| Échap | Revenir à la Sélection (annule la fusion) |
| Ctrl+Shift+P | Masquer / afficher les panneaux |
| Ctrl+Q | Quitter |

Les raccourcis standard proviennent des séquences Qt ; `Ctrl+0`, `F5`, `F`,
`V/H/M`, `Ctrl+Shift+P` sont définis explicitement, sans conflit avec la
navigation clavier (Tab reste réservé au parcours des contrôles).

## Implémentation associée

- `apps/desktop/main_window.cpp/.hpp` — menus, barres, docks, sélection, cadre.
- `apps/desktop/app_theme.*`, `design_tokens.*` — thème et tokens.
- `apps/desktop/properties_panel.*`, `document_panel.*`, `workflow_panel.*`,
  `empty_state_widget.*` — panneaux.
- `apps/desktop/tools.hpp`, `ui_icons.*` — modes d'interaction et icônes.
- `apps/desktop/canvas_view.cpp` — zoom, déplacement, grille, cadre, rendu points.
- `apps/desktop/ruler.cpp` — règles en mm.
- `apps/desktop/import_dialog.cpp`, `brightness_dialog.cpp` — dialogues.
