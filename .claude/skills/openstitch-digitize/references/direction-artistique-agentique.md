# Direction artistique agentique : image préparée vers plan textile

## Quand utiliser ce protocole

Quand une image est déjà préparée (par GPT Image ou autrement) et que l'agent doit **prendre des décisions artistiques** plutôt que seulement reproduire une segmentation : choix des matières, volumes, découpage, directions de fils, satin/tatami, superpositions et détails à préserver. Ce protocole complète les phases A–D du skill principal ; il ne remplace ni les outils MCP ni les contrôles physiques.

## Contrat de travail (portes vérifiables)

1. **Observation** : récupérer source et dimension cible, inspecter le projet existant via `project_inspect` et les previews via `project_render` si disponibles. Énoncer les limites de ce qui est visible. Ne jamais prétendre avoir vu une image si aucun rendu n'a été reçu.
2. **Lecture plastique** : décrire sujet focal, silhouettes, hiérarchie, masses, lumière, volumes, textures, espaces de tissu volontairement nus. Identifier au moins trois détails structurants à protéger et les éléments pouvant être simplifiés ; ne pas déduire les objets de la couleur seule.
3. **Plan textile par région** : préparer le plan de décision ci-dessous **avant** toute mutation. Définir le rôle visuel, l'ordre/occlusion, la famille de points candidate, les directions et leurs transitions, l'échelle physique et les risques. Les guides de satin et directions sont des hypothèses à vérifier sur géométrie réelle.
4. **Critique contradictoire du plan** : chercher ce qui pourrait devenir plat, illisible, trop dense ou physiquement fragile. Réviser d'abord segmentation, volume ou ordre plutôt que compenser systématiquement par la densité. Expliquer les compromis.
5. **Exécution** : mapper les régions aux IDs disponibles via `semantic_map`, `project_inspect` et `project_select`. Vérifier les capacités réelles avec `project_operations`. Sur projet live, mutations avec approbation utilisateur ; employer `dry_run` quand supporté. Ne jamais inventer une opération MCP. Utiliser `semantic_validate` avant import/remplacement quand pertinent.
6. **Rétroaction visuelle** : `project_render` pour comparer à la source et au plan, idéalement avec recadrages à échelle comparable ; `project_report`, `project_check`, `project_fabric_risk` et `project_satin_compare` lorsque disponibles pour des preuves complémentaires. Distinguer observation artistique de mesure du moteur.
7. **Révision ciblée** : constituer une liste priorisée de défauts liés à des IDs stables, corriger une cause à la fois sur copie/checkpoint lorsque possible, réafficher et comparer. `semantic_replace` ou `project_apply` selon le cas ; ne pas relancer toute la numérisation pour une correction locale réalisable. S'arrêter au plus tard après trois cycles sans gain démontré et présenter les désaccords restant à arbitrer.
8. **Livraison** : rendre le plan initial, les décisions modifiées, le bilan avant/après, les preuves/limites, et les essais physiques recommandés. Ne pas annoncer « brodable » sur une seule preview.

### Format de plan artistique (document de raisonnement, PAS une commande MCP)

Le JSON ci-dessous sert d'artefact d'échange. Ses champs ne sont **pas** automatiquement acceptés par `project_apply` ou `semantic_import` ; traduire chaque intention vers leurs schémas réels après inspection du registre des opérations.

```json
{
  "schema": "openstitch.art_direction/1",
  "source": {"kind": "prepared_image", "width_mm": 100},
  "intent": "Préserver la silhouette et suggérer le relief par la direction du fil",
  "protected_details": ["contour du sujet", "séparation des plans", "détail focal"],
  "regions": [
    {
      "semantic_id": "subject.body",
      "visual_role": "volume principal",
      "layer": "foreground",
      "occlusion": {"over": [], "under": []},
      "technique_candidates": ["tatami"],
      "direction_intent": "Suivre la courbure du volume, transition douce au changement de plan",
      "subdivision": "Séparer en deux zones si la direction doit changer",
      "why": "Éviter un grand aplat de points uniforme",
      "technical_risks": ["densité aux raccords"],
      "confidence": "medium",
      "status": "proposed"
    }
  ],
  "open_questions": [],
  "preview_review": {"observed": false, "issues": [], "next_changes": []}
}
```

Ne jamais remplacer des IDs du projet par des noms supposés : `semantic_id` est un identifiant logique à relier explicitement aux vrais objets, qui peuvent être absents. Les `technique_candidates` sont des propositions et doivent être validées par les contraintes locales (largeur, étranglements, trous, branches, support, machine). Une direction de fil peut nécessiter plusieurs régions plutôt qu'un angle unique.

## Grille de critique après preview

Pour chaque élément jugé insatisfaisant, enregistrer **objet(s) concernés, observation, cause probable, correction locale et preuve attendue** :

- Lisibilité : la silhouette et les détails protégés survivent-ils à la taille réelle ?
- Relief : les directions soulignent-elles les volumes ou créent-elles des bandes artificielles ?
- Matière : le satin valorise-t-il un bord, une nervure ou un objet étroit, plutôt que couvrir arbitrairement une grande masse ?
- Composition : les superpositions et réserves de tissu restent-elles cohérentes ?
- Technique : largeur satin, collisions, sous-couches, densité, compensation, longueur de points, coupes et risque textile sont-ils acceptables selon les mesures disponibles ?
- Fidélité : la simplification est-elle voulue et documentée, sans effacer des objets essentiels ?

Un score esthétique n'est pas une mesure objective de brodabilité. En cas de conflit, rapporter les deux diagnostics, conserver la version précédente et demander un arbitrage si nécessaire.

## Compétences à mobiliser à la demande

- **Composition** : hiérarchie, identité des objets, silhouette et simplification intentionnelle.
- **Planification textile** : techniques, directions, guides, découpe, occlusions (réutiliser `directions-et-planification-textile`, `selection-satins`, `parametres`).
- **Critique et reprise** : previews, diagnostic, variantes, corrections régionales (réutiliser `retouches-et-validation`, `diagnostic`).

Ce sont des **étapes du même agent**, pas trois serveurs ou agents séparés. Le MCP actuel diffuse ce sujet via `digitize_guide(topic="direction-artistique-agentique")` et `openstitch://guide/direction-artistique-agentique` après recompilation. Claude Code lit également ce fichier dans le dépôt. Aucun moteur génératif d'image supplémentaire n'est nécessaire pour une image d'entrée déjà préparée.

## Scénarios d'acceptation à vérifier manuellement

1. Image plate avec plusieurs volumes de même couleur : l'agent propose des objets distincts et motive ses directions ; il ne fusionne pas uniquement au RGB.
2. Ruban courbe et grande masse : satin envisagé pour le ruban après vérification de largeur, tatami directionnel pour la masse ; comparaison de satins si pertinente.
3. Preview montrant un défaut limité : l'agent cible un objet et propose une correction locale, conserve les autres et compare avant/après.
4. Projet live : aucun changement sans approbation et aucune affirmation d'exécution lorsque l'outil refuse une opération.
5. MCP autonome : `digitize_guide` découvre le nouveau sujet, et la copie embarquée correspond au fichier source selon le test existant.

Les résultats de ces scénarios doivent être effectivement observés avant de marquer la fonctionnalité comme validée.
