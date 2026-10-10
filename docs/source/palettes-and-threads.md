# Palettes et fils

Public : utilisateur avancé, mainteneur. État : **Implémenté / partiel** (fils
reliés au document et à l'interface ; nuanciers de marques à fournir par
l'utilisateur).

## Ce qui existe

- Chaque objet de broderie porte une **couleur RGB** (`std::array<uint8_t,3>`),
  reprise de la région d'origine, et peut référencer un **fil de nuancier**
  (`EmbroideryObject::thread`, HP-THR-004) : voir *Fil d'un objet* ci-dessous.
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

## Bibliothèque de fils active (`ThreadLibrary`)

`include/openstitch/thread_palette/thread_library.hpp` — type valeur, sans E/S,
déterministe. `ThreadLibrary::with_builtin()` contient, dans l'ordre : le
nuancier **Générique** (`generic_basic_chart()`, 28 couleurs usuelles sans
marque, libres de droits), puis les nuanciers compilés de `all_charts()`
(Madeira/Isacord, **données de démonstration fictives**, `is_demo_chart`).
`add_chart` ajoute un nuancier utilisateur (identifiant unique, codes uniques,
jamais de remplacement silencieux) ; `remove_chart` ne retire que les
nuanciers utilisateur. `search(needle, chart_id, limit)` cherche dans le code, le
nom, la gamme et la marque ; `nearest(rgb, chart_id, n)` classe les fils par
distance CIEDE2000 (tous les nuanciers si `chart_id` est vide).

### Licences et nuanciers de marques (HP-THR-002)

Les cartes de couleurs des fabricants ne sont **pas redistribuées** : l'application
ne livre que le nuancier Générique (valeurs choisies pour ce projet) et laisse
l'utilisateur charger les siennes. Madeira/Isacord intégrés restent des données
fictives tant qu'une transcription sourcée n'existe pas (HP-THR-002 reste partiel).

**Format d'import** (menu *Fils ▸ Importer un nuancier*, `project_io::
read_thread_chart_file`, extension `.csv` ou `.json`, UTF-8) :

- **CSV** — séparateur `,` ou `;` détecté sur l'en-tête, guillemets doubles
  acceptés, lignes vides et lignes `#…` ignorées. En-tête **obligatoire**
  (insensible à la casse) : `code,name,hex` (`hex` = `#RRGGBB`) ou
  `code,name,r,g,b` (0 à 255) ; colonnes facultatives `brand`, `range`
  (synonymes : `reference`/`ref`, `nom`, `marque`, `gamme`). Le nom du fichier
  donne le nom du nuancier.
- **JSON** — `{"name": "Mes fils", "source": "carte 2024", "threads": [{"code":
  "1001", "name": "Rouge", "rgb": "#C8102E" ou [200,16,46], "brand": "…",
  "range": "…"}]}`.

L'identifiant du nuancier est dérivé du nom (`user_` + minuscules `[a-z0-9_]`) :
un fichier ne peut pas se faire passer pour un nuancier intégré. Toute ligne
invalide (code vide ou en double, couleur illisible) fait échouer l'import avec
son numéro de ligne : jamais de nuancier à moitié lu. L'application copie le
fichier dans son dossier de données (`thread_charts/`) et le recharge à chaque
lancement ; les nuanciers ne sont **jamais** stockés dans le `.osp`.

## Fil d'un objet (HP-THR-004)

`EmbroideryObject::thread` (`std::optional<ThreadKey>`) : le fil assigné, vide pour
une couleur libre. `rgb` reste la **source de rendu** ; assigner un fil copie
sa couleur dans `rgb`. `commands::SetObjectThreadCommand` (header
`libs/commands/.../thread_commands.hpp`) assigne un fil, ou une couleur libre
(HP-OBJ-018, version RGB), à un ou plusieurs objets en **un seul pas d'annulation** ;
les états précédents (fil + couleur) sont restitués exactement. Persistance : champ
`thread` du `.osp`, schéma **v6**, projet plus ancien = couleurs libres (voir
*Format de projet*). `color_blocks` renseigne `ColorBlock::thread_key` depuis le fil
de l'objet.

## API « fils du design » (`stitch_analysis::thread_usage`)

`libs/stitch_analysis/include/openstitch/stitch_analysis/thread_usage.hpp` — Qt-free,
déterministe, destinée au panneau Fils **et** à la future fiche de production
imprimable (une seule dérivation des fils utilisés). Toujours appelée avec la
séquence **effective** (`stitch_generation::effective_sequence`).

- `thread_usage(project, sequence, library*, options)` → un `ThreadUsage` par fil, dans
  l'ordre de première utilisation : `identity` (fil de nuancier, sinon RGB libre),
  `brand`/`code`/`name` (résolus dans la bibliothèque si fournie), `objects` (ordre de
  couture), `stitch_count`, `length_mm` (fil cousu piqûre à piqûre, sauts exclus),
  `color_blocks` (passages), `estimated_seconds` (piqûres ÷ vitesse + changements de fil
  + coupes ; hypothèses dans `ThreadUsageOptions` : 700 pts/min, 30 s par changement,
  3 s par coupe). Deux objets partagent un fil si leurs fils sont égaux, ou, sans fil,
  si leur RGB est égal. Les points sans objet source (DST importé) tombent dans une
  couleur libre noire, comme `color_blocks`.
- `thread_label(usage)` : « Marque Réf — Nom » ou `#RRGGBB`.
- `thread_usage_csv(usage)` : export tableur (`;`, nombres au point décimal).
- `objects_using_thread(project, identity)` : sélectionner / remplacer un fil.

## Film couleur (HP-THR-005)

`color_film(project, sequence)` liste les blocs de couleur dans l'ordre de couture
(`ColorFilmBlock` : fil, objets, points, longueur, `locked`).
`reorder_film_blocks(project, film, from, to)` et `merge_same_thread_blocks(project,
film)` renvoient le **nouvel ordre complet des objets** (à passer à
`ReorderEmbroideryCommand`, un pas d'annulation). Les objets **figés** et les objets
absents du film (masqués) gardent leur emplacement absolu ; seuls les objets libres
sont redistribués dans les emplacements libres. Fusionner les blocs de même fil
réduit les changements mais modifie l'empilement des couches (le fil cousu plus tard
passe plus tôt) : l'interface le dit et l'opération reste annulable.

## Réduction de palette (HP-THR-011)

`thread_palette::reduce_colors(couleurs pondérées, N)` : fusion agglomérative par
distance CIEDE2000 (les deux couleurs les plus proches fusionnent, la plus lourde
garde sa teinte exacte), déterministe (égalités départagées par l'ordre d'entrée).
Deux usages : `AutoOptions::max_threads` (« Limiter à N fils » de la numérisation
automatique : les régions sont recolorées avant la création des objets, la géométrie
est inchangée, le fond ignoré ne compte pas) et le bouton *Limiter à N fils* du
panneau Fils sur un motif existant (un pas d'annulation).

## Panneau Fils

`apps/desktop/thread_panel.*` (présentation seule) + `main_window_threads.cpp`
(câblage, aucune règle métier) : onglets *Projet*, *Catalogues* et *Film couleur*, menu
*Fils* (voir le *Guide utilisateur*). Clic sur un fil du catalogue = assigner à la
sélection (objets de broderie sélectionnés, ou ceux des formes sélectionnées).

## Ce qui n'existe pas encore

- Pas de filtrage par gamme possédée / nuancier personnel éditable (HP-THR-007) ;
- pas de coloris multiples (HP-THR-006), de poids/type de fil (HP-THR-009) ni de
  pipette (HP-THR-012) ;
- les noms/références de fils sont portés par le `.osp`, la liste CSV et la future fiche
  de production ; **aucun format machine implémenté (DST) ne les porte** ;
- aucune carte de marque réelle n'est livrée (licences) : seuls le nuancier Générique
  et deux nuanciers de démonstration fictifs sont intégrés, les autres marques citées par
  HP-THR-002 (Robison-Anton, Sulky, Gunold, Marathon, Floriani, Coats, Brother, Janome,
  Pantone approximé…) passent par l'import utilisateur.

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
- `libs/thread_palette/.../thread_library.hpp`, `chart_import.hpp`,
  `color_reduction.hpp` — bibliothèque active, import CSV, réduction de palette.
- `libs/project_io/.../thread_chart_io.hpp` — import de fichiers CSV/JSON.
- `libs/document/.../embroidery_object.hpp` — champs `rgb` (rendu) et `thread`.
- `libs/commands/.../thread_commands.hpp` — `SetObjectThreadCommand`.
- `libs/stitch_analysis/.../thread_usage.hpp` — `thread_usage`, film couleur.
- `apps/desktop/thread_panel.*`, `apps/desktop/main_window_threads.cpp` — panneau.
- Tests : `tests/unit/thread_palette/test_library.cpp`,
  `tests/unit/commands/test_thread_commands.cpp`,
  `tests/unit/project_io/test_thread_persistence.cpp`,
  `tests/unit/stitch_analysis/test_thread_usage.cpp`,
  `tests/unit/autodigitize/test_thread_limit.cpp`,
  `tests/unit/desktop/test_thread_panel.cpp`.
- `libs/stitch_generation/src/generate.cpp` — insertion des `ColorChange`.
- `libs/segmentation/src/segmentation.cpp` — `recolor_region`.
