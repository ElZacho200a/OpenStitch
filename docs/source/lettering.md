# Lettrage (texte brodé)

Le lettrage transforme un texte tapé par l'utilisateur en objets vectoriels (contours des
glyphes) et en objets de broderie **éditables**, régénérés comme les autres objets. Il couvre
les fiches HP-TXT-001, 003, 004 (en partie), 007 (en partie), 009 et 013 (en partie) de la
feuille de route (`docs/roadmap-parite-hatch.md`).

## Architecture et justification (ADR lettrage)

**Décision : un objet « texte » dans le document + des lettres dérivées matérialisées.**

- `document::TextObject` (`Project::text_objects`) porte l'**intention** : texte UTF-8, police
  (référence), hauteur de capitale, espacements, interligne, alignement, crénage, origine,
  rotation, couleur, type de remplissage, largeur maximale de satin, densité.
- Les **lettres** sont des objets ordinaires : un `VectorObject` annoté `text_owner` (id du
  texte) par type de point nécessaire à la lettre, et un `EmbroideryObject` qui le suit. Elles
  passent donc par toute la chaîne existante (aperçu, sélection, ordre de couture, analyse,
  export DST) sans cas particulier.
- Les **points ne sont jamais stockés** (ADR-014) : ils sont dérivés par `effective_sequence`
  comme pour tout objet.
- Éditer le texte **remplace atomiquement** le texte et ses lettres
  (`commands::SetTextObjectCommand`) : un seul pas d'annulation, les nouvelles lettres reprennent
  la place d'ordre de couture des anciennes, l'annulation restitue exactement l'état précédent
  (retouches comprises). `RemoveTextObjectCommand` supprime le texte et toutes ses lettres.

Pourquoi matérialiser les lettres plutôt que les régénérer à chaque ouverture ?
(1) Le projet reste affichable et exportable **sans la police** (autre machine, police
désinstallée) : seule l'édition du texte est bloquée, avec un message explicite.
(2) Aucune nouvelle voie de génération parallèle : zéro risque de contourner
`effective_sequence` ou les retouches. (3) Le coût est un contour vectoriel par lettre, déjà le
régime des objets issus de l'auto-numérisation.

Contrepartie assumée : modifier le texte **ré-génère** ses lettres ; les retouches faites à la
main sur une lettre (nœuds, retouches de points) sont remplacées. Déplacer le texte entier au
canevas puis l'éditer conserve la position visible (`lettering::text_displacement`).

## Format `.osp` (schéma v6)

Clé `textObjects` (absente si le projet n'a pas de texte) et `textOwner` sur les objets
vectoriels. Tous les champs sont lus avec une valeur par défaut ; les énumérations hors plage
sont bornées. Un lecteur v5 refuse un fichier v6 (message « version plus récente ») ; un fichier
v1..v5 se charge, marqué migré (`LoadInfo::migrated`). Voir `project-format.md`.

## Bibliothèque `libs/lettering` (sans Qt)

- `Font` : police TrueType/OpenType via **FreeType** (licence FTL ; l'option GPLv2 n'est pas
  utilisée), encapsulée, jamais exposée. Contours aplatis (Béziers, tolérance 15 µm), hauteur de
  capitale mesurée sur « H », crénage de la table `kern`, `inspect_font_file` pour le catalogue.
- `layout_text` : positionnement (crénage, espacement des lettres et des mots, interligne,
  alignement gauche/centre/droite/justifié, lignes multiples, rotation). Les contours sont
  unis par la règle non nulle (`geometry::union_nonzero`) : trous des glyphes (A, O, e…) et
  composants qui se chevauchent (accents composés) sont corrects. Caractère absent de la police :
  omis et signalé (`glyphe-absent`), jamais un carré silencieux. UTF-8 invalide : U+FFFD.
- `build_text_objects` : choix du point **par morceau de glyphe** (un « ä » = corps satin +
  deux points tatami) :

| Cas | Point | Avertissement |
|---|---|---|
| Petite forme pleine sans trou (point, tréma) | tatami | non |
| Trait moyen < 1 mm | contour | `trait-trop-fin` |
| Trait moyen > largeur max satin (6 mm par défaut) | tatami | `lettre-trop-large` (si satin demandé) |
| Sinon | **auto-satin par squelette**, vérifié : couverture mesurée ≥ 92 % | — |
| Squelette insuffisant (boucles, jonctions complexes) | tatami | `satin-impossible` |

  Épaisseur moyenne de trait = 2 × aire / périmètre. Tatami / contour forcés par l'utilisateur
  sont appliqués tels quels.
- Petites lettres (HP-TXT-013, partiel) : sous 8 mm de hauteur, pas de compensation de tirage,
  pas de retrait de bord ni de sous-couche de bord en tatami ; sous-couche centrale du satin
  seulement si le trait fait au moins 1,6 mm ; contour avec des points de 1,5 mm.
- Taille minimale (HP-TXT-009) : `check_text_size` et la règle d'analyse
  `stitch_analysis::analyze_text_objects` (< 5 mm : satin fragile ; < 3 mm : illisible ;
  lettres de trait < 1 mm cousues en contour), affichée dans le panneau Analyse.

## Interface

Outil **Texte (T)** : un clic sur le canevas pose l'origine et ouvre le dialogue. Menu
**Texte** : Nouveau texte, Modifier le texte (F2), Supprimer le texte. Double-clic sur une
lettre, ou bouton « Modifier le texte… » de l'inspecteur : même dialogue. Le dialogue
(police, hauteur, couleur, type de point, alignement, espacements, interligne, crénage,
position, rotation) montre un aperçu des contours et les avertissements ; la création vérifie le
satin lettre par lettre. Polices : deux intégrées (Bitstream Vera Sans Gras et Roman,
`resources/fonts/`, licence permissive du même dossier) et les polices installées (dossiers de
polices du système, énumérés par Qt, fichier passé à la bibliothèque).

## Limites connues

- Texte sur ligne droite seulement (bases arc/cercle/chemin : HP-TXT-005, enveloppes : 006).
- Pas de HarfBuzz : crénage `kern` seulement (pas GPOS), pas de ligatures ni d'écritures
  complexes/RTL (HP-TXT-012 non traité). Les glyphes absents sont signalés.
- Pas d'édition d'une lettre isolée conservée à la régénération (HP-TXT-008), pas de crénage
  manuel par paire, pas de connecteurs optimisés entre lettres (HP-TXT-014) : l'ordre est celui
  de la lecture, le routage est celui des objets ordinaires.
- Pas de polices de broderie prénumérisées (HP-TXT-002).
- Auto-satin : les lettres à boucle fermée avec jonction (4, 6, 9, R en Vera) retombent en
  tatami avec avertissement.
- Les polices à variations (variable fonts) sont lues à leur instance par défaut.

## Tests

`tests/unit/lettering/` (police, mise en page, construction, commandes, alphabet complet A–Z,
a–z, 0–9, accents français : 100 % des lettres cousues, points dans la boîte du glyphe,
déterminisme), `tests/unit/project_io/test_text_persistence.cpp` (v5 → v6),
`tests/unit/stitch_analysis/test_text_rules.cpp`, `tests/unit/desktop/test_lettering_ui.cpp`
(catalogue, dialogue, outil, annulation).
