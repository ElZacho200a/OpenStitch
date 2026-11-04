# 04 — Modèle d'interaction et design system

Statut : **spécification de direction + ce qui est déjà appliqué**. Les choix visuels n'ont pas été éprouvés avec des
utilisateurs (aucun test utilisateur n'a eu lieu) ; ils sont à valider par la recette des parcours (`05`, `06`).

## Principes (repris de la mission, avec leur application)

| Principe | Application dans V2 | État |
|---|---|---|
| A. Canevas central | le canevas occupe tout l'espace restant ; rail d'outils ≤ 56 px, un seul dock à droite | ✅ (capture 1920×1200 : le canevas = ≈ 75 % de la largeur, ≈ 65 % de la surface) |
| B. Workflow non linéaire | espaces = filtres sur un même document ; tout outil reste appelable (palette, à venir) | 🟡 |
| C. Une intention = une interaction | un registre d'outils unique ; test de conflit de raccourcis ; renommer/masquer = même lot que Claude | ✅ registre / ⬜ le reste |
| D. Manipulation directe | calque d'interaction `QGraphicsItem` au-dessus de l'aperçu raster | ⬜ |
| E. Inspecteur contextuel | la sélection pilote le panneau ; paramètres essentiels d'abord | ⬜ (la liste des objets existe) |
| F. Découverte progressive | outils non migrés visibles mais **désactivés avec la raison** (pas de faux boutons) | ✅ |
| G. Retour immédiat | ligne d'état persistante (rouge + police grasse pour l'erreur, `accessibleDescription`), progression, bouton Annuler | ✅ minimal |
| H. Tout changement maîtrisable | annuler/rétablir libellés (« Annuler « Masquer » »), aperçu ≠ modification, tâche annulée ne livre rien | ✅ |
| I. Une sélection cohérente | `SelectionModel` unique ; arbre ↔ modèle testés dans les deux sens | ✅ (canevas ⬜) |
| J. Cohérence totale | mêmes composants, mêmes états ; à vérifier à chaque lot | — |

## Espaces de travail

| Espace | Raccourci | Outils mis en avant | Panneaux (cible) |
|---|---|---|---|
| Conception | Ctrl+1 | sélection, vue, recadrage, formes, plume, crayon, couteau, texte, pipette, déformation, pot de peinture, zones de détail | image, segmentation, régions |
| Numérisation | Ctrl+2 | sélection, vue, satin, guides de direction, ligne de rupture, clic-pour-broder, couteau, texte, pipette | inspecteur de points, fils |
| Optimisation | Ctrl+3 | sélection, vue, règle | ordre, sauts/coupes, densité, tirage |
| Production | Ctrl+4 | sélection, vue, règle, pipette | simulation, rendu, cadre, export |

Le rail change avec l'espace (test `workspaces filter the tool rail`), la sélection et l'historique non.

## Interaction clavier et accessibilité (déjà appliquées)

- Chaque outil a un raccourci mono-touche unique par espace (test de conflits) ; Maj+touche pour la variante.
- Boutons du rail : texte = touche, nom accessible = libellé complet, infobulle = libellé + raccourci.
- Arbre documentaire : nom accessible, navigation clavier native Qt, édition F2/double-clic, case de visibilité.
- Canevas : `Qt::StrongFocus`, **F** ajuste, molette = zoom ancré sous le **curseur de l'événement** (pas
  `QCursor::pos()` : correct avec événements synthétiques et pavés tactiles, leçon de `ui-interaction-model.md` §0).
- Erreur : jamais un dialogue bloquant ; message persistant jusqu'à la prochaine action réussie.

## Design system

**État actuel** : V2 hérite du style et du thème clair/sombre du système (Qt 6.8, vérifié en capture sombre). Il
n'embarque **pas encore** de design system propre : pas de tokens, pas d'icônes, pas de feuille de style. L'ancien
desktop en a un (`design_tokens.*`, `app_theme.*`, `ui_icons.*`) qui sera **repris et épuré**, pas réinventé.

**Direction décidée** (à matérialiser dans le lot « infrastructure d'interface ») :

- Densité : compacte par défaut (rail 44 px, lignes d'arbre ≈ 22 px, mémorisation par espace) ; confortable en option.
- Couleurs : une teinte d'accent unique (sélection, outil actif), neutres du système pour les surfaces ; le rouge est
  réservé aux erreurs ; la couleur d'un fil n'est jamais décorative (pastille = donnée).
- Typographie : police système, une seule échelle (corps / secondaire / titre de panneau) ; pas de gras décoratif.
- Pas de cartes, pas d'ombres ni de dégradés décoratifs ; hiérarchie par alignement, séparateurs fins, regroupement.
- Icônes : jeu monochrome unique, 20 px, trait constant ; le texte reste visible pour les outils rares.
- Responsive : sous 1000 px, docks en onglets et rail replié ; cibles tactiles ≥ 40 px quand le mode tactile est actif.

**Ce qui n'est pas fait** : maquettes de parcours, thème personnalisé, icônes, mode tactile, fenêtres < 1000 px
(non testées), anglais (aucune chaîne V2 n'est dans `i18n/openstitch_en.ts` : les `tr()` existent mais ne sont pas extraits).
