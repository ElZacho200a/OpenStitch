# Parcours utilisateur : image → broderie

Analyse du chemin complet, de l'ouverture à l'export, tel que le code le propose aujourd'hui
(`main` à `c4dba99`, **lecture du code, interface non pilotée**). Les identifiants renvoient au
[rapport d'audit](audit-ux-ui-2026-10-18.md) et aux entrées de `docs/roadmap-parite-hatch.md`
§20. Question directrice : *comment un utilisateur expérimenté ferait-il cela plus vite, sans
rendre la découverte plus difficile pour un débutant ?*

Compte des clics : actions utilisateur minimales dans le cas courant, hors saisie de valeurs.

## 1. Vue d'ensemble

| # | Étape | Clics aujourd'hui | Cible | Friction principale | Tâches |
|---|---|---|---|---|---|
| 1 | Créer / ouvrir | ≈ 4 (accueil → fichier → OK) ; glisser-déposer : 1 + OK | Ctrl+O universel | Ctrl+O n'ouvre que les images ; accueil sans « Dessiner/Texte » (PAR-01, 09) | HP-UX-017, HP-UX-012 |
| 2 | Importer l'image | OK, puis segmentation à part (F6) | case « segmenter ensuite » mémorisée | deux temps pour le cas courant | HP-UX-012 |
| 3 | Taille physique et cadre | 0 si 96 dpi tient ; sinon ≈ 6 (annuler, menu, dialogue, réimporter) | 1 (cadre dans l'import) | le dialogue constate le dépassement mais ne le corrige pas (PAR-08, 20) | HP-UX-033 |
| 4 | Prétraitement, segmentation | 2 par opération ; aperçu direct seulement pour la segmentation | aperçu pour toute retouche | 6 opérations sur 8 sans aperçu, modale « supprime la segmentation » (PAR-19) | HP-VIEW-012 |
| 5 | Régions : sélection, fusion | 1 touche (J/A/C/N) | inchangé | **point fort** | — |
| 6 | Vectorisation | F7 + OK (2) | F7 (1) | modale « Détail » pour un curseur déjà mémorisé (PAR-07) | HP-UX-021 |
| 7 | Type de point | forme neuve : 2 clics + dialogue ; multi : 3 ; directionnel neuf : 4 et plus | 1 clic, partout | trois chemins pour « Satin » (PAR-02 à 04) | HP-UX-002, HP-UX-028 |
| 8 | Paramètres (densité, angle) | clic + saisie + Entrée dans 13 à 25 lignes | essentiels en tête, ↺ par champ | inspecteur plat, pas de défaut (BRO-11, 12, PAR-17) | HP-UX-003 |
| 9 | Numérisation automatique | F8 + question + dialogue de ~25 réglages (≈ 3 modales) | préréglage + 1 dialogue | expert : 3 clics par essai ; débutant : champs incompréhensibles (PAR-06) | HP-UX-030 |
| 10 | Objets, ordre, couleurs, fils | deux listes ; ordre = 1 clic par cran | glisser-déposer ; fils visibles | Document et Ordre de couture concurrents (PAR-14) | HP-UX-037 |
| 11 | Aperçu, simulation, rendu | lecture : 1 ; rendu réaliste : ≥ 3 menus, **coupé après simulation** | bascule de vue ; sortie de simulation | PAR-10, 11, BRO-23 | HP-UX-022, HP-UX-036 |
| 12 | Brodabilité | F5 + F9 + 3 menus | un panneau Qualité | qualité dispersée (PAR-13) | HP-UX-031 |
| 13 | Sauvegarde `.osp` | Ctrl+S (1) ; 3 la première fois | inchangé | heure affichée = heure du rafraîchissement (UX-32) | HP-UX-039 |
| 14 | Export | Ctrl+E + « Choisir » + fichier ≈ 4 | Ctrl+Maj+E rapide ≈ 2 | décision implicite, « Voir les problèmes » ferme le dialogue (PAR-12) | HP-UX-032 |

## 2. Détail par étape

### 1-3. Ouvrir, importer, dimensionner

- **Acquis** : glisser-déposer routé par extension (`main_window_ux.cpp:70-97`), garde des
  remplacements, récupération après arrêt anormal.
- **Frictions** : Ctrl+O et le filtre du dialogue ignorent `.osp` alors que `openPath` sait tout
  router (PAR-01). Le dialogue d'import dit « vous pourrez agrandir le cadre » mais l'utilisateur
  doit quitter le dialogue pour le faire (PAR-08).
- **Expert** : Ctrl+O universel, derniers réglages d'import mémorisés (déjà le cas via
  `ui_memory`). **Débutant** : l'accueil propose un guide de 2 minutes et les trois façons de
  commencer (image, dessin, texte).

### 4-6. Préparer, segmenter, vectoriser

- **Acquis** : segmentation **non modale avec aperçu calculé hors fil**
  (`main_window_segment_preview.cpp:47-77`) ; actions de régions à une touche.
- **Frictions** : six des huit opérations d'image s'appliquent au clic sans aperçu (seules Luminosité/Contraste et Quantifier en ont un) ; F7 ouvre toujours une
  modale pour un seul curseur.
- **Règle proposée** : *un geste répété n'ouvre pas de dialogue ; la variante qui règle est sur
  Maj+*. Appliquée à F7 (HP-UX-021), à la création d'objets (HP-UX-002) et à l'export
  (HP-UX-032).

### 7-8. Type de point et paramètres

- **Frictions** : une forme sans couture n'a aucun choix de type dans l'inspecteur (PAR-02) ; le
  menu contextuel ne propose pas non plus le type (PAR-04) ; la création passe par un dialogue
  modal aux valeurs en dur (BRO-19).
- **Cible** : sélectionner une forme → quatre boutons de type identiques à ceux d'une couture
  existante → un clic crée l'objet avec le dernier réglage, l'inspecteur règle ensuite en direct.
  Les dialogues de création disparaissent (décision du 2026-10-18) ; l'entrée « Créer avec
  réglages… » reste pour qui veut les valeurs avant création.
- **Inspecteur** : trois réglages visibles d'emblée (espacement, angle, compensation), sections
  repliées pour la sous-couche et l'avancé, ↺ « défaut » par champ avec info-bulle plage/défaut.

### 9. Numérisation automatique

Le cas courant (« numérise-moi ce logo ») coûte deux modales et expose ~25 réglages d'expert. La
cible : **trois préréglages** (« Logo / aplats », « Dessin au trait », « Photo / pointillé »),
une section « Avancé » repliée, la question Ajouter/Remplacer intégrée, et un résumé chiffré en fin
d'opération (points, durée, fils, problèmes détectés, bouton « Voir »). Aucune modification du
moteur : seul l'écran de paramètres change.

### 10. Objets, ordre, couleurs

Le panneau Document et l'onglet « Ordre de couture » listent les mêmes objets. Fusion proposée :
une seule liste à sélection étendue, vecteurs non cousus inclus (UX-13), ordre par
glisser-déposer, colonne de type en icône. Le choix de la couleur d'un nouvel objet reste
imposé (80,120,200) : une « couleur courante » et la pipette au moment du tracé
(HP-OBJ-018) suppriment un clic par objet.

### 11. Aperçu, simulation, rendu réaliste

Constat important (PAR-10) : après avoir lu une simulation, le rendu réaliste **ne s'affiche plus**
tant que le document n'est pas régénéré, et rien n'indique pourquoi. Un bouton « Quitter la
simulation » et une sortie automatique en fin de lecture règlent le cas ; la **bascule de vue à
quatre états** (Dessin / Points / Réaliste / Simulation) rend les chemins symétriques et dit
« Vecteurs masqués » quand un clic tombe sur une couche cachée (BRO-23).

### 12. Brodabilité

F5 (analyse), F9 (statistiques), puis trois menus (fidélité, épaisseurs, diagnostic) : un panneau
« Qualité » à onglets, ouvert automatiquement après F8 et avant l'export, avec « corriger tout ce
qui est sûr » annulable en un pas. Les corrections s'appuient sur les règles existantes de
`stitch_analysis` (aucune règle nouvelle).

### 13-14. Sauvegarder, exporter

- **Export** : Ctrl+E ouvre un dialogue de ~15 contrôles, puis « Choisir le fichier… », puis la
  boîte système. Pour qui exporte souvent : **Ctrl+Maj+E = refaire avec les derniers réglages**
  vers le dernier dossier. Pour le débutant : « Enregistrer le projet d'abord » et « Voir les
  problèmes » (sans fermer l'export) dès qu'il y a un avertissement, bouton par défaut non
  destructif quand des risques existent (UX-10).
- **Sauvegarde** : Ctrl+S direct. L'heure affichée doit être celle de la sauvegarde, pas celle
  du dernier rafraîchissement (UX-32).

## 3. Trois parcours types, avant/après

| Parcours | Aujourd'hui | Après lots A à C |
|---|---|---|
| **Logo → DST, débutant** | accueil, import + OK, F6 + Segmenter, F8 + 2 modales, F5, F9, Ctrl+E + Choisir | accueil, import (cadre inclus), F8 avec préréglage, panneau Qualité s'ouvre seul, Ctrl+E |
| **Retouche d'un `.osp`, expert** | menu Fichier ▸ Ouvrir un projet (pas de raccourci), sélection par Alt+clic + menu, 13 champs à parcourir, Ctrl+E + 3 écrans | Ctrl+O, re-clic pour cycler, 3 champs d'emblée, Ctrl+Maj+E |
| **SVG → broderie** | import, « Créer un tatami… » (modale) pour chaque forme, couleur imposée | import, sélection de toutes les formes, un clic « Tatami » (annulable en un pas) |

## 4. Principes retenus

1. **Un geste répété n'ouvre pas de dialogue** (variante qui règle : Maj+ ou « avec réglages »).
2. **Un seul chemin par intention** : bouton, touche et entrée de menu pointent sur la même action.
3. **Aperçu avant engagement** : tout réglage s'applique en direct (hors fil UI) ; les modales
   sont réservées aux choix coûteux ou irréversibles.
4. **Échap remonte d'un niveau**, il ne « ferme tout ».
5. **Le danger se dit avant** (retouches invalidées, export avec risques), pas après.
6. **Découverte progressive** : essentiels visibles, avancé replié et mémorisé, expérimental
   derrière un menu « Avancé ». Pas d'assistant imposé : les experts gardent les raccourcis.
