# Audit parité — gestion des fils V1 → OpenStitch Studio (2026-11-09)

## Constat et périmètre

Références V1 : `apps/desktop/thread_panel.{hpp,cpp}`,
`apps/desktop/main_window_threads.cpp`, `main_window_colorways.cpp`,
`thread_strip.cpp`, et les algorithmes `stitch_analysis/thread_usage.*` et
`thread_reduction.*`. Référence Studio :
`apps/desktop_v2/studio/thread_controller.*`, `qml/ThreadPanel.qml`,
`apps/desktop_v2/core/thread_plan.*`.

La première version du Studio possédait déjà les nuanciers CSV/JSON, la fusion et
recoloration de fils, les couleurs proches (CIEDE2000), l'affectation à un
fil fabricant, les coloris de base, le déplacement d'objets et l'export CSV.
Mais le dock Fils était un bloc vertical très long, sans navigation entre usages.

| Fonction V1 | Studio avant | Travail de cette PR |
|---|---|---|
| Fils distincts, fusion, recoloration, visibilité | Oui | Conservé et réorganisé |
| Catalogue, CIEDE2000, import/export de nuanciers | Oui | Ajout « Je possède » |
| « Mes fils » et références personnelles | **Absent** | JSON utilisateur, sauvegarde atomique, recherches limitées, ajout/retrait |
| Limiter l'affectation aux fils possédés | **Absent** | Proches + correspondance automatiques depuis le stock |
| Correspondance des couleurs libres en lot | **Partiel** (une commande par couleur) | **Une seule commande** (1 Ctrl+Z) |
| Réduction de palette vers N fils | **Absent** | Aperçu (avant/après/objets), confirmation, une commande annulable |
| Film couleur réel (changements de bobine) | **Absent** | Séquence effective du Studio, compteur, sélection, déplacement |
| Regrouper passages identiques | **Absent** | Confirmation explicite : avertissement sur l'empilement |
| Gestion avancée des coloris | Activation/ajout/suppression | Renommage et confirmation de suppression active désactivée |
| Tissu de fond / matière / stabilisateur / chaîne / souplesse | **Absent** | Formulaire et `SetFabricCommand` annulable |
| Onglets Projet / Catalogues / Mes fils / Film / Coloris / Tissu / Ordre | **Absent** | Navigation compacte et scroll indépendant |
| Bandeau de pastilles (ThreadStrip) drag/drop | **Absent** | Non couvert, chantier UX transversal |
| Réduction avec comparaison graphique avant/après | **Absent** | Aperçu chiffré seulement, comparaison graphique future |
| Assignation en glisser-déposer dans le canevas | **Absent** | Non couvert, tâche de gestures séparée |

## Contrats

- L'identité d'un fil réel (`ThreadKey{chart_id, code}`) et le RGB de rendu
  restent synchronisés via `SetObjectThreadCommand`; aucune mutation
  directe du document dans QML.
- Les modifications du projet (couleur, ordre, tissu, réduction) passent
  toutes par `DocumentSession`, en une commande par geste, avec annulation.
- L'inventaire personnel est **hors OSP** (`my_threads.json` dans la
  configuration du Studio) ; l'import/export .osp ne l'écrase jamais.
  Une base JSON illisible bloque les écritures plutôt que perdre la donnée.
- Les recherches limitées ne présentent que les références réellement
  possédées, dont les fils personnels ; CIEDE2000 départage les candidats.
- Le film provient de la séquence effective déjà construite pour le rendu
  Studio et non d'un second appel à `effective_sequence`. Le réordonnancement
  refuse les calques décroissants, les identifiants inconnus, les objets figés
  ou les déplacements sans effet.
- Les opérations de regroupement explicitent le changement potentiel de
  superposition ; les algorithmes de `stitch_analysis` restent l'autorité.
- Aucune référence d'une palette fictive intégrée n'est présentée comme
  une couleur fabricant vérifiée.

## Tests

`test_studio_view` : round-trip du stock, références personnelles,
limitation CIEDE2000, 1 undo pour l'association en lot, film couleur,
sélection/réordonnancement, aperçu et application de la réduction,
restauration du tissu après undo, affichage et navigation réels des onglets
dans `Main.qml`.

**Limites** : le test compile et utilise le backend Qt Quick logiciel de
CI ; le rendu de la couleur du tissu en mode réaliste suit encore la
préférence de rendu et devra être synchronisé dans un chantier de rendu
dédié. Le déplacement à la souris des pastilles de fil de V1 n'est pas
reproduit par cette PR ; les actions accessibles par boutons sont conservées.
