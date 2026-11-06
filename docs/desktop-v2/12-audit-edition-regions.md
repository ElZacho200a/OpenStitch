# 12 — Audit de l'édition des régions (espace Zones)

État au 2026-11-06. Audit par lecture du code (`core/region_edit.*`, `ui/region_edit_controller.*`,
`ui/region_panel.*`, `ui/shell_window.cpp`, `ui/preview_canvas.cpp`). Rien n'a été jugé à l'écran : les
constats d'ergonomie viennent de ce que le code permet ou interdit, pas d'un essai utilisateur.

## Ce qui existe

Un outil « Régions » (X) et un « Pinceau de frontière » (U), un panneau (liste, supprimer, fusionner dans une
voisine, recolorer, fusionner deux couleurs, nettoyer les îlots, boucher les trous, rayon du pinceau,
régénérer). Chaque correction est une commande annulable ; le moteur (`region_edit`) est solide et testé.

## Constats

### A. Sélection (le plus pauvre)

| # | Constat | Gravité |
|---|---|---|
| A1 | **Une seule région à la fois** (`selected_` est un entier). Ni Maj/Ctrl+clic, ni rectangle, ni lasso. | P0 |
| A2 | La sélection est **privée au contrôleur** : elle n'est pas dans le `SelectionModel` commun (le type `TargetKind::Region` existe mais n'est jamais alimenté). L'inspecteur, les calques et l'arbre l'ignorent. | P0 |
| A3 | **Aucun lien région ↔ objet** : impossible d'aller de la région à l'objet de broderie qu'elle a produit, ni l'inverse. | P0 |
| A4 | Pas de survol : on ne voit la région visée qu'après le clic. | P1 |
| A5 | Pas de sélection par couleur (« toutes les régions de ce fil »), ni de « sélectionner les voisines », ni de baguette magique. | P1 |
| A6 | Liste : « Région 12 — 340 px ». Ni nom, ni vignette de forme, ni surface en mm², ni filtre, ni tri. | P1 |
| A7 | Aucune navigation au clavier entre régions (Tab, flèches), ni Échap pour désélectionner. | P1 |
| A8 | Cliquer le fond désélectionne sans retour clair ; aucun « tout sélectionner / inverser ». | P2 |

### B. Raccourcis clavier

| # | Constat | Gravité |
|---|---|---|
| B1 | **Aucune commande `region.*` dans la palette** (Ctrl+K) : supprimer, fusionner, recolorer, nettoyer, boucher, régénérer ne sont atteignables qu'à la souris. | P0 |
| B2 | Aucun raccourci clavier propre aux régions. En particulier **Suppr** ne supprime pas la région choisie (il vise les objets de broderie). | P0 |
| B3 | Pas de `[` / `]` pour le rayon du pinceau, ni de molette+modificateur. | P1 |
| B4 | Seuls X et U existent. Aucune touche pour « basculer pinceau / gomme » (il faut tenir Ctrl). | P1 |

### C. Outils et gestes

| # | Constat | Gravité |
|---|---|---|
| C1 | **Pas de curseur de pinceau** (cercle au rayon réel) : on peint sans voir la taille. | P0 |
| C2 | Fusion uniquement via une liste déroulante de voisines ; pas de « cliquer la région cible au canevas ». | P1 |
| C3 | « Supprimer » absorbe dans la voisine principale sans choix. | P1 |
| C4 | Pas d'outil lasso / polygone pour (re)définir une zone, ni de création d'une nouvelle région, ni de découpe d'une région en deux. | P1 |
| C5 | Le pinceau a un rayon et rien d'autre (pas de dureté, pas de remplissage au seau, pas de mode « ne touche que le fond »). | P2 |
| C6 | Le Pot de peinture (G) est déclaré mais non migré. | P1 |

### D. Retour visuel et état

| # | Constat | Gravité |
|---|---|---|
| D1 | La carte des régions **remplace** les fils à l'écran : pas de transparence réglable, pas de bascule rapide carte / broderie, pas de contours de régions. | P1 |
| D2 | Pas d'« isoler » (n'afficher que la région choisie). | P2 |
| D3 | Après correction, les objets ne suivent pas : on voit seulement une bannière. Aucun aperçu de ce que « Régénérer » produira. | P1 |
| D4 | « Régénérer » remplace les objets : les retouches manuelles sont perdues (annulable, mais sans avertissement avant). | P1 |

### E. Cohérence avec le reste

| # | Constat | Gravité |
|---|---|---|
| E1 | Pas d'opération `project_ops` pour les corrections de régions : ni la CLI ni l'agent n'y accèdent (la matrice le note 🟡). | P1 |
| E2 | Aucun test ne vérifie le raccourci clavier ni le parcours « sélectionner plusieurs, agir ». | P1 |

## Ordre de correction proposé

1. **Sélection partagée** : `Region` dans le `SelectionModel`, multi-sélection (Maj/Ctrl, rectangle), survol,
   Échap, Tab / flèches, sélection par couleur, lien région ↔ objet (A1-A7).
2. **Commandes et raccourcis** : `region.delete` (Suppr dans l'outil), `region.merge`, `region.recolor`,
   `region.clean`, `region.holes`, `region.regenerate`, `region.select-color`, `region.select-neighbours`,
   `region.invert`, `[` `]`, `Maj` pour la gomme — toutes dans la palette (B1-B4).
3. **Curseur de pinceau** et bascule carte / broderie avec opacité (C1, D1).
4. Fusion et suppression au clic canevas, choix de la cible (C2, C3).
5. Lasso / découpe / nouvelle région (C4), aperçu de la régénération (D3, D4).
6. Opérations `project_ops` (E1) et tests de parcours (E2).
