# Stratégie d'évolution de l'interface

Principes d'organisation, changements structurants proposés, arbitrages et lots de réalisation.
S'appuie sur le [rapport d'audit](audit-ux-ui-2026-10-18.md) et les
[parcours](parcours-utilisateur.md). Les tâches sont dans `docs/roadmap-parite-hatch.md` §20
(`HP-UX-017…044`, `HP-QA-008`, `HP-PERF-009`).

**Posture** : évolution progressive, pas de refonte. L'interface actuelle a de vrais acquis
(navigation, segmentation non modale, panneaux, état « modifié », récupération). La refonte
précédente (`docs/ui-redesign-*.md`) a posé des tokens, un inspecteur et des barres : on corrige
la **cohérence des interactions** et l'**exploitation de l'espace**, on ne repart pas de zéro.

## 1. Principes

1. **Un geste répété n'ouvre pas de dialogue.** La variante qui règle est sur Maj+ ou une entrée
   « avec réglages… ».
2. **Un seul chemin par intention** : bouton, raccourci et entrée de menu partagent la même
   `QAction`.
3. **Aperçu avant engagement.** Les réglages s'appliquent en direct, **hors fil UI** ; les modales
   ne servent qu'aux choix coûteux ou irréversibles.
4. **Échap remonte d'un niveau** (mode, outil, objets, régions) ; il ne ferme pas tout.
5. **Le danger se dit avant** : retouches manuelles qui seront invalidées, export avec risques.
6. **Découverte progressive** : essentiels visibles ; avancé replié et mémorisé ; expérimental
   dans un menu « Avancé ». Pas d'assistant bloquant : les experts gardent leurs raccourcis.
7. **Une règle de « saisissable » unique** : le même prédicat (visible, filtre, tolérance de clic)
   pour le clic, Maj/Ctrl/Alt, le rectangle, la liste et le clavier.
8. **Budget d'écran** : à 1366 × 768 en 150 % (≈ 911 × 480 logiques), le canevas garde au moins
   450 px de large et 260 px de haut. Ce budget devient un test (`HP-QA-008`).

## 2. Organisation des panneaux

### Aujourd'hui (mesuré/lu)

Fenêtre minimale **523 × 441** docks par défaut ; à 1024 × 640 le canevas tombe à **360 × 421** et
la palette déborde dans `»`. Cinq rangées permanentes (menu, barre principale, barre contextuelle,
barre de simulation, bande des fils) + barre d'état + règles ≈ 255 px ; à droite, cinq docks
empilés (Propriétés/Filtres en onglets ; Analyse, Historique, Fils, Couleurs séparés).

### Proposé

```
Écran large (≥ 1280 logiques)           Portable (< 1000 logiques, mode « portable »)
+--------------------------------+      +-------------------------------------------+
| Menu                           |      | Menu                                      |
| [Barre principale + contexte]  |      | [Barre principale + contexte]   [Exporter]|
+--+----------------------+------+      +--+---------------------------+-----------+
|O |                      |Doc.  |      |O |                           | Document  |
|u |       CANEVAS        |Prop. |      |u |         CANEVAS           | Propriétés|
|t |   (règles, Tab=plein)|Filtre|      |t |   (>= 450 x 260 garanti)  | Ordre ... |
|i |                      |------|      |i |                           | (1 colonne|
|l |                      |Qualité|     |l |                           |  en onglets)
|s |                      |Fils..|      |s |                           |           |
+--+----------------------+------+      +--+---------------------------+-----------+
| Simulation + fils (1 rangée,    |      | Simulation + fils (1 rangée, si séquence) |
|  visible si une séquence existe)|      | État : geste ........ curseur | Accroche  |
| État : geste ...... curseur     |      +-------------------------------------------+
+--------------------------------+
Docks à la demande (Analyse, Historique, Fils, Couleurs) : tabifiés ensemble, jamais empilés.
Palette d'outils : deux colonnes sous 480 px de haut ; Terminer/Annuler la forme dans la
barre contextuelle pendant un tracé.
```

Changements, par rapport coût/effet : docks en onglets (`HP-UX-024`), défilement des docks Fils
et Couleurs (`HP-UX-024`), fusion simulation + fils et densité compacte automatique
(`HP-UX-026`), mode une colonne (`HP-UX-027`), dialogues bornés à l'écran (`HP-UX-025`).

## 3. Refontes évaluées : simplifient-elles vraiment ?

Pour chaque changement important : ce qu'il simplifie, **ce qu'il ne fait que déplacer**, et la
décision.

