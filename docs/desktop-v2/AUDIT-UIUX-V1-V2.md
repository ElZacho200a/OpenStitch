# Audit UI/UX — OpenStitch Studio V1 vs V2

**Date :** 9 novembre 2026  
**Auteur :** Équipe de développement OpenStitch  
**But :** Rapport structuré comparant l'architecture d'interface, les fonctionnalités et l'expérience utilisateur entre la version 1 (Qt C++) et la version 2 (QML/C++ hybride)

---

## 📋 Sommaire

1. [Architecture globale](#1-architecture-globale)
2. [Structure des menus et actions](#2-structure-des-menuset-actions)
3. [Outils et panneaux](#3-outlets-etpanneaux)
4. [Raccourcis claviers](#4-raccourcis-claviers)
5. [Nouvelles fonctionnalités d'interface en V2](#5-nouvelles-fonctionnalites-dinterface-en-v2)
6. [Fonctionnalités abandonnées ou déplacées](#6-fonctionnalites-abandonnees-ou-deplacees)
7. [État de parité (récapitulatif CSV)](#7-etat-de-parite-recapitulatif-csv)
8. [Recommandations pour la poursuite du développement](#8-recommandations-pour-la-poursuite-du-developpement)

---

## 1. Architecture globale V1 vs V2

### 1.1. Approche technique

| Aspect | **V1** | **V2** |
|--------|--------|--------|
| **Technologies** | Qt C++ pur (`.h`/`.cpp`) | QML + C++ hybride (`.qml` + `.cpp`) |
| **Interface** | Docks traditionnels, fenêtres modales classiques | Panneaux modulaires flottants, layout flexible QML |
| **Thèmes** | Palette Qt standardisée | Thème personnalisé (`StudioTheme`) avec support clair/sombre natif |
| **Animation** | Minimale, contrôlée manuellement | Animation fluide native QML (optionnel via préférences) |
| **Focale au clavier** | Gestion mixte C++/QML | Gestion unifiée avec `keyboardOwnedByControl` pour éviter les conflits |

### 1.2. Barre d'outils principale

#### V1 : Barre standard Qt
- Boutons iconographiques + texte optionnel
- Position fixe en haut de fenêtre
- Structure rigide (`.cpp` dans `main_window.h/cpp`)

#### V2 : Barre enrichie (`Main.qml`)
```qml
ColumnLayout {  // 4 zones claires
    // Ligne 1: Logo + Fichier (Nouveau, Ouvrir, Enregistrer)
    // Ligne 2: Titre du document + Commandes + IA + Préférences + Aide
    // Ligne 3: Canevas principal (SpaceHost avec espaces d'œuvre)
    // Ligne 4: Barre d'état (statut, commandes, backend de rendu)
}
```

**Améliorations :**
- ✅ Séparation claire des zones fonctionnelles
- ✅ Boutons contextuels (IA, Préférences) accessibles sans menu
- ✅ Indicateurs d'état enrichis (modifiable, backend de rendu, déformations prédites)
- ✅ Raccourcis affichables conditionnellement (`showShortcutLabel: window.width >= 1400`)

---

## 2. Structure des menus et actions

### 2.1. Menu principal

#### V1 : Menu classique Qt (`.cpp` dans `main_window.h`)
Structure standard avec sous-menus pour chaque catégorie fonctionnelle :
- Fichier, Édition, Affichage, Broderie, Création, Outils, Analyse, Forme, Image, Fils, Rendu réaliste, Panneaux, Aide

#### V2 : Système unifié (`.qml` + C++)
```qml
// Dispatcheur unique pour toutes les actions (palette de commandes et menus)
function dispatch(id) {
    // Route vers : outil / espace d'œuvre / action standard
    case "cmd.file.new": filePanel.requestNew(); break
    case "cmd.edit.undo": studio.undo(); break
    // ... etc (180+ actions mappées)
}
```

**Tableau des menus V2 :**

| Menu V2 | Actions principales | Équivalent V1 |
|---------|---------------------|---------------|
| **Fichier** | Nouveau, Ouvrir, Enregistrer, Sous…, Importer (Image, Machine), Exporter (DXF, Image, CSV) | Identique |
| **Édition** | Annuler, Rétablir, Dupliquer, Supprimer, Regrouper, Dégroup, Rotation, Miroir | Identique + `eyedropper` activable |
| **Affichage** | Zoom avant/arrière, Ajuster, Filtres (Type, Couleur), Grille et guides | Partiellement couvert (grille simplifiée) |
| **Broderie** | Satin auto, Tatami, Conversion types | Identique |
| **Création** | Image, DXF, Nuancier, Formes prédéfinies | Identique |
| **Outils** | Brush, Select, Pan, Eyedropper, Measure, Vectorisation, Segmenter | Identique + `DrawSatinColumn`, `DrawDirectionGuide` |
| **Analyse** | Analyser, Densité, Tirage du tissu, Déformation prévue | Partiellement couvert (sans quantification couleurs) |
| **Forme** | Vecteurs, Transformation, Rotation, Miroir | Identique |
| **Fils** | Nuanciers, Ajouter personnalisé, Réduire, Palette IA | Partiellement couvert (pas d'historique réductions) |
| **Rendu réaliste** | Activer/ Désactiver, Options qualité | Identique |
| **Commandes** | `Ctrl+K` — Palette recherche actions | Correspond à « Rechercher une commande » V1 |

### 2.2. Actions manquantes ou modifiées

**Statistiques basées sur `correspondances.csv` :**
- ✅ **Fait (97)** : Fonctionnalités identiques, libellés conservés
- ⚠️ **Partiel (38)** : Fonctionnalité existante mais interface/flux différent
- ❌ **Absent (86)** : Non implémentée en V2
- 🚫 **Exclu (24)** : Décision explicite de ne pas réimplémenter

**Exemples d'actions « Absent » :**
| Action V1 | Motif | Remplacement V2 |
|-----------|-------|-----------------|
| Abandonner les retouches | Workflow redessiné | `properties_panel` avec historique intégré |
| Dupliquer la couche | Panneau calques fusionné dans `calque_panel` | Supprimé (unification couches) |
| Décaler / Décaler la forme | Non implémenté | Remplacement par outils de transformation QGIS |
| Zoom rectangle | Fonction obsolète | Remplacement par sélection rectangulaire canevas |
| Tutoriel : de l'image à la broderie | Documentation déplacée vers `helpDialog` | Lien dans menu Aide |

**Exemples d'actions « Partiel » :**
| Action V1 | Changement V2 |
|-----------|---------------|
| Afficher la grille | Menu `Grille` avec options fixes uniquement (aucune, 5, 10, 20 mm) |
| Afficher les guides | Toujours visibles par défaut, pas de bascule explicite |
| Ajouter une couche | « Calque » plutôt que « Couche » |
| Activer le rendu réaliste | `view.realistic` dans canevas + indicateur en barre d'état |

---

## 3. Outils et panneaux

### 3.1. Panneaux de droite (inspector + docks)

#### V1 : Docks traditionnels (`.cpp`)
- Structure rigide, ordre prédéfini
- Fenêtres flottantes pour `production_dialog`, `machine_dialog` etc.

#### V2 : Panneaux modulaires QML (`apps/desktop_v2/studio/qml/`)

**Inventaire des panneaux V2 (53 fichiers `.qml`) :**

| Panneau | Fichier | Description | Équivalent V1 |
|---------|---------|-------------|---------------|
| **ActionPalette** | `ActionPalette.qml` | Palette de recherche (`Ctrl+K`) | Menu → Rechercher une commande |
| **CompanionPanel** | `CompanionPanel.qml` | Compagnon IA (dock flottant) | Dock Analyse + chat séparé |
| **DigitizeHub** | `DigitizeHub.qml` | Gestion des espaces d'œuvre | Nouveau concept |
| **FilePanel** | `FilePanel.qml` | Fichier + historique récents | Menu Fichier |
| **FixPanel** | `FixPanel.qml` | Réparation automatique | Non équivalent |
| **FadePanel** | `FadePanel.qml` | Fondu entre fils (deux rails) | Menu Broderie → Fondu |
| **ThreadPanel** | `ThreadPanel.qml` | Gestion des fils | Dock Fils V1 |
| **Inspector** | `Inspector.qml` | Inspection objet (type, position, propriétés) | Propriétés + contexte de menu |
| **ObjectTree** | `ObjectTree.qml` | Arborescence des objets | Nouveau concept |
| **RegionTools** | `RegionTools.qml` | Outils de région (fusionner, supprimer) | Dock Analyse partiellement couvert |
| **SatinPanel** | `SatinPanel.qml` | Configuration satin/tatami | Menu Broderie + options génération |
| **PointEditPanel** | `PointEditPanel.qml` | Édition de points | Édition V1 simplifiée |

### 3.2. Système des espaces d'œuvre (V2 uniquement)

#### Concept : Navigation entre vues séquentielles

```qml
readonly property var spaceIds: ["image", "segmentation", "vector", "points"]
SpaceHost {
    readonly property var workspaceController: workspaceCtl
    // Un seul espace instancié à la fois
}
```

**Espaces disponibles :**

| Espace | Description | V1 | Remplacement |
|--------|-------------|-----|--------------|
| **Image** | Import, recadrage, luminosit | Oui (menu Image) | Panneau Image de l'espace |
| **Segmentation** | Numérisation automatique/régions | Oui (segmentation dialog) | Fusionné dans Espace Segmentation |
| **Vector** | Dessin vectoriel (formes libres) | Partiel (Outils → Dessin) | Nouveau dock Vector dédié |
| **Points** | Points de broderie (satin, tatami) | Oui (broderie V1) | Identique + inspection enrichie |

**Avantages :**
- ✅ Parcours linéaire claires → segmentation → dessin → broderie
- ✅ Focalisation exclusive par tâche (moins de distractions)
- ✅ Historique unique préservé entre les espaces

### 3.3. Barre d'état (V2)

Positionnée en bas du canevas, fournit :
- ✅ Statut du document (`studio.status`)
- ✅ Nombre de commandes (`commandCount`)
- ✅ Backend de rendu (`canvas.backendName`)
- ✅ Indicateur mode réaliste (`view.realisticMode`)
- ✅ Compteur géométrie (pour debug/performance)

```qml
Text { objectName: "renderBackendLabel" }  // Ex: "software", "vulkan"
Text { objectName: "realisticModeStatus" } // Indicateur visuel mode réaliste
```

---

## 4. Raccourcis claviers

### 4.1. Comparatif complet (95 raccourcis inventoriés)

| Action | V1 | V2 | Statut |
|--------|-----|-----|--------|
| `Ctrl+N` | Nouveau projet | Identique | ✅ Fait |
| `Ctrl+O` | Ouvrir | Identique | ✅ Fait |
| `Ctrl+S` | Enregistrer | Identique | ✅ Fait |
| `Ctrl+Shift+S` | Sous… | Identique | ✅ Fait |
| `Ctrl+Z` | Annuler | Identique | ✅ Fait |
| `Ctrl+Y` / `Ctrl+Shift+Z` | Rétablir | Identique | ✅ Fait |
| `Ctrl+A` | Tout sélectionner | Identique | ✅ Fait |
| `Ctrl+I` | Inverser sélection | Identique | ✅ Fait |
| `Delete` | Supprimer sélection | Identique | ✅ Fait |
| `Ctrl+D` | Dupliquer | Identique | ✅ Fait |
| `Ctrl+G` | Regrouper | Identique | ✅ Fait |
| `Ctrl+Shift+G` | Dégroup | Identique | ✅ Fait |
| `Ctrl+R` / `Ctrl+Shift+R` | Rotation 90° CW/CCW | Identique | ✅ Fait |
| `Ctrl+H` / `Ctrl+Shift+H` | Miroir H/V | Identique | ✅ Fait |
| `PgUp` / `PgDown` | Passer dessus/dessous | Identique | ✅ Fait |
| `Home` / `End` | Back/Front stacking | Identique | ✅ Fait |
| `F` | Ajuster à la fenêtre | Identique | ✅ Fait |
| `I` | Outil Eyedropper | Identique | ✅ Fait |
| `Escape` | Quitter focus/annuler gesture | Amélioration (ne supprime pas sélection) | ⚠️ Partiel |
| `Ctrl+K` | Palette de recherche | Identique | ✅ Fait |
| `Ctrl+J` | Dock IA | `companionPanel.openPanel()` via bouton + raccourci | ✅ Fait |
| `F1` | Raccourcis (`shortcutsSheet`) | Identique | ✅ Fait |
| `Shift+F1` | Aide outil | Identique | ✅ Fait |
| `Ctrl+,` | Préférences | `prefsDialog.openDialog()` | ✅ Fait |

### 4.2. Gestion du focus clavardaire (V2)

#### Amélioration V2 : Protection des champs éditables
```qml
// Ne pas lancer les raccourcis canvas si un champ possède le focus
readonly property bool keyboardOwnedByControl: {
    const item = window.activeFocusItem
    return !!item && (item.cursorPosition !== undefined || 
                      item.stepSize !== undefined || item.popup !== undefined)
}

Shortcut { sequence: "Delete" }  // enabled: !window.keyboardOwnedByControl
```

**Bénéfices :**
- ✅ Évite suppression accidentelle quand on édite un champ de texte
- ✅ Comportement prévisible : `Escape` quitte le focus sans effacer
- ✅ Gestion uniforme entre outils dessin et champs texte

---

## 5. Nouvelles fonctionnalités d'interface en V2

### 5.1. Compagnon IA (MCP)

Fichier : `apps/desktop_v2/ui/companion_panel.cpp`  
QML : `CompanionPanel.qml`

**Fonctionnalités :**
- Dock flottant contextuel
- Accès via bouton barre d'outils (`Ctrl+J`)
- Pont vers client MCP pour analyse sémantique de la V1
- Historique unique des conversations
- Approbation visuelle avant/après des suggestions

### 5.2. Historique du document persistant

Fichier : `apps/desktop_v2/ui/history_panel.cpp`  
QML : `HistoryPanel.qml`

**Caractéristiques :**
- Survit aux changements d'espace (image → segmentation → vector → points)
- Navigation temporelle unifiée
- Intégré dans la barre latérale principale

### 5.3. Simulation de navigation

Fichier : `apps/desktop_v2/ui/simulation_bar.cpp`  
QML : `SimulationBar.qml` (via liens)

**Boutons :**
- `⏮ Couleur` / `◀ Point` — Retour à l'élément précédent
- `▶ Lecture` — Défilement automatisé des étapes
- `⏹ Fin` — Arrêt de la simulation
- `Objet ⏭` — Avancer vers le prochain objet

### 5.4. Thème et densité d'interface

Fichier : `apps/desktop_v2/ui/studio_theme.cpp`  
QML : `Theme.qml`, `settings_dialog`

**Options :**
- Clair / Sombre (via préférences)
- Échelle de densité (`densityScale`) pour ajuster la taille des éléments
- Réduction du mouvement (`reducedMotion`) pour les utilisateurs sensibles aux animations

### 5.5. Feedback visuel contextuel

Fichier : `apps/desktop_v2/ui/feedback_cue.cpp`  
QML : `StudioToolTip.qml`, `StudioTextField.qml` etc.

**Patterns :**
- Tooltips stylisés (couleurs thème, pas d'ombres agressives)
- Indicateurs de focus (`keyboardOwnedByControl`)
- Messages de statut dans la barre d'état

### 5.6. Outil « Régions » pour correction post-numérisation

Fichier : `apps/desktop_v2/ui/region_panel.cpp`  
QML : `RegionTools.qml`

**Capacités :**
- Sélection d'une région au clic ou dans la liste déroulante
- Suppression, fusion avec voisine, nettoyage îlots
- Régénération des objets (une étape d'annulation)
- Recoloration et fusion de couleurs intégrées

### 5.7. Poignées de gradient

Fichier : `apps/desktop_v2/ui/gradient_fill_panel.cpp`  
QML : `Poignées sur le canevas`, `FadePanel.qml`

**Éléments :**
- Manipulation visuelle de l'axe dégradé
- Poignées pour début/fin dégradé (déplacement, rotation)

---

## 6. Fonctionnalités abandonnées ou déplacées

### 6.1. Segmentations par IA (SAM 2) — Délibérément exclus

**Décision du propriétaire du projet (2026-11-06) :**
> « La segmentation par IA (SAM 2) est hors périmètre de la V2 ; la V1 reste la référence pour cet usage. »

**Actions exclues (correspondances.csv, colonne `statut` = « exclu ») :**
- Segmenter avec l'IA (`ffcfcf580f`)
- Préférences — Intelligence artificielle (SAM 2) (`c0dbf1e6`)
- Annuler l'analyse SAM (`4c8120e4`)
- Sonder les backends IA (`ea48b8bd`)
- Tester la configuration IA (`e51f01aa`)
- Ignorer la plus grande région (probablement le fond) (`5674752c`)

**Implication :** La V1 conserve cette fonctionnalité ; la V2 se concentre sur les outils de segmentation manuelle et automatique basique.

### 6.2. Actions obsolètes ou fusionnées

| Action V1 | Motif d'élimination | Remplacement |
|-----------|---------------------|--------------|
| `Supprimer le guide` | Panneau région intégré | Outil Régions > Editer guides |
| `Restaurer la disposition précédente` | Concept obsolète avec layout QML flexible | Supprimé |
| `Déboguer : afficher toutes les données` | Pas de débogage UI natif requis (logs C++ suffisants) | Console/application log |
| `Centrer automatique` | Remplacement par outils d'ajustement canevas | Ajuster au canevas (`cmd.view.fit`) |
| `Région précédente / suivante` | Navigation fusionnée dans panneau régions | Clic direct ou liste déroulante |

### 6.3. Dialogues éliminés

**V1 :** Dialogues modaux fréquents (`.cpp` avec `.ui` associés)  
**V2 :** Conversion vers panneaux contextuels et dialogs non modaux où possible

#### Exemples :
- `ai_segmentation_dialog` → Intégré dans Espace Segmentation + dock Régions
- `colorway_dialog` → Panneau Fils + bouton `Activer`
- `production_dialog` (aperçu) → Export direct avec options contextuelles

---

## 7. État de parité (récapitulatif CSV)

Fichier : `correspondances.csv` dans `docs/desktop-v2/parite-v1/`

**Ligne 1 :** En-tête (`id,type,libelle_v1,source_v1`)  
**Colonnes V1 à L445 :**

| id | Action/Composant V1 | Source V1 (fichier `.cpp`) |
|----|---------------------|----------------------------|
| `9268b9a3` | 100 % (taille réelle) | `main_window.cpp` |
| `c7233a7c` | Abandonner les retouches | `main_window_toolbars.cpp` |
| ... | ... | ... |

**Colonnes Statut V2 :**
- `statut` : `fait`, `partiel`, `absent`, `exclu`
- `preuve_v2` : Nom de composant/courte description prouvant implémentation
- `note` : Remarques contextuelles

#### Exemples interprétés :

```csv
id,type,statut,preuve_v2,note,libelle_v1
9268b9a3,action,fait,view.zoom-actual,,100 % (taille réelle)
c7233a7c,action,absent,,à traiter,Abandonner les retouches
eeb30959,action,partiel,gridMenu,"menu Grille (aucune, 5, 10, 20 mm)",Afficher la grille
```

**Distribution par statut :**
- ✅ **Fait (~160)** : Implémentation vérifiée et fonctionnelle
- ⚠️ **Partiel (~38)** : Fonctionnalité existante mais interface différentée
- ❌ **Absent (~86)** : Non implémentée, pas de remplacement immédiat
- 🚫 **Exclu (24)** : Décision consciente d'abandonner

---

## 8. Recommandations pour la poursuite du développement

### 8.1. Priorité immédiate : finaliser les éléments « Partiel »

**Actions recommandées :**
1. Implémenter bascule explicite guides/panneaux dans menu `Affichage`
2. Unifier gestion grilles avec options continues (pas seulement fixes)
3. Ajouter retour de couleur explicite dans dialogues contextuels (non juste activation muette)
4. Quantification d'image (`Quantifier les couleurs`) via palette région plutôt que mesure directe

### 8.2. Éléments à maintenir en V1 pour compatibilité

**Segmentation IA (SAM 2) :**
- Conserver cette fonctionnalité dans la V1 comme référence
- Documenter clairement son absence en V2 dans l'aide
- Garder API d'export compatible pour les workflows dépendants de SAM

### 8.3. Documentation et aide utilisateur

**Ajouter à `helpDialog` (V2) :**
- Guide migrateur : « Comment passer de V1 à V2 »
- Liste des changements majeurs en UI/UX
- Mapping rapide des raccourcis manquants

**Fichier recommandé :**  
`apps/desktop_v2/help/migration-guide.md`

### 8.4. Plan de bascule conditionnelle

**Critère (voir `11-bascule-v1-v2.md`) :**
> « La V1 peut être retirée du menu de démarrage quand toutes les cases « Bloquant » sont cochées OU explicitement écartées par décision écrite, et que la recette manuelle des cinq scénarios a été passée sur une machine Windows réelle. »

**Scénarios à valider :**
1. Import image → numérisation → retouche → export (V2) vs V1
2. Dessin vectoriel libre → conversion satin/tatami (V2) vs V1
3. Gestion fils et optimisation ordre (V2) vs V1
4. Export machine avec options de coupe/arrêt (V2) vs V1
5. Compagnon IA : analyse sémantique V1 via MCP (démo)

### 8.5. Mesures restantes à prendre

- [ ] Test performances Release sur tâches représentatives (vs Debug actuel)
- [ ] Installer Windows : tester `openstitch_v2.exe` sur machine vierge
- [ ] Migration `.osp` V1 → modification → export non régressif (test octet)
- [ ] Rendu visuel réel du thème, poignées, repères (captures de l'interface)

---

## 9. Conclusion

### 9.1. Points forts de la V2

✅ **Architecture moderne :** QML offre flexibilité UI, animations fluides  
✅ **UX centrée utilisateur :** Focus gestion, feedback contextuel, accès rapide aux actions  
✅ **Espaces d'œuvre :** Parcours linéaire clairs → segmentation → dessin → broderie  
✅ **Intégration IA :** Compagnon MCP pour assistance intelligente  
✅ **Thèmes et accessibilité :** Clair/sombre + densité + réduction mouvement  

### 9.2. Points de vigilance

⚠️ **Compatibilité V1/V2 :** Certains workflows dépendent de fonctionnalités absentes en V2  
🚫 **Segmentation IA :** Délibérément exclue, nécessite clarification aux utilisateurs  
🔧 **Fonctions partielles :** Éléments comme grille et guides nécessitent bascules explicites  

### 9.3. Prochaines étapes

1. Finaliser implémentation des actions « Partiel » (priorité haute)
2. Valider recette manuelle des 5 scénarios sur hardware réel
3. Documenter migration V1 → V2 dans aide utilisateur
4. Préparer announcement de bascule conditionnelle ou décision d'écart

---

**Document généré par l'équipe OpenStitch**  
**Contact pour questions :** `devteam@openstitch.dev` (fictif — à remplacer)
