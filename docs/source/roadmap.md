# Roadmap

Public : mainteneur, contributeur. Vue d'ensemble **courte** ; **aucune date n'est annoncée**.
Revue le 2026-10-06 après un audit complet contre le code.

Trois documents, un rôle chacun — ne pas dupliquer leur contenu ici :

| Question | Document |
|---|---|
| Qu'est-ce qui manque par rapport à Hatch, et où en est chaque entrée (☐ ◐ ☑) ? | `docs/roadmap-parite-hatch.md` — **source de vérité des statuts** (256 entrées HP-*, tableau de bord en tête) |
| Dans quel ordre le livrer (vagues, scopes S1…S15, dépendances, plans) ? | `specs/implementation-roadmap.md` |
| Où va le projet, en gros ? | ce chapitre |

## Déjà livré (depuis la roadmap d'origine)

- Auto-satin par squelette et traversées orientées (`satin-squelette.md`, branche de recherche brevets, **non fusionné sur `main`**, à essayer sur machine) ;
- Moteur satin topologique historique (supprimé, remplacé ci-dessus) : réseaux Y/T/anneaux, guides éditables, jonctions ancrées,
  sous-couches, underpath caché, coupes et points d'arrêt, filtre de points courts
  (`satin.md`, `tatami.md`, `auto-numerisation.md`).
- Auto-numérisation durcie (Lots A à G : fond présumé, angles et sous-couches des
  remplissages, chevauchement, fragments, finitions, métriques) et mode **Contours / Line
  Art** (traits → running/satin par couleur, curseur de détail, 2026-10-05).
- Fichiers : Nouveau projet, Enregistrer / Enregistrer sous, fichiers récents, sauvegarde
  automatique avec récupération après plantage.
- Bibliothèque de fils (`thread_palette`) avec distance perceptuelle ; nuanciers fabricants
  encore partiels (données placeholder).
- Distribution : installateur Windows (Inno Setup), workflow de release sur tag, release
  « latest » publiée par la CI — **installateur non signé**.

## En cours

- **PR #5** — couche de normalisation machine (HP-FMT-001), préalable à PES/JEF/EXP.
- **PR #3** — plan de code de la sélection, du presse-papiers et des transformations (S3).
- **Satin guidé** : restent la propagation géométrique coordonnée des angles et des
  déplacements de guides sur un réseau, puis les retours textiles suivant le réseau plutôt
  qu'un segment direct. Ne jamais accepter silencieusement une gerbe ou un croisement comme
  satin valide.

## Court terme (P0 restants)

Formats machine (PES, JEF, EXP) · fils par objet et film couleur · sélection multiple,
presse-papiers, rotation, miroir · tracés ouverts et satin de bordure (outils) · lettrage ·
longueurs min/max appliquées à tous les générateurs · tâches de fond avec annulation ·
rendu réaliste des points · fiche de production · installateur signé · protocole de
validation sur machine réelle · jeu de référence et métriques pour l'auto-numérisation.
La liste exacte et l'ordre sont dans les deux documents ci-dessus.

## Moyen et long terme

- Remplissages avancés (concentrique, spirale, radial, motifs), édition de points complète,
  simulateur, profils machine/cadres/tissus, appliqué, bibliothèques de motifs.
- Autres formats (VP3, HUS, …), import SVG/DXF plus complet, migrations `.osp` systématiques.
- Internationalisation (infrastructure puis anglais) : volontairement **en toute fin**, un
  seul balayage `tr()` une fois le reste fonctionnel.
- Portage macOS et publication publique.

## Sources

- `docs/phase0/08-roadmap-adr.md` — roadmap d'origine (13 phases), **historique**.
- `docs/stitch-engine-audit.md` — défauts et priorités du moteur de points.
- `docs/roadmap-parite-hatch.md`, `specs/implementation-roadmap.md` — état courant.

## Implémentation associée

Les fonctionnalités « Non implémenté » du chapitre *Limitations* constituent la liste de
travail ; chacune indique le module cible.