| Changement | Simplifie | Déplace la complexité vers | Décision |
|---|---|---|---|
| **Création directe + inspecteur en direct** (HP-UX-002, 028) | supprime 3 modales et un clic par objet | l'inspecteur, qui doit être bon dès la création ; **régénération synchrone** → dépend de `HP-PERF-009` | **Oui**, option « Créer avec réglages… » conservée (décision 2026-10-18) |
| **Mode « Remodeler » unique** (HP-UX-001) | 5 modes à activer par menu | un répartiteur par type d'objet, tests de modes | **Par étapes** : d'abord double-clic pour entrer + Échap d'un niveau (S) ; la fusion complète des modes n'est faite que si ces deux gestes ne suffisent pas |
| **Fusion Document + Ordre de couture** (HP-UX-037) | deux sources pour le même rang | une liste plus riche (colonnes, glisser-déposer, sélection étendue) | **Oui**, mais après `HP-OBJ-001/010` ; garder « Optimiser… » séparé |
| **Panneau « Qualité » unique** (HP-UX-031) | 5 chemins (F5, F9, 3 menus) | risque de panneau « fourre-tout » | **Par étapes** : ouverture automatique du dock Analyse existant d'abord ; onglets ensuite |
| **Bascule de vue à 4 états** (HP-UX-036) | symétrie Dessin/Points/Réaliste/Simulation | masque la composition libre des couches | **Oui en ajout** : le menu Calques reste, la bascule est un raccourci |
| **Mode portable une colonne** (HP-UX-027) | canevas utilisable à 911 px | deux dispositions à tester | **Oui**, mais déclenché par une action et une détection d'écran, pas imposé |
| **Verrou d'édition d'objet** (HP-UX-034) | protège un travail fini | un champ persistant `.osp` + migration | **Écarté** (décision) : mode « Isoler » de session, sans changement de format |
| **Dialogues de création supprimés** | clics en moins | rien de caché si « avec réglages… » reste | **Oui** |

### Écarté volontairement

- **Assistant pas à pas obligatoire** : ralentit les experts ; `HP-UX-012` reste un parcours
  *optionnel* et non bloquant.
- **Profils « Débutant / Expert »** : doublent les chemins à tester. La divulgation progressive
  (sections repliées mémorisées, menu « Avancé ») suffit.
- **Nouveau thème, animations, réécriture des panneaux** : aucun constat ne les demande.
- **Changement du moteur de points, des formats ou de `.osp`** : hors périmètre ; aucune tâche
  n'en a besoin.

## 4. Lots de réalisation

| Lot | Contenu | Tâches | Priorité | Dépend de |
|---|---|---|---|---|
| **0 — Garde-fou** | tests de taille minimale (fenêtre, docks, dialogues) | HP-QA-008 | P1 | — |
| **A — Quick wins et gestes** | Ctrl+O, Échap, sélection (prédicat + tolérance + cyclage), F7, sortie de simulation, raccourcis couches/zoom | HP-UX-017 à 023 | P1/P2, tailles S | — |
| **B — Responsive et panneaux** | docks, dialogues bornés, barres, densité auto, mode portable | HP-UX-024 à 027 | P1/P2, M | HP-QA-008 |
| **C — Workflows de broderie** | création directe, type 1/N formes, retouches, valeurs mixtes, régénération différée, numérisation, qualité, export, cadre | HP-UX-002, 003, 028 à 033, 038, HP-PERF-009 | P1/P2, M/L | HP-QA-001 (partiel), HP-PERF-001 |
| **D — Cohérence et productivité** | Isoler, style copier/coller, bascule de vue, liste Document, contrat d'état | HP-UX-034 à 037, 039 | P2, M/L | lot C, HP-OBJ-001 |
| **E — Finitions** | barre Fils clavier, icônes de type, DPI des pastilles, import anisotrope | HP-UX-041 à 044 | P2/P3, S | — |

**Ordre recommandé** : 0 → A en parallèle de B (fichiers disjoints) → C → D → E. Le lot A et
`HP-QA-008` apportent le plus de confort pour le moins de risque.

## 5. Garde-fous

- Aucune tâche ne modifie `libs/stitch_generation`, `libs/formats` ni le schéma `.osp`
  (exception à confirmer : `HP-UX-044` touche `libs/document/image_placement.cpp`, à n'engager
  qu'en conservant la compatibilité des projets existants).
- Toute mutation du document reste une `ICommand` ; une édition de groupe = **une** commande.
- Tout nouveau texte passe par `tr()` (pas de concaténation) pour ne pas aggraver `HP-I18N-002`.
- Un changement de `app_theme.cpp` s'accompagne d'un test de contrat QSS clair/sombre.
- Un calcul déplacé hors fil UI garde une sortie **octet pour octet identique** au calcul
  synchrone et passe par `effective_sequence`.
- Chaque tâche livrée est câblée dans `apps/desktop` avec un test QTest headless (règle du dépôt).

## 6. Non planifié (à décider)

- **Réduction de couleurs à 5 endroits** (PAR-16) : un point d'entrée « Palette » unique est
  souhaitable mais touche segmentation, fils et numérisation ; à cadrer avec `HP-THR-*` avant de
  planifier.
- **Étude utilisateurs formative** (≈ 5 personnes, tâches : premier motif, SVG, texte, réglage,
  avertissement, récupération) : nécessaire pour valider les lots C et D, hors périmètre de ce
  dépôt.
- **Validation d'accessibilité assistée** (NVDA/UIA) : prévue par `HP-I18N-004`.
