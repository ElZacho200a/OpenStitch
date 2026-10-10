# Palettes et fils

Public : utilisateur avancé, mainteneur. État : **Prévu / partiel**.

## Ce qui existe

- Chaque objet de broderie porte une **couleur RGB** (`std::array<uint8_t,3>`),
  reprise de la région d'origine.
- Un **changement de couleur** est inséré automatiquement entre deux objets
  consécutifs de couleurs différentes lors de la génération.
- La recoloration d'une région est disponible (choix d'une couleur libre).
- **Catalogue de fils** (`libs/thread_palette`, `openstitch::thread_palette`) :
  bibliothèque cœur sans Qt ni dépendance tierce, ne liant que
  `openstitch::core` (C-S1-01). Voir *Catalogue* ci-dessous.

## Catalogue (`libs/thread_palette`)

Types valeur (`include/openstitch/thread_palette/thread.hpp`) :

- `ThreadKey { chart_id, code }` — identité stable d'un fil, comparable et
  ordonnable, sans pointeur vers le nuancier. Destinée à être embarquée par un
  futur consommateur (`stitch`/`document`/`formats`/`stitch_analysis`, hors
  périmètre S1) sans tirer le registre entier.
- `Thread { key, brand, range, name, rgb }` — une entrée d'un nuancier.
- `ThreadChart { chart_id, display_name, source_note, threads }` —
  `source_note` porte la traçabilité du sourçage (fabricant, gamme, URL, date
  de consultation), en dur dans le binaire.

Registre (`include/openstitch/thread_palette/catalog.hpp`) :

- `all_charts()` — tous les nuanciers compilés, dans l'ordre textuel des
  appels de `src/catalog.cpp` (déterministe entre deux exécutions et entre
  plateformes).
- `find_chart(chart_id)` — `nullptr` si l'identifiant est inconnu.
- `find_by_code(chart_id, code)` — `std::optional<Thread>` vide si le
  nuancier ou le code sont inconnus.
- `search_by_name(needle)` — sous-chaîne insensible à la casse, résultats dans
  l'ordre du registre.

Deux nuanciers de fabricant sont chargés aujourd'hui, dans cet ordre de
déclaration : **Madeira Polyneon** (`madeira_polyneon`) puis **Isacord 40**
(`isacord_40`).

> **DONNÉES PLACEHOLDER.** Les deux fichiers de données
> (`libs/thread_palette/data/madeira_polyneon.cpp`,
> `libs/thread_palette/data/isacord_40.cpp`) contiennent actuellement des
> entrées **inventées** (codes, noms, RGB plausibles mais fictifs), pas une
> transcription du vrai nuancier publié par le fabricant : l'environnement
> d'implémentation initial n'avait pas d'accès web pour sourcer les valeurs
> réelles selon S1-POLICY-1. Chaque `source_note` le dit explicitement et
> pointe vers la page officielle à consulter. **À faire avant toute
> utilisation hors développement/test** : remplacer ces deux fichiers par une
> transcription réelle depuis la carte de couleurs officielle de chaque
> fabricant.

Procédure pour ajouter un nuancier (S1-POLICY-1/2/3) :

1. Sourcer uniquement depuis la page produit ou la fiche technique **publiée
   par le fabricant lui-même** — jamais depuis une table déjà agrégée par un
   logiciel ou plugin tiers. Seuls les faits (code, nom commercial, RGB
   publié) sont repris.
2. Ajouter `libs/thread_palette/data/<nuancier>.cpp`, qui définit une factory
   `detail::make_<nuancier>_chart()` déclarée dans `src/chart_data.hpp` et
   retournant un `ThreadChart` construit depuis un tableau de `Thread`
   compilé (pas de lecture JSON/CSV à l'exécution, C-S1-02).
3. Enregistrer l'appel de la factory dans `src/catalog.cpp` (`registry()`),
   dans l'ordre de déclaration souhaité, et ajouter la ligne CMake du nouveau
   fichier `.cpp` dans `libs/thread_palette/CMakeLists.txt`.
4. Remplir `source_note` : fabricant, gamme, URL source, date de
   consultation. Ajouter la ligne correspondante dans la sous-section
   « Nuanciers de fils » de `THIRD_PARTY_LICENSES.md` (une ligne par
   nuancier, jamais fusionnée).

Distance perceptuelle (`include/openstitch/thread_palette/color_distance.hpp`,
module séparé du registre — un consommateur qui ne veut que lister les fils ne
paie pas le coût de la conversion couleur) :

- `to_cielab(rgb)` — conversion sRGB → CIELAB (D65, observateur 2°), écrite
  depuis les formules standard, indépendante de `cv::cvtColor`
  (`libs/segmentation`, approximation OpenCV non spécifiée bit à bit, et
  `thread_palette` ne peut lier aucune bibliothèque tierce).
- `ciede2000(a, b)` — distance perceptuelle CIEDE2000, validée en test contre
  les 34 paires de référence publiées par Sharma, Wu & Dalal (2005).
- `nearest_threads(rgb, chart, top_n)` — top-N fils les plus proches d'une
  couleur, triés par distance CIEDE2000 croissante, égalités départagées par
  l'ordre de déclaration du nuancier (déterministe).

## Ce qui n'existe pas encore

Limitation : dans l'application (menus, inspecteur, panneaux), **aucun
sélecteur de fil, nuancier ni pipette n'est branché** : on travaille en couleurs
RGB libres. Le catalogue existe en bibliothèque et son seul point d'accroche est
le champ facultatif `thread_key` d'un bloc de couleur du design importé
(`stitch::ColorBlock`, relu depuis le `.osp`), sans interface pour le renseigner.
Il n'est **pas encore relié** au document :

- pas d'association objet ↔ fil de nuancier (`ThreadRef` sur
  `EmbroideryObject`, HP-THR-004) ;
- pas de filtrage par gamme possédée / nuancier personnel (HP-THR-007) ;
- pas de poids/type de fil (HP-THR-009) ;
- pas d'UI (film couleur, pipette, sélecteur de fil) ;
- seulement deux nuanciers réels chargés (Madeira Polyneon, Isacord 40, tous
  deux actuellement en données placeholder — voir l'avertissement ci-dessus) ;
  les autres nuanciers cités par HP-THR-002 (Robison-Anton, Sulky, Gunold,
  Marathon, Floriani, Coats, Brother, Janome, Pantone approximé…) restent à
  ajouter.

## Conséquence pour le DST

Le format DST ne stocke de toute façon **pas** les couleurs réelles, seulement
des arrêts « changement de fil ». L'ordre des couleurs est porté par le document
`.osp`, pas par le DST (voir *Format DST*).

## Implémentation associée

- `libs/thread_palette/include/openstitch/thread_palette/thread.hpp` — types
  `ThreadKey`/`Thread`/`ThreadChart`.
- `libs/thread_palette/include/openstitch/thread_palette/catalog.hpp` et
  `src/catalog.cpp` — registre et recherche.
- `libs/thread_palette/include/openstitch/thread_palette/color_distance.hpp`
  et `src/color_distance.cpp` — sRGB→CIELAB, CIEDE2000, `nearest_threads`.
- `libs/thread_palette/data/madeira_polyneon.cpp`,
  `libs/thread_palette/data/isacord_40.cpp` — nuanciers compilés (données
  placeholder, voir avertissement ci-dessus).
- `libs/document/.../embroidery_object.hpp` — champ `rgb` (RGB direct de
  l'objet, pas encore relié au catalogue).
- `libs/stitch_generation/src/generate.cpp` — insertion des `ColorChange`.
- `libs/segmentation/src/segmentation.cpp` — `recolor_region`.
