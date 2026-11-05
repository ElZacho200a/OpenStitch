# Desktop V2 — sensation, finition et fluidité (« feel »)

> Proposition UX pour revue, 2026-11-05. Conserver les fonctionnalités, le moteur, les raccourcis et les formats existants.
> Complète [04 — modèle d'interaction](04-ux-design-system.md) et [05 — parcours](05-parcours.md).
> Le « jus » récompense **une intention réussie** et améliore la lisibilité du changement ; il n'est jamais une animation décorative.

## 1. Diagnostic du V2 actuel

- Architecture `DocumentSession` / `SelectionModel` / `TaskManager` saine, mais `ShellWindow` reste le point d'orchestration central (plus de 2 500 lignes).
- Le rail d'outils est difficile à lire sans connaître les raccourcis : PR #255/#256 déjà ouvertes, à **dédupliquer**.
- La navigation temporaire Espace / clic milieu est intégrée (#258). Le trackpad et le cadrage de sélection sont proposés (#257).
- Les données de broderie et les effets d'interface ne sont pas séparés visuellement : le rose de sélection doit signifier « éditable », jamais une couleur de fil.
- L'aperçu réaliste est lourd à recalculer ; aucun effet ne doit forcer `effective_sequence` ou un rendu raster supplémentaire.
- Une opération peut être correcte techniquement mais peu satisfaisante : validation silencieuse, état occupé discret, réglages répétitifs (#259), commandes indisponibles opaques (#260).
- Aucun test utilisateur comparatif V1 / V2 n'existe ; ne pas confondre présence d'une fonctionnalité, qualité du rendu et fluidité du geste.

## 2. Direction artistique : « atelier de précision »

**Positionnement** : logiciel de création textile exigeant ; calme, dense, net, chaleureux. Éviter le style « dashboard SaaS » et les animations gratuites.

| Élément | Règle | Justification |
|---|---|---|
| Surfaces | graphite / ardoise en sombre ; ivoire / gris chaud en clair | laisser la broderie et les vraies couleurs de fils dominer |
| Accent unique | rose couture / framboise désaturée pour action active, sélection, focus | faire correspondre le shell et les poignées de sélection |
| Sémantique | vert = succès, ambre = attention, rouge = erreur ; libellé toujours présent | ne jamais se reposer sur la couleur seule |
| Élévation | aplats, séparateurs fins, bordures régulières, pas d'ombres systématiques | lisibilité à 1280×720, faible surcharge GPU |
| Typographie | police système, 3 niveaux : contenu / secondaire / titre de zone | réduire la fatigue visuelle |
| Espaces | Conception / Numérisation / Optimisation / Production conservent un document et un undo | pas de rupture d'état entre étapes |
| Données métier | nuanciers et fils toujours en RGB réel, pas de recoloration par thème | fidélité textile |
| Dessin | plein contrôle d'un canevas de grande taille, outils contextuels sans fenêtres modales | le motif reste le centre du produit |

### Tokens initiaux proposés (mesures de contraste à vérifier)

Les valeurs sont des **hypothèses de design à tester** sur Windows clair/sombre et sous différents DPI ; elles ne sont pas des couleurs de fil.

| Token | Sombre | Clair |
|---|---|---|
| `background` | `#191B21` | `#F7F6F3` |
| `surface` | `#242731` | `#FFFFFF` |
| `surface-raised` | `#2D313D` | `#F1EFEB` |
| `border` | `#414653` | `#D8D4CF` |
| `foreground` | `#ECECF2` | `#22252D` |
| `muted` | `#B6BBC8` | `#575D69` |
| `accent` | `#F08CB7` | `#A92D66` |
| `accent-soft` | `#483141` | `#F8E2EB` |

## 3. Motion — quand et comment

**Échelle de temps**, point de départ avant mesure : 90–140 ms pour activation, 140–200 ms pour confirmation, 200–260 ms maximum pour transition de contexte. Courbes `OutCubic` ou `OutQuad`, jamais rebond, overshoot, « confettis » ou mouvement continu.

**Règles d'or** :
1. Le geste modifie d'abord la donnée ; l'effet reflète le résultat **après succès**, jamais avant.
2. Un `undo` reste une action, pas un temps d'attente : aucune animation ne bloque la souris ni le clavier.
3. Tout état est compréhensible **sans** animation : forme, couleur et texte identifient l'opération.
4. Le mouvement est désactivable (préférence `none / subtle`) ; suivre « animation réduite » quand la plateforme l'expose et garder une désactivation explicite.
5. Pas d'animation pendant une tâche de calcul lourde ni pour plus de 50 objets sélectionnés ; le rendu machine et le DST ne dépendent jamais de l'effet.
6. Ne jamais animer l'aperçu brodé en modifiant le projet, les points, la densité, l'ordre, ni les dimensions du canvas. Les surimpressions vivent dans `ui/` uniquement.

### Matrice des micro-interactions

| Déclencheur | Feedback proposé | Durée/budget | Sans motion |
|---|---|---|---|
| Choisir un outil | état actif accentué + libellé persistant ; transition de fond très courte | ≤ 120 ms | changement immédiat |
| Sélectionner 1–20 objets | apparition douce du contour / poignée, focus net | ≤ 150 ms | contour immédiat |
| Déplacer / redimensionner | silhouette fantôme légère pendant le drag ; mesure mm / repère d'accroche | 60 fps cible (sans regénérer les points) | même silhouette statique |
| Valider un réglage | confirmation discrète (« appliqué »), un undo pour un geste | ≤ 150 ms | texte visible |
| Terminer un export | accusé de réussite compact avec fichier et chemin accessible ; échec persistant | ≤ 200 ms | état statique |
| Calcul de numérisation | progression déterminée quand possible, libellé de phase, Annuler visible | **pas de faux progrès** | même progression |
| Diagnostic cliquable | centrer sur le(s) objet(s) + surbrillance, sans masquer la zone | ≤ 180 ms | centre / bordure |
| Changer d'espace | garder le motif et le zoom ; transition des panneaux sans déplacer le dessin | ≤ 180 ms | bascule immédiate |

## 4. Lots indépendants et ordre d'intégration

### A — Shell & tokens (PR de code indépendante)
- Créer les couleurs / espacements / états en un seul module UI, **pas** dans `ShellWindow`.
- Intégrer une première feuille de style Qt Widgets en clair et sombre, sans altérer les nuanciers réels.
- Vérifier : menus, docks, onglets, liste, boutons, champs, focus clavier, disabled et deux thèmes.
- Vérifier la restauration du thème lors du changement `Affichage > Thème` ; ne pas coder une palette sombre définitive.
- Tests : jetons non vides, sélection par thème, règles de focus, widgets accessibles.

### B — Feedback de fin d'action (PR de code indépendante)
- Réutiliser les messages de la barre d'état : message persistant et distinction erreur / succès.
- Ajouter un indicateur d'accent animé **uniquement pour les opérations réussies** avec `QVariantAnimation` / `QPropertyAnimation`, sans remplacer ni masquer le texte.
- Préférence explicite « mouvement réduit », testée ; *pas* d'effet sur les commandes et les fichiers.
- Limiter la fréquence des animations ; annulation / erreur = retour immédiat, jamais faux « succès ».
- Tests : démarrage / arrêt de l'animation, succession rapide, absence d'effet avec mouvement réduit.

### C — Interaction canevas (PR de code indépendante)
- Micro-retour de sélection sur les superpositions `QGraphicsItem` sans toucher au raster rendu.
- Interrompre ou supprimer l'animation si la sélection change rapidement, si le canvas est effacé, si la sélection est grande ou si la préférence réduit le mouvement.
- Le futur déplacement « fantôme » n'est pas à maquiller en mouvement fluide si la génération est lente : obtenir d'abord un geste direct, puis calculer après relâchement.
- Tests : pas de mutation du document, sélection stable, absence de fuite d'items.

### D — Parcours de broderie (lots fonctionnels, après A–C)
- **Satin** : rails A/B éditables avec poignées, Bézier, barreaux et aperçu provisoire local (sans régénération complète à chaque pixel).
- **Fondu** : poignées de transition et axe sur le canevas, contrôle direct de l'étendue et aperçu de densité.
- **Compensation** : superposition nominal / prédictif, curseur d'intensité, vue avant/après, calibration lisible.
- **Optimisation** : mini-comparatif avant/après montrant distance, sauts et coupes ; validation unique annulable.
- Ces lots doivent conserver l'accès aux réglages avancés, aux trois variantes de satin et à toutes les opérations MCP.

### E — Mesure de « bonne sensation » et bascule
- Captures 1280×720, 1920×1200, écran 200 % DPI ; thème clair/sombre ; clavier, souris, trackpad.
- Avec les mêmes projets, mesurer V1 vs V2 : nombre de clics, changements d'outil, temps pour atteindre l'objectif, erreurs, nombre de retours en arrière.
- Mesurer le **p95** de latence entre geste et retour visuel ; cible 50 ms pour la réaction UI locale (hors génération broderie), aucun freeze de plus de 100 ms sur actions courantes.
- Essayer avec 1, 100, 500 et 1000 objets ; cible aucune animation qui augmente le temps de numérisation ou altère le DST octet pour octet.
- Questionnaire qualitatif après chaque tâche : « je comprends ce qui vient de se passer », « le geste m'a paru précis », « je veux répéter ce geste » (échelle 1–5).
- Ne pas basculer `openstitch.exe` sur V2 tant que les workflows critiques et la sécurité documentaire ne sont pas validés.

## 5. Garde-fous de revue

- **Pas de feature supprimée** pour améliorer la sensation ; un outil incomplet reste identifiable et non trompeur.
- Les couches `core/`, `libs/`, formats DST/OSP et MCP ne dépendent d'aucun style ou mouvement UI.
- Toute PR de motion fournit : déclencheur, condition d'annulation, comportement réduit, test de rapidité / coalescence, capture ou recette.
- Le travail des PR #253/#254 (sécurité), #255/#256 (rail), #257 (navigation), #259 (inspecteur), #260 (palette) est **à consolider, pas dupliquer**.
- Les couleurs du motif et de la palette de fil ne sont jamais retouchées par l'habillage du shell.
- Éviter une animation qui masque le résultat de l'action, attire l'œil hors du canevas ou s'affiche à répétition pendant la simulation.

## 6. Critères de succès par parcours (recette humaine)

| Parcours | Critère d'acceptation |
|---|---|
| Image → broderie → DST | progression compréhensible, document jamais perdu, sortie confirmée sans blocage |
| Tracer → sélectionner → déplacer | geste continue après un pan temporaire, sélection facile, repères visibles |
| Réglage de satin en rafale | paramètre manipulable sans tremblement de l'inspecteur ni historique saturé |
| Modifier un fondu | « où agit le fondu » compréhensible sans refermer/rouvrir un dialogue |
| Optimiser les coupes | résultat chiffré et annulable, pas seulement une notification « terminé » |
| Session Claude / MCP | aperçus avant/après explicites, permission et annulation sans surprises |

## 7. Références techniques et ergonomiques

- Qt 6 — [Animation framework](https://doc.qt.io/qt-6/animation-overview.html) : animation des propriétés GUI / objets, courbes et chronologie.
- Apple HIG — [Motion](https://developer.apple.com/design/human-interface-guidelines/motion) : fonction plutôt que décoration, feedback bref et mouvement optionnel.
- Code OpenStitch : `apps/desktop_v2/ui/`, `docs/desktop-v2/02-inventaire-et-parite.md`, `04-ux-design-system.md`.

## Essai agent UI — accès permanent aux outils (2026-11-05)

**Constat vérifié dans le code :** le rail vertical liste tous les outils de l'espace mais une petite hauteur de fenêtre peut masquer les dernières entrées. Son mode compact économise de la largeur, pas de la hauteur. Le menu « Outils » classique regroupe des actions métier et n'est pas l'inventaire du rail.

**Correction :** bouton « Tous les outils » dans la barre des espaces, sans remplacement du rail ni mutation du document. Son menu reflète les outils de l'espace courant, garde icône, libellé complet et raccourci dans l'infobulle, affiche mais désactive avec justification les outils non migrés. Chaque choix réutilise la commande `tool.<id>` : pas de seconde logique de sélection et aucun nouveau raccourci clavier concurrent.

**User story :** en tant qu'utilisateur sur écran de faible hauteur (ou avec l'inspecteur déployé), je veux accéder à n'importe quel outil disponible sans agrandir la fenêtre ni mémoriser les raccourcis, afin de poursuivre ma correction de broderie sans friction.

**Critères d'acceptation :** le bouton reste dans la barre supérieure ; chaque espace offre exactement ses outils ; « Régions » est accessible dans Conception ; les outils non migrés sont désactivés et expliqués ; changer d'outil ne modifie pas la révision du projet ; changement d'espace restaure « Sélection » si l'outil précédent est absent. Test : `allToolsMenuKeepsLowRailToolsAccessibleWithoutEditingTheDocument`.

**À vérifier manuellement :** capture réelle Windows à 1024×520 et 1280×720 (clair/sombre), comportement des menus sous DPI 200 %, focus clavier et contraste. Aucune capture n'a été produite durant cet essai de l'agent.
