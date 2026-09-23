# Combler les écarts P0 de parité avec Hatch Embroidery

## Objectif

OpenStitch Studio vise la parité fonctionnelle avec Hatch Embroidery (Wilcom).
`docs/roadmap-parite-hatch.md` est la liste de référence, tenue à jour à partir
du code, qui documente tout ce qui manque ou reste partiel. Cet objectif
couvre le sous-ensemble **priorité P0** de cette roadmap : ce sont les écarts
jugés bloquants pour un usage sérieux du logiciel (formats machine courants,
gestion des fils, texte, moteur de points, édition d'objets, production,
performance, distribution).

Pour chaque entrée traitée : lire son bloc complet dans
`docs/roadmap-parite-hatch.md` (état actuel, comportement Hatch attendu,
modules concernés, dépendances, critères d'acceptation) avant de coder, et
mettre à jour le statut de l'entrée (☐ → ☑, avec la date) une fois les
critères d'acceptation vérifiés par les tests.

## Backlog P0 restant (voir docs/roadmap-parite-hatch.md pour le détail de chaque entrée)

Formats machine :
- HP-FMT-001 — Couche de normalisation machine séparée des codecs
- HP-FMT-002 — Export PES (Brother / Babylock / Bernette)
- HP-FMT-003 — Import PES
- HP-FMT-004 — JEF / JEF+ (Janome, Elna) lecture + écriture
- HP-FMT-005 — EXP (Melco / Bernina) lecture + écriture

Fils et couleurs :
- HP-THR-001 — Bibliothèque thread_palette
- HP-THR-002 — Nuanciers des fabricants
- HP-THR-003 — Fil le plus proche (distance perceptuelle)
- HP-THR-004 — Fil assigné à chaque objet + remplacer une couleur partout
- HP-THR-005 — Film couleur / barre des couleurs du design

Texte :
- HP-TXT-001 — Objet texte dans le modèle
- HP-TXT-003 — Conversion de polices TrueType/OpenType
- HP-TXT-004 — Lettrage satin automatique par lettre

Moteur de points :
- HP-STI-004 — Satin de bordure à largeur fixe le long d'un chemin
- HP-ENG-001 — Compensation d'étirement du tatami
- HP-ENG-002 — Sous-couche automatique selon la forme
- HP-ENG-008 — Longueurs min/max appliquées à tous les générateurs (partiel)
- HP-ENG-010 — Entrée et sortie automatiques au plus proche (partiel)

Auto-numérisation :
- HP-AUTO-001 — Qualité de l'auto-numérisation comparable à Hatch (partiel)
- HP-AUTO-003 — Clic-pour-broder / baguette magique
- HP-AUTO-009 — Choix du type satin / tatami fiable (partiel)

Vectorisation et édition d'objets :
- HP-VEC-002 — Tracé ouvert (ligne, polyligne, courbe ouverte)
- HP-OBJ-001 — Sélection multiple
- HP-OBJ-002 — Couper / copier / coller
- HP-OBJ-003 — Déplacement au clavier
- HP-OBJ-004 — Rotation libre d'objets
- HP-OBJ-006 — Miroir horizontal / vertical
- HP-OBJ-014 — Transformer le design entier
- HP-OBJ-016 — Suppression multiple (partiel)
- HP-OBJ-018 — Changer la couleur d'un objet de broderie

Séquençage et rendu :
- HP-SEQ-003 — Séquencer par couleur depuis le film couleur
- HP-VIEW-001 — Rendu réaliste des points

Production et fichiers :
- HP-PROD-001 — Fiche de production imprimable
- HP-FILE-003 — Fichiers récents
- HP-FILE-004 — Sauvegarde automatique et récupération après plantage

UX, perf, distribution, i18n :
- HP-UX-006 — Progression et annulation des opérations longues
- HP-I18N-001 — Infrastructure de traduction
- HP-I18N-002 — Anglais complet
- HP-PERF-001 — Tâches en arrière-plan avec progression et annulation
- HP-PERF-002 — Régénération incrémentale par objet
- HP-DIST-001 — Installateur Windows signé et publié (partiel)
- HP-PHYS-001 — Protocole de validation sur machine

(HP-FILE-001 et HP-FILE-002 sont déjà faits au 2026-09-23, à ne pas reprendre.)

## Contraintes non négociables (voir GUARDRAILS.md et CLAUDE.md)

- Apache-2.0 uniquement : aucun code GPL/AGPL lié ou copié.
- Le cœur (libs/) ne dépend jamais de Qt ni d'aucun code GPL — vérifié par le
  build Linux `linux-core` (cœur + CLI, sans Qt) en CI.
- Coordonnées en micromètres entiers.
- Toute mutation du document passe par une commande `ICommand` (undo/redo).
- Les points de broderie ne sont jamais stockés : ils sont dérivés via
  `effective_sequence`, jamais consommés directement via `generate_sequence`
  en dehors de ce chemin.
- Déterminisme : mêmes entrées → mêmes points, toujours.
- Une fonctionnalité Hatch qui imposerait de violer une de ces règles se fait
  autrement, ou pas du tout — noter la déviation dans l'entrée de roadmap
  concernée plutôt que de l'ignorer silencieusement.
