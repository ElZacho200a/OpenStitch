# Roadmap

Public : mainteneur, contributeur. Cette roadmap est construite à partir du code
existant, des documents de conception (`docs/phase0/`, `docs/stitch-engine-*.md`)
et des fonctionnalités manquantes identifiables. **Aucune date n'est annoncée.**

## Court terme (dette et robustesse)

- **Priorité immédiate après le Lot 8.2 — qualité satin et orientation guidée** :
  le pipeline utilise maintenant le moteur topologique et représente les Y/T
  et anneaux comme un réseau de sections `SatinParams` rétrocompatibles. Les
  fixtures bande/T/anneau/tentabrode verrouillent cette base. Continuer à
  durcir les arcs très asymétriques (progression bilatérale et métriques de
  stagnation), puis ajouter plusieurs guides
  de direction éditables par zone satin : chaque guide relie les deux rails et
  impose une orientation locale, interpolée continûment entre guides. **Premier
  incrément livré** : affichage des guides, déplacement indépendant des deux
  extrémités avec projection/validation monotone, undo/redo et test Qt de
  glisser. **Deuxième incrément livré** : sélection explicite sur le canevas,
  ajout dans le plus grand intervalle admissible, suppression avec plancher de
  deux guides, ordre de routage préservé et parcours QTest complet. Restent les
  guides coordonnés aux jonctions du réseau multi-rail. Ne jamais accepter
  silencieusement une gerbe ou un croisement comme satin valide.

- Refonte avancée du **satin** (§12 de l'étude) : correspondance par sections,
  barreaux de direction, densité perpendiculaire au fil, gestion des points
  courts dans les virages, split stitch pour les colonnes larges, terminaisons.
- Refonte avancée du **tatami** (§15) : underpath **caché** au lieu de sauts,
  points d'entrée/sortie imposés, sous-couches, motifs de phase.
- Séparer la **normalisation machine** de l'encodeur DST (préparer PES/JEF/EXP).

## Moyen terme (fonctionnalités)

- **Palette de fils** (`thread_palette`) : base fabricants, distance perceptuelle,
  association objet↔bobine, import/export.
- **Édition manuelle des points** (déplacer, convertir en saut, ajouter une
  coupe) avec états `Clean`/`Dirty`/`ManuallyEdited`.
- **Sous-couches** paramétrables (contour, zigzag) pour satin et tatami.
- **Compensation** directionnelle et profils tissu/stabilisateur/fil.
- **Édition de nœuds** complète (ajout/suppression, tangentes, anguleux/lisse).

## Long terme (avancé)

- **Auto-satin** : poursuivre au-delà du réseau de sections maintenant branché
  dans l'auto-numérisation (jonctions textiles avancées, réseaux à plusieurs
  trous, validation machine, guides d'orientation par section). Les formes
  refusées retombent sur le tatami ; le satin naïf reste désactivé.
- **Remplissages avancés** : concentrique, spirale, radial, guidé (champ de
  direction), motifs.
- **Autres formats** : PES, JEF, EXP, VP3.
- **Tâches asynchrones** (annulation, progression) et **carte de densité**.
- **Profils machine/cadres**, sauvegarde automatique, migrations de projet.
- Portage **macOS**, distribution de binaires, publication publique.

## Sources

- `docs/phase0/08-roadmap-adr.md` — roadmap d'origine (13 phases).
- `docs/stitch-engine-audit.md` — défauts et priorités du moteur de points.

## Implémentation associée

Les fonctionnalités « Non implémenté » du chapitre *Limitations* constituent la
liste de travail. Chacune indique le module cible.
