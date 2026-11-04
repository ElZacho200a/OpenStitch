# 02 — Inventaire des capacités et matrice de parité

Cette matrice est **la** liste de suivi jusqu'à la bascule. Source : `apps/desktop/main_window_*.cpp` (une
responsabilité par fichier), panneaux et dialogues de `apps/desktop/`, `tools.hpp`, catalogue `project_ops`.
Elle est à mettre à jour à chaque PR (règle de revue : une PR V2 qui change un statut modifie ce fichier).

Légende — **✅** équivalent V2 fonctionnel et testé · **🟡** partiel (précisé) · **⬜** à migrer.
Espace cible : **C** Conception · **N** Numérisation · **O** Optimisation/Analyse · **P** Production · **T** transverse.

L'inventaire est fait au niveau « famille de capacités » (une ligne par fichier de responsabilité ou groupe de
fichiers). Il **ne** descend **pas** encore au niveau de chaque champ d'inspecteur (≈ 160 Ko de `properties_panel.cpp`) :
ce niveau fin sera produit au début de chaque lot de migration concerné (voir `06`), pas avant, pour ne pas figer une
liste qui bougera. C'est une limite assumée de cette première livraison.

## Matrice

| # | Capacité | Interface actuelle (fichiers) | Moteur / commande | V2 | État | Remarques |
|---|---|---|---|---|---|---|
| 1 | Ouvrir une image | `main_window_files`, `import_dialog` | `image::load_image` | T | ✅ | + numérisation automatique ; résolution via « hauteur visée » (pas de dialogue modal) |
| 2 | Ouvrir un projet `.osp` | `main_window_files`, `recent_files` | `project_io::load_project` | T | 🟡 | ouverture ✅ (tâche d'arrière-plan, test aller-retour enregistrement → ouverture) ; **fichiers récents ✅** (sous-menu, persistés, entrées disparues élaguées), migration de schéma signalée ⬜ |
| 3 | Importer DST / SVG / formats machine | `main_window_files`, `import_dialog` | `formats::read_*`, op `import_svg` | T | 🟡 | **import SVG ✅** (`import_svg`, type de points au choix) ; import DST / formats machine ⬜ |
| 4 | Enregistrer `.osp`, autosave, récupération | `main_window_files`, `autosave`, `crash_report*` | `project_io::save_project` | T | 🟡 | enregistrement ✅ ; **autosave ✅** (60 s si modifié, écriture atomique) et **récupération au démarrage ✅** ; rapport de plantage ⬜ |
| 5 | Exporter pour la machine | `machine_export_dialog`, `production_dialog` | `effective_sequence` + `formats::registered_formats()` | P | ✅ | **tous les formats inscrits qui savent écrire (DST, PES, JEF, EXP, VP3…)**, couleurs de blocs dérivées du projet, nom de motif, écriture atomique, tâche annulable ; options machine avancées (départ, coupes, arrêts) ⬜ |
| 6 | Exporter image / PDF / imprimer | `export_image*`, `print_preview` | — | P | 🟡 | **export d'image PNG (résolution réglable, tâche annulable) ✅** ; **fiche de production PDF ✅** (dimensions, comptes, plan de fils, aperçu) ; impression directe ⬜ |
| 7 | Prétraitement d'image (luminosité, rotation, recadrage…) | menu Image, `brightness_dialog`, `main_window_rotation` | `image::ImageOp`, op `image_denoise` | C | 🟡 | **réduction du bruit (médian / bilatéral / aplatir) ✅** ; **luminosité / contraste, niveaux de gris, netteté, CLAHE, rotations, retournements, recadrage ✅** (menu Image, une annulation par étape) |
| 8 | Segmentation (couleurs, régions, nettoyage) | `segmentation_dialog`, `segmentation_colors_panel`, `main_window_regions`, `segment_preview` | `segmentation::*`, ops `seg_*`, `segment_image` | C | 🟡 | exécutée **dans** la numérisation automatique ✅ ; pas de pilotage manuel ⬜ |
| 9 | Segmentation IA (SAM) | `ai_segmentation_dialog`, `sam_worker_client`, `ai_preferences*` | `ai_segmentation` | C | ⬜ | hors priorité produit (mémoire projet) : portée en dernier |
| 10 | Zones de détail, clic-pour-broder | `main_window_detail_zones`, `main_window_click_to_stitch` | `click_to_stitch_commands` | C/N | 🟡 | clic-pour-broder (tolérance, type auto/remplissage/satin/contour), pinceau de zones de détail (Ctrl = gomme), image source en calque (Affichage > Image source), masque affiché ; tout est annulable en une étape |
| 11 | Dessin vectoriel (formes, polygone, Bézier, main levée) | `main_window_draw_tools`, `_shapes`, `_vector_tools` | `vector_ops`, `vector_path_commands`, op `add_shape` | C | 🟡 | **rectangle, ellipse (Maj = cercle), polygone ✅** par `add_shape` (une annulation par forme, forme refusée = document intact) ; Bézier, main levée, polygone régulier, satin ⬜ |
| 12 | Édition de nœuds, reshape, pinceau, pot de peinture, couteau | `main_window_vector_edit`, `_node_select`, `_reshape`, `_brush`, `_bucket`, `node_handle.hpp` | `vector_ops`, `outline_ops` | C | 🟡 | **couteau ✅** (ligne tracée sur les formes sélectionnées, chaque morceau devient un objet, une annulation ; une ligne qui ne traverse rien est refusée par le moteur) ; poignées de nœuds, reshape, pinceau de déformation, pot de peinture ⬜ |
| 13 | Opérations géométriques (fusion, découpe, simplifier, lisser, trous) | `main_window_objects`, `_regions`, menu Forme | ops `merge`, `split`, `simplify_shapes`, `smooth_shapes`, `remove_holes`… | C | 🟡 | **fusion (union / soustraction / intersection / exclusion), simplifier, lisser, combler les trous, supprimer les miettes, séparer, numériser les formes ✅** (sur la sélection, une annulation chacun) ; découpe au couteau, pinceau de déformation ⬜ ; **moteur ✅ (lot)** ; interface ⬜ |
| 14 | Calques | `calque_panel` | `calque_commands` | C | 🟡 | **panneau des calques ✅** (ajouter, renommer, supprimer, monter / descendre, visibilité, verrou, déplacer la sélection ; un calque masqué n'est ni dessiné ni cousu ; une annulation par action) ; calque actif de création, drag & drop ⬜ |
| 15 | Sélection (clic, rectangle, nommée, retrouver) | `main_window_selection_*`, `selection_model`, `selection_queries` | — | T | 🟡 | **modèle unique ✅** (`SelectionModel`) + liste ✅ + **clic au canevas ✅** (Ctrl = bascule, Maj = ajoute, vide = désélectionne, `HitIndex` sur les brins cousus) ; **cadre élastique ✅** (sélection « fenêtre » : objets entiers ; Ctrl/Maj ajoutent), **Tout sélectionner / Désélectionner ✅** ; nommées ⬜, « retrouver » ⬜ |
| 16 | Transformations (déplacer, tourner, miroir, aligner, répartir) | `main_window_objects`, `_rotation`, `_arrange`, `_editing` | `transform_ops`, `arrange_ops`, ops `translate/rotate/scale/mirror` | C/N | 🟡 | **déplacer au glisser, flèches (1 mm, Maj = 10 mm), mettre à l'échelle (poignées de coin), pivoter ±90°, miroir H/V, dupliquer, supprimer ✅**, sélection multiple, une annulation ; **angle libre (Position et taille…), aligner (6 modes), répartir (H/V) ✅** |
| 17 | Groupes, ordre d'empilement | `document_panel` | `group_commands`, ops `group/ungroup/stack_order` | T | 🟡 | **ordre de couture (premier plan / avancer / reculer / arrière-plan) ✅** ; **groupes ✅** (Grouper Ctrl+G / Dégrouper Ctrl+Maj+G, une annulation) |
| 18 | Type de point et paramètres (inspecteur) | `properties_panel` (2 951 l.), `stitch_type_catalog` | `convert_ops`, ops `set_stitch_type`, `set_params` | N | 🟡 | **inspecteur contextuel ✅** : type parmi 6, couleur, visible, verrouillé, **et réglages de couture essentiels par type (contour 2, tatami 7, directionnel 5, satin 5 ; table `paramSpecs`, `set_params`, valeurs communes / mixtes, une annulation)** ; autres réglages (≈ 150 champs du moteur : sous-couches fines, motifs, appliqué, guides…) ⬜ |
| 19 | Satin : colonnes, rails, guides | `main_window_draw_tools`, `satin_guide_item.hpp` | `SatinParams`, op `set_geometry` | N | 🟡 | outil « Colonne satin » (S) : clics par paires (rail A, rail B), Entrée termine, rails croisés signalés ; services de déplacement / insertion de nœud de rail testés |
| 20 | Auto-satin et variantes | `main_window_satin_auto`, `auto_satin_strategy_ui.hpp` | `auto_satin`, op `auto_satin` | N | 🟡 | **satin automatique ✅** (écart, 3 stratégies, sur la sélection) ; guides imposés, variantes fines ⬜ |
| 21 | Remplissages spéciaux (directionnel, motif, radial, spirale, croisé, point de croix, appliqué) | `main_window_directional`, `_fill_center`, `_applique` | `DirectionalFillParams`…, op `directional_guides` | N | 🟡 | **réglages essentiels de la spirale, du point de croix et de l'appliqué dans l'inspecteur ✅** ; motifs, radial, croisé, contour concentrique : réglages ⬜ |
| 22 | Dégradés et fondus entre fils | `color_blend_dialog`, `gradient_fill_panel`, `main_window_color_blend`, `_gradient` | `color_blend_commands`, `project_ops/gradient_fill` | N | 🟡 | **fondu de deux fils créé / édité / supprimé ✅** (`project_ops::gradient_fill` : deux couches, fil du dessus, rampes complémentaires ou non ; le moteur refuse un remplissage incompatible avec son message ; une annulation) ; poignées de transition au canevas, courbes de densité, fil de nuancier ⬜ ; audit dédié : `docs/audit-fondu-ux-2026-11-03.md` |
| 23 | Directions de points / guides de direction | `main_window_directional` | outil `fill.direction-guide` | N | 🟡 | **guides de direction automatiques ✅** ; tracé manuel des guides et lignes de rupture ⬜ |
| 24 | Sous-couches, densités, compensations | `properties_panel`, `density_overlay`, `main_window_density` | params d'objet | N/O | 🟡 | **sous-couches (mode manuel / auto, zigzag, retrait, écart, rangées), liaisons cachées ✅** dans l'inspecteur ; densité par zone, cartes de densité ⬜ |
| 25 | Compensation de tirage prédictive | `main_window_pull_preview`, `fabric_params.hpp`, `fabric_risk_overlay` | `project_ops/fabric_deformation`, `color_registration` | O | 🟡 | **mode de compensation manuel / auto / adaptatif / PRÉDICTIF choisi dans l'inspecteur ✅** (tatami, directionnel, satin, spirale) ; carte de déformation en surimpression, étalonnage sur pièce-test, solveur interactif ⬜ |
| 26 | Édition manuelle de points | `main_window_point_edit` | `stitch_point_edit_commands` | N | 🟡 | outil « Retouche de points » (J) : poignées des points cousus de l'objet sélectionné, clic = choisir, clic ailleurs = déplacer, Maj+clic = insérer, Suppr = supprimer, Édition > Effacer les retouches de points |
| 27 | Fils, nuanciers, coloris | `thread_panel`, `thread_strip`, `colorway_dialog`, `main_window_threads/_colors/_colorways` | `thread_commands`, `colorway_commands`, `thread_palette` | N/P | 🟡 | **couleur libre éditable ✅** (inspecteur) ; **panneau Fils ✅** (plan de fils par couleur, masquer/afficher, sélectionner, recolorer, fusionner), **coloris ✅** (ajouter, activer, renommer, supprimer, annulable) ; palette de nuanciers / mélange ⬜ |
| 28 | Ordre de broderie, sauts, coupes, optimisation | `main_window_order_filter`, `_order_jumps`, `order_list_widget` | `sewing_order_commands`, `optimization`, op `optimize_order`, `set_order` | O | 🟡 | optimisation **appliquée** à la numérisation ✅ ; pilotage et liste d'ordre ⬜ |
| 29 | Simulation de couture | `main_window_simulation`, `simulation_nav_bar` | `stitch_render/simulation` | P | 🟡 | **simulation : curseur sur la séquence (le cadre ne bouge pas) + lecture automatique, espace Production ✅** ; vitesse réglable, tête de couture, sons ⬜ |
| 30 | Rendu réaliste | `main_window_realistic`, `realistic_*` | `stitch_render`, `render_realistic_png` | P | 🟡 | aperçu ✅ (non interactif, instantané, révision étiquetée) ; options de rendu ⬜ |
| 31 | Analyse : brodabilité, densité, risques, tissu | `main_window_analysis`, `_density`, `_fabric_risk` | `stitch_analysis`, `project_ops/analysis`, `quality` | O | 🟡 | **diagnostic de brodabilité ✅** (`diagnose_project` en tâche annulable, constats cliquables → sélection + centrage, résultat marqué périmé après modification) ; cartes de densité / risque tissu, fidélité, correctifs proposés ⬜ |
| 32 | Cadres et machine | `machine_dialog`, `main_window_machine` | `machine_commands`, op `set_machine`, `set_canvas` | P | 🟡 | **cadre (taille, marge, forme) et machine ✅** ; bibliothèque de cadres par modèle de machine ⬜ |
| 33 | Lettrage / texte | `text_dialog`, `text_advanced_panel`, `main_window_lettering`, `font_catalog` | `text_commands`, `lettering`, ops `add_text/set_text` | C/N | 🟡 | **ajout de texte ✅** (police, hauteur, remplissage, alignement, rotation) ; édition, texte sur courbe, polices installées ⬜ |
| 34 | Historique (annuler / rétablir) | menu Édition | `UndoStack` | T | 🟡 | annuler/rétablir ✅ (libellé de l'étape) ; **liste d'historique ✅** (Édition > Historique : revenir à n'importe quelle étape) |
| 35 | Compagnon Claude / MCP | `companion_dock`, `claude_backend`, `live_server`, `main_window_companion` | `mcp::LiveService`, `project_ops::apply_batch` | T | 🟡 | **primitive commune ✅** (`DocumentSession::previewBatch/applyBatch` : même lot que Claude) ; panneau, session live, aperçu avant/après ⬜ |
| 36 | Préférences, thèmes, langues, densité d'UI | `app_theme`, `app_language`, `general_preferences_dialog`, `responsive.hpp`, `design_tokens` | — | T | 🟡 | **thème système / clair / sombre ✅** ; langue (EN), densité d'interface, mémorisation des réglages ⬜ ; V2 suit le thème système ; tokens et i18n EN ⬜ |
| 37 | Aide, palette de commandes, raccourcis | `help_dialogs`, `help_manual`, `command_palette_dialog`, `interaction_map` | — | T | 🟡 | registre d'outils + raccourcis uniques testés ✅ ; **palette de commandes Ctrl+K ✅** (recherche insensible aux accents, 20 outils + espaces + fichier/édition/sélection) ; **aide ✅** (F1 : raccourcis et commandes générés depuis le registre, À propos) ; manuel complet ⬜ |
| 38 | Mesure, règles, grille, guides | `main_window_measure`, `ruler`, `grid_settings_dialog` | — | C | 🟡 | **règle (distance, Δx, Δy, angle ; ne modifie jamais le document) et grille 5 / 10 / 20 mm alignée sur l'origine ✅** ; guides aimantés, magnétisme ⬜ |
| 39 | Pipette | `main_window_eyedropper` | — | C/N | 🟡 | **pipette ✅** : prélève le fil de l'objet cousu sous le curseur et l'applique à la sélection (une annulation) ; échantillonnage de l'image source et fils de nuancier proches ⬜ |
| 40 | Menus contextuels | `main_window_context_menu` | — | T | 🟡 | **menu contextuel du canevas (suppression, duplication, transformations, ordre, satin auto, guides ; le clic droit sélectionne l'objet) ✅** ; menus par type d'objet dans l'arbre ⬜ |
| 41 | Réglages de numérisation automatique | `generation_options_dialog`, `autodigitize_presets.hpp` | `DigitizeSettings` | N | 🟡 | réglages par défaut + hauteur visée ✅ ; options avancées, préréglages ⬜ |
| 42 | Préréglages de points | `main_window_stitch_presets` | `stitch_preset_commands` | N | 🟡 | **préréglages de points ✅** (enregistrer les réglages d'un objet sous un nom, les appliquer à la sélection, supprimer ; stockés dans le projet ; menu, palette, menu contextuel) ; préréglages globaux entre projets, import / export ⬜ |
| 43 | Vues d'affichage (passes, plans, densité, risques) | `main_window_view_display`, `view_display_state.hpp` | `stitch_render/view_display` | T | 🟡 | **cadre de broderie et grille affichables ✅** ; passes de couture, plans d'affichage, cartes de densité / risque ⬜ |
| 44 | Espaces de travail / disposition des panneaux | `main_window_ux`, `ui_memory.hpp` | — | T | 🟡 | 4 espaces filtrant le rail d'outils ✅ ; panneaux par espace, mémorisation ⬜ |

**Décompte au 2026-11-04** : 44 familles — ✅ 2 · 🟡 41 · ⬜ 1. Aucune exception (« non migré volontairement ») n'est
prononcée à ce stade ; la seule candidate argumentée est la segmentation IA (ligne 9), décalée en dernier mais pas supprimée.

## Parité des outils de canevas (vérifiée par test)

Les 20 énumérateurs de `apps/desktop/tools.hpp::Tool` ont chacun un équivalent déclaré dans
`ToolRegistry::builtin()` (test `every legacy canvas tool has a V2 equivalent`). **Déclaré ≠ fonctionnel** : seuls
`select`, `pan`, `draw.rectangle`, `draw.ellipse`, `draw.polygon`, `measure`, `eyedropper` et `knife` sont activés ; les 12 autres sont affichés **désactivés** avec la raison dans l'infobulle.

## Écart manuel / agent (à fermer)

Le catalogue de lot compte 55 opérations (`openstitch-cli project operations`). Toute capacité migrée vers V2 doit
exister **comme opération de lot** (ou en être une composition) avant d'être branchée à l'interface : c'est la règle
« une seule architecture pour l'utilisateur et pour Claude » (ADR 0004). Les gestes manuels sans équivalent de lot
sont listés dans le journal au fur et à mesure de leur découverte.


## Familles encore ⬜ au 2026-11-04 et pourquoi

| # | Famille | Raison |
|---|---|---|
| 9 | Segmentation IA (SAM) | **Exception justifiée** : hors priorité produit (décision du propriétaire du projet, voir mémoire du projet) ; l'ancien desktop reste disponible pour cette fonction |
| 10 | Zones de détail, clic-pour-broder | pas de glisser continu pour déplacer le pinceau en aperçu ; pas de panneau des zones de détail (seulement le masque en surimpression) ; pas de remplissage des trous réglable |
| 19 | Satin : colonnes, rails, guides | pas de poignées de rails sur le canevas (déplacer / insérer un nœud n'existe qu'en service testé), pas de guides ni de barreaux éditables, pas de poignées Bézier |
| 26 | Édition manuelle de points | pas de glisser-déposer d'une poignée (déplacement en deux clics), pas de sélection multiple de points, pas de changement de type de point ni de coupe après point dans l'interface (le service moteur `set_type`/`set_trim` n'est pas encore exposé) |
