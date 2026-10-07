# Implementation Plan: S2a — Couche machine et design importé

Dérive l'architecture approuvée `specs/arch-plan/vision/20260928-091642-arm-1.md`
(section « Couche machine et codecs », AD-02/02 ter/03/04, AI-02/03a/03b/04,
Scope 2) pour les parties qui touchent S2a. Couvre HP-FMT-001. Pas de
pré-plan détaillé fourni pour ce lot — ce document EST le plan dérivé ; les
décisions non explicitement tranchées par l'architecture sont marquées
`[DÉCISION S2a]` avec leur justification.

## 1. Scope recap

- Une couche de normalisation machine pure (`normalize_for_machine`,
  `MachineConstraints`) dans `libs/formats`, séparée de l'encodeur DST.
- Le type partagé de bloc de couleur `ColorBlock` (AD-02 ter) dans
  `libs/stitch`, portant un `thread_palette::ThreadKey` optionnel (nouvelle
  arête `stitch → thread_palette`).
- Un registre de formats (`FormatInfo`, AI-02) dans `libs/formats`, avec UNE
  entrée pour l'instant (`"dst"`) — PES/JEF/EXP (S2b/S2c) ajoutent leur propre
  ligne plus tard, sans toucher au reste.
- Le codec DST (`dst.hpp`/`dst.cpp`) migré sur `normalize_for_machine` :
  signature publique `encode_dst`/`decode_dst`/`write_dst_file`/
  `read_dst_file` **inchangée** (cf. §7 — décision explicite, pas un oubli),
  sortie strictement identique à l'octet.
- La table de blocs de couleur du design (`stitch_analysis`, AI-03a, AD-02
  bis), nouveau fichier, dépend de `document` + `thread_palette`.
- La composition projet ↔ fichier machine (`project_io`, AI-03b), nouveau
  fichier : `export_machine_file`, `import_machine_file`, génériques sur le
  registre (aucune autre scope n'est listée comme écrivain futur de ce
  fichier dans *Shared-File and Section Ownership* ⇒ il doit rester générique
  sans édition supplémentaire quand S2b/S2c arriveront).
- La nature « design importé » (`document::ImportedDesign`, AD-04,
  AI-04), nouveau membre optionnel de `Project`, restituée par
  `generate_sequence`/`effective_sequence` (inscription dans
  `libs/stitch_generation`, section « corps », droits R3).
- Une nouvelle commande `ICommand` (`libs/commands`, nouveau fichier,
  `commandes propres... S2a (import)`) pour que l'arrivée d'un design importé
  dans `Project` reste une mutation passant par `ICommand` (Tier 0).
- `apps/desktop/main_window.cpp` : édits strictement bornés à
  `resetDocumentState` (suppression de la ligne `sequenceImported_ = false;`),
  `refreshImage` (sortie sans image + les deux gardes `sequenceImported_`),
  `applyLoadedProject` (aucun changement de corps requis — réutilisé tel
  quel par `importDst`, voir §7), `importDst`, `exportDst`,
  `updateEmptyState`, et dans `renderStitches` l'extension des tables
  visibilité/couleur. `main_window.hpp` : suppression du membre
  `sequenceImported_`.
- Hors scope (explicitement, par l'architecture) : codecs PES/JEF/EXP,
  aiguillage par nature de `commands::ICommand` pour transformer un design
  importé (S3, AD-05), film couleur (S4), fiche de production (S12).

## 2. Nouveaux fichiers

```
libs/stitch/include/openstitch/stitch/color_block.hpp
tests/unit/stitch/test_color_block.cpp                 # ajouté à test_stitch

libs/formats/include/openstitch/formats/machine.hpp
libs/formats/src/machine.cpp
libs/formats/include/openstitch/formats/format_registry.hpp
libs/formats/src/format_registry.cpp
tests/unit/formats/test_machine.cpp                     # normalisation seule
tests/unit/formats/test_format_registry.cpp
tests/unit/formats/test_dst_bytes_regression.cpp         # byte-identique, cf. §6

libs/stitch_analysis/include/openstitch/stitch_analysis/color_blocks.hpp
libs/stitch_analysis/src/color_blocks.cpp
tests/unit/stitch_analysis/test_color_blocks.cpp

libs/document/include/openstitch/document/imported_design.hpp
tests/unit/document/test_imported_design.cpp            # sérialisation ronde

libs/project_io/include/openstitch/project_io/machine_file.hpp
libs/project_io/src/machine_file.cpp
tests/unit/project_io/test_machine_file.cpp

libs/commands/include/openstitch/commands/import_machine_design_command.hpp
tests/unit/commands/test_import_machine_design_command.cpp  # si tests/unit/commands existe,
                                                              # sinon ajouté à project_commands
                                                              # (cf. §4, vérifier au moment d'écrire)

docs/source/formats.md                                   # nouvelle page (dst-format.md renommée/étendue — voir §8)
```

Fichiers existants modifiés (édits additifs ou bornés comme indiqué) :

```
libs/stitch/CMakeLists.txt                 # + lien thread_palette, + color_block.hpp (header-only, pas de .cpp)
libs/formats/CMakeLists.txt                # + machine.cpp, format_registry.cpp, + lien thread_palette
libs/formats/include/openstitch/formats/dst.hpp   # commentaire mis à jour (migration), signatures inchangées
libs/formats/src/dst.cpp                    # corps migré sur normalize_for_machine/sequence_from_machine_records
libs/stitch_analysis/CMakeLists.txt         # + color_blocks.cpp, + lien thread_palette
libs/document/include/openstitch/document/project.hpp  # + #include imported_design.hpp, + membre imported_design
libs/document/CMakeLists.txt                # + lien stitch, thread_palette (PUBLIC)
libs/project_io/CMakeLists.txt              # + machine_file.cpp, + lien stitch_generation, stitch_analysis, formats
libs/project_io/src/json_serialize.cpp      # bloc additif project_to_json / project_from_json (imported_design)
libs/stitch_generation/src/generate.cpp     # inscription R3 en tête de generate_sequence
libs/commands/CMakeLists.txt                # + import_machine_design_command.hpp si lib a besoin d'un .cpp (non — header-only)
tests/unit/formats/CMakeLists.txt           # + 3 nouveaux fichiers de test
tests/unit/stitch/CMakeLists.txt            # + test_color_block.cpp
tests/unit/stitch_analysis/CMakeLists.txt   # + test_color_blocks.cpp
tests/unit/document/CMakeLists.txt          # + test_imported_design.cpp
tests/unit/project_io/CMakeLists.txt        # + test_machine_file.cpp
apps/desktop/main_window.hpp                # suppression sequenceImported_
apps/desktop/main_window.cpp                # cf. §1 et §7
docs/roadmap-parite-hatch.md                # statut HP-FMT-001
docs/source/architecture.md                 # nouvelle section AD-04
docs/source/dst-format.md                   # note de migration (renvoi vers formats.md si créée)
THIRD_PARTY_LICENSES.md                     # vérifié, probablement pas de changement (voir §9)
```

## 3. Types et API

### 3.1 `stitch::ColorBlock` (AD-02 ter) — `libs/stitch/include/openstitch/stitch/color_block.hpp`

```cpp
namespace openstitch::stitch {
struct ColorBlock {
    std::array<std::uint8_t, 3> rgb{};
    std::optional<thread_palette::ThreadKey> thread_key; // vide = pas de correspondance connue
    std::size_t start{0}; // index dans StitchSequence::commands, inclus
    std::size_t end{0};   // exclu
    bool operator==(const ColorBlock&) const = default;
};
}
```
`[DÉCISION S2a]` bornes en indices `[start, end)` dans `StitchSequence::commands`
(demi-ouvert, cohérent avec les conventions C++ du dépôt) plutôt qu'un
`std::span` (un `ColorBlock` doit survivre à la séquence dans certains
contextes — design importé persisté — un span serait une référence pendante).

### 3.2 `formats::MachineConstraints` / `MachineRecord` / `MachineSequence` — `machine.hpp`

```cpp
namespace openstitch::formats {
struct MachineConstraints {
    std::int32_t resolution_um{100};   // µm par unité native (DST : 0,1 mm)
    int max_record_delta{121};         // |dx|,|dy| max par enregistrement, en unités natives
    enum class TrimEncoding { Native, RepeatedZeroJumps };
    TrimEncoding trim_encoding{TrimEncoding::RepeatedZeroJumps};
    int trim_zero_jump_count{3};       // RepeatedZeroJumps : nb de sauts nuls = coupe
    bool merge_stop_into_color_change{true}; // formats qui ne distinguent pas Stop
    std::optional<int> max_colors{};
};

enum class MachineRecordType : std::uint8_t { Stitch, Jump, ColorChange, Stop, Trim };

struct MachineRecord {
    int dx{0};
    int dy{0};
    MachineRecordType type{MachineRecordType::Stitch};
    bool operator==(const MachineRecord&) const = default;
};

struct MachineSequence {
    std::vector<MachineRecord> records; // deltas relatifs à une origine implicite (0,0)
};

[[nodiscard]] Result<MachineSequence> normalize_for_machine(const stitch::StitchSequence& sequence,
                                                             const MachineConstraints& constraints);

[[nodiscard]] stitch::StitchSequence sequence_from_machine_records(
    std::span<const MachineRecord> records, const MachineConstraints& constraints);
}
```

`[DÉCISION S2a]` `normalize_for_machine` prend UNIQUEMENT `(sequence,
constraints)` — pas de table de couleur en paramètre — parce que c'est
textuellement la signature donnée par l'acceptation HP-FMT-001 du roadmap, et
parce que `formats` ne connaît pas `document` (les couleurs de blocs viennent
de `EmbroideryObject::rgb`, hors de portée de cette fonction pure). Le
« design machine » d'AD-02 (séquence normalisée + table `ColorBlock`) existe
comme type séparé (`MachineDesign`, assemblé par l'appelant, voir §3.4) pour
les formats qui ont de vraies couleurs (S2b/S2c) ; DST n'en a pas besoin
(aucune couleur réelle, seulement des arrêts, cf. roadmap §2 FMT-002).

`normalize_for_machine` reproduit, paramétrée, la logique exacte aujourd'hui
dans `encode_dst` :
- `quantize(pos) = round(pos / resolution_um)`, sans dérive cumulative
  (quantification de chaque position absolue, delta recalculé à partir des
  positions quantifiées, jamais un delta accumulé flottant).
- Un déplacement `|dx|` ou `|dy|` > `max_record_delta` est découpé en N
  enregistrements `Jump` intermédiaires de pas égal (division entière) + un
  dernier enregistrement portant le TYPE réel demandé.
- `Trim` : si `RepeatedZeroJumps`, émet `trim_zero_jump_count` enregistrements
  `Jump` de delta nul, puis, si la position cible diffère de la position
  courante, un `Jump` du delta réel (découpé si besoin). Si `Native`, émet un
  (des) enregistrement(s) `Trim` directement (dernier morceau du découpage).
- `Stop` : fondu en `ColorChange` si `merge_stop_into_color_change`.
- Coalescence : un `Jump` « organique » (commande d'entrée) de delta nul est
  **omis** si le dernier enregistrement émis était déjà un `Jump` de delta nul
  — sans ça, plusieurs sauts sous la résolution d'affilée seraient relus
  comme une coupe fantôme (cf. test existant « sauts sous la resolution DST »).

`sequence_from_machine_records` est la réciproque côté décodage : reconstruit
des positions absolues µm (`resolution_um`), convertit un run de
`trim_zero_jump_count` `Jump` de delta nul consécutifs en un seul `Trim`
logique (et un run plus court en autant de `Jump` réels), ajoute le `End`
final. Ne valide PAS les octets (ça reste la responsabilité du décodeur du
format, qui lui sait reconnaître son marqueur de fin).

### 3.3 `formats::FormatInfo` / registre — `format_registry.hpp`

```cpp
namespace openstitch::formats {
using EncodeFn = Result<std::vector<std::uint8_t>> (*)(const stitch::StitchSequence&);
using DecodeFn = Result<stitch::StitchSequence> (*)(std::span<const std::uint8_t>);

struct FormatInfo {
    std::string id;                      // "dst"
    std::string display_name;            // "Tajima DST"
    std::vector<std::string> extensions; // {"dst"}, sans le point, minuscules
    bool can_read{false};
    bool can_write{false};
    MachineConstraints default_constraints{};
    EncodeFn encode{nullptr};
    DecodeFn decode{nullptr};
};

[[nodiscard]] std::span<const FormatInfo> registered_formats();
[[nodiscard]] const FormatInfo* find_format(std::string_view id);
[[nodiscard]] const FormatInfo* find_format_for_extension(std::string_view extension_no_dot);
}
```

`[DÉCISION S2a]` le registre porte des pointeurs de fonction (pas seulement
des métadonnées descriptives) pour que `project_io`'s composition générique
(§3.4) n'ait **jamais besoin d'être modifiée** quand S2b/S2c ajoutent PES/
JEF/EXP — cohérent avec le fait que *Shared-File and Section Ownership* ne
liste AUCUN écrivain futur pour le fichier de composition de `project_io`.
`dst.cpp` fournit deux adaptateurs internes (signature exacte du pointeur,
sans `DstWriteOptions`) enregistrés comme la ligne `"dst"` du tableau statique
de `format_registry.cpp`. S2b/S2c ajoutent leur propre ligne (et leur propre
`#include`) dans ce même tableau (droit d'inscription R3 déjà accordé par la
table de propriété : « ligne de registre... de chaque codec : S2b, S2c »).

### 3.4 `document::ImportedDesign` (AD-04, AI-04) — `imported_design.hpp`

```cpp
namespace openstitch::document {
struct ImportedDesign {
    std::string source_format;                   // id du registre, ex. "dst" -- diagnostic seulement
    stitch::StitchSequence sequence;              // décodée, positions ABSOLUES µm
    std::vector<stitch::ColorBlock> color_blocks; // AD-02 ter, ordonnée
    bool operator==(const ImportedDesign&) const = default;
};
}
```

`document::Project` gagne `std::optional<ImportedDesign> imported_design;`
(nouvelle arête `document → stitch`, `document → thread_palette` via
`ColorBlock`/`ThreadKey`).

### 3.5 `stitch_analysis::color_blocks` (AI-03a, AD-02 bis) — `color_blocks.hpp`

```cpp
namespace openstitch::stitch_analysis {
[[nodiscard]] std::vector<stitch::ColorBlock> color_blocks(const document::Project& project,
                                                            const stitch::StitchSequence& sequence);
}
```
Un bloc commence au premier `Stitch`/`Jump` suivant un `ColorChange`/`Stop`
(ou au début) et se termine juste avant le `ColorChange`/`Stop`/`End`
suivant. Couleur = `EmbroideryObject::rgb` de l'objet source de la PREMIÈRE
commande du bloc (`project.findEmbroidery(cmd.source)`) ; `thread_key` reste
vide en P0 (S4 le renseigne plus tard, AD-02 bis, sans changer le type). Les
blocs vides (`start == end`, deux arrêts consécutifs sans point entre eux)
sont omis.

`[DÉCISION S2a]` couleur par défaut pour un bloc **sans objet source connu**
(design importé : `source == ObjectId{}`, ou tout autre cas où
`findEmbroidery` échoue) = `{0, 0, 0}` (noir), `thread_key` vide. DST ne
porte aucune couleur réelle (roadmap §2, FMT-002) — un design importé DST
produit donc N blocs noirs (un par segment entre arrêts), honnêtement
« couleur inconnue », jamais une supposition. Ce choix est documenté dans le
code et dans `docs/source/formats.md` ; à réviser quand HP-FMT-003 (import
PES, couleurs réelles) arrivera.

### 3.6 `project_io::export_machine_file` / `import_machine_file` (AI-03b, AD-03)

```cpp
namespace openstitch::project_io {
[[nodiscard]] Result<void> export_machine_file(const document::Project& project,
                                               const std::string& format_id,
                                               const std::filesystem::path& path);

[[nodiscard]] Result<document::ImportedDesign> import_machine_file(
    const std::filesystem::path& path, std::string format_id = {});
}
```
`export_machine_file` : résout `format_id` dans le registre (refuse si
inconnu/`!can_write`/`encode==nullptr`), calcule
`stitch_generation::effective_sequence(project)` (retouches incluses),
encode, écrit (non atomique, comme l'actuel `write_dst_file` — pas de
changement de contrat là-dessus en P0).
`import_machine_file` : si `format_id` vide, déduit de l'extension du
chemin (minuscule, sans point) via `find_format_for_extension` ; décode ;
construit `ImportedDesign{format_id, *sequence, stitch_analysis::color_blocks(document::Project{}, *sequence)}`
(un `Project` vide : la dérivation des blocs ne peut utiliser aucune
correspondance objet→couleur puisqu'aucun objet n'existe pour un design
tout juste importé, cf. §3.5).

### 3.7 `commands::SetImportedDesignCommand` (Tier 0, AD-04)

```cpp
namespace openstitch::commands {
class SetImportedDesignCommand final : public ICommand {
public:
    explicit SetImportedDesignCommand(std::optional<document::ImportedDesign> next);
    void apply(document::Project& project) override;  // swap avec project.imported_design
    void revert(document::Project& project) override;  // swap inverse
    [[nodiscard]] std::string name() const override { return "Importer un design machine"; }
private: /* next_, previous_ : std::optional<document::ImportedDesign> */
};
}
```
Même patron d'échange que `SetSegmentationCommand`.

## 4. Inscriptions dans des fichiers possédés par d'autres scopes (droits R3)

- `libs/stitch_generation/src/generate.cpp`, en tête du corps de
  `generate_sequence` (juste après l'accolade ouvrante, avant la première
  ligne existante — aucune ligne existante modifiée) :
  ```cpp
  // S2a (AD-04) : un design importé est une donnée SOURCE, jamais régénérée
  // par objet -- restituée telle quelle, avant toute construction depuis
  // `embroidery_objects` (vide pour un projet d'import).
  if (project.imported_design) {
      return project.imported_design->sequence;
  }
  ```
  Exempté de `check_no_raw_sequence_bypass.cmake` (le script ignore
  `libs/stitch_generation/`). `effective_sequence` (qui enchaîne
  `generate_sequence` + `apply_manual_overrides`) n'a besoin d'AUCUN
  changement : `apply_manual_overrides` itère `project.embroidery_objects`
  (vide), donc no-op sur une séquence importée — comportement correct déjà
  garanti par la structure existante.
- `libs/project_io/src/json_serialize.cpp` : un bloc additif dans
  `project_to_json` (juste avant `return j;`) et un bloc additif dans
  `project_from_json` (juste avant `} catch`), aucune ligne existante
  touchée (cf. §5 pour le format JSON).

## 5. Sérialisation `.osp` du design importé

Bloc additif dans `project_to_json`, seulement si présent :
```cpp
if (project.imported_design) {
    const auto& im = *project.imported_design;
    json id;
    id["sourceFormat"] = im.source_format;
    id["sequence"] = json::array();
    for (const auto& cmd : im.sequence.commands) {
        id["sequence"].push_back({{"pos", vec_to_json(cmd.pos)},
                                  {"type", static_cast<int>(cmd.type)},
                                  {"pass", static_cast<int>(cmd.pass)}});
        // source délibérément omis : toujours ObjectId{} pour un design importé (0 = manuel/importé, sequence.hpp)
    }
    id["colorBlocks"] = json::array();
    for (const auto& b : im.color_blocks) {
        json bj{{"rgb", rgb_to_json(b.rgb)}, {"start", b.start}, {"end", b.end}};
        if (b.thread_key) {
            bj["threadChart"] = b.thread_key->chart_id;
            bj["threadCode"] = b.thread_key->code;
        }
        id["colorBlocks"].push_back(bj);
    }
    j["importedDesign"] = id;
}
```
Lecture symétrique (`if (j.contains("importedDesign")) { ... }`), absence =
`std::nullopt` (rétrocompatible, v1/v2/v3 n'en avaient pas — pas de bump de
`kSchemaVersion`, un champ additif optionnel n'en nécessite pas, même
politique que les champs `visible`/`intent` existants lus avec
`.value(..., défaut)`).

`[DÉCISION S2a]` pas de nouvelle version de schéma : `kSchemaVersion` reste 3
tant qu'aucun champ EXISTANT ne change de sens (cf. le commentaire de
`project_io.hpp` : seul un changement de NATURE du fichier bump la version).
Un design importé absent est un no-op de lecture, exactement comme
`overrides`/`finishing` absents aujourd'hui.

## 6. Preuve du « byte-identique » (HP-FMT-001, exigence dure)

1. **Snapshot de référence pré-refactor** : avant de toucher `dst.cpp`, un
   test temporaire (`tests/unit/formats/test_dst_bytes_regression.cpp`, gardé
   après coup comme régression permanente) encode plusieurs séquences
   représentatives (carré simple, grands déplacements, trims multiples,
   changements de couleur, séquence de 1000 points à quantification non
   triviale) avec le `encode_dst` ACTUEL (avant refactor) et fige le résultat
   en tableau d'octets attendu (`std::vector<std::uint8_t>` littéral ou hex
   dump en commentaire). Le test est écrit et exécuté AVANT le refactor pour
   capturer la vérité actuelle, puis laissé inchangé : après le refactor, il
   doit repasser au vert sans aucune modification de ses valeurs attendues.
2. L'intégralité de `tests/unit/formats/test_dst.cpp` (existant, NON modifié)
   doit continuer à passer sans changement d'assertion — c'est le filet de
   sécurité comportemental déjà en place (en-tête calculé, aller-retour exact,
   découpage des grands déplacements, trim/couleur, déterminisme, fichiers
   invalides, octets en trop ignorés, sauts sous résolution jamais relus
   comme une coupe).
3. Nouveaux tests de normalisation PURE (`test_machine.cpp`) : saut de 50 mm
   découpé en N enregistrements de delta ≤ 121, dérive cumulée nulle sur
   10 000 points (acceptation HP-FMT-001 explicite), aller-retour
   normalize → sequence_from_machine_records pour chaque type de commande.
4. Argument de correction (documenté ici, pas seulement dans le code) : les
   bornes min/max de l'en-tête DST sont calculées, avant ET après le
   refactor, comme la somme cumulée des deltas émis à partir d'une origine
   commune — la géométrie du découpage (chaque morceau intermédiaire reste
   sur le segment [position précédente, position cible], donc dans sa boîte
   englobante) garantit que min/max calculés sur les enregistrements finaux
   coïncident avec min/max calculés sur les commandes d'entrée (ancien
   algorithme). Si un test de régression échoue malgré cet argument,
   `encode_dst` n'est PAS considéré migré avant correction — c'est le critère
   de blocage de cette tâche, pas une approximation acceptée.

Si, après implémentation, un écart d'un seul octet apparaît entre l'ancien et
le nouveau comportement sur un cas non couvert par les tests existants, la
roadmap reste `◐ Partiel` avec la raison exacte — pas de passage à `☑ Fait`
sans cette preuve.

## 7. `apps/desktop` — décisions explicites

`[DÉCISION S2a]` **signatures DST inchangées** (`encode_dst`/`decode_dst`
restent `StitchSequence ↔ bytes`, pas `MachineDesign ↔ bytes`). Justification :
(a) l'acceptation HP-FMT-001 ne porte que sur `normalize_for_machine` et le
byte-identique, pas sur la forme de l'API du codec ; (b) ces fonctions sont
directement appelées par `apps/cli/main.cpp` (`stats`, `dst2svg`, `digitize`)
et par les tests existants — un changement de signature élargirait le
périmètre touché bien au-delà de S2a ; (c) le registre (§3.3) expose déjà le
contrat `MachineDesign`-compatible nécessaire pour S2b/S2c via les pointeurs
`EncodeFn`/`DecodeFn` typés sur `StitchSequence`, donc rien n'empêche PES/JEF
d'avoir de vraies tables de couleurs AILLEURS (le design importé, AI-04, les
porte déjà) sans que `encode_dst`/`decode_dst` eux-mêmes en aient besoin.

`[DÉCISION S2a]` **`importDst` reste un remplacement complet du document**
(même confirmation utilisateur qu'aujourd'hui, même `resetDocumentState`,
même perte d'historique d'annulation PRÉCÉDENT) — PAS une fusion avec le
document courant. Ce qui change : au lieu d'assigner directement un champ
hors-document (`MainWindow::sequence_`/`sequenceImported_`), la nouvelle
séquence entre dans `project_.imported_design` via
`undoStack_.execute(std::make_unique<commands::SetImportedDesignCommand>(...), project_)`
poussée sur la pile FRAÎCHEMENT VIDÉE par `resetDocumentState` — Tier 0
respecté (toute mutation de `Project` passe par `ICommand`), et un Ctrl+Z
immédiatement après un import redonne un document sans design importé (seul
cas réellement exercé en P0, puisqu'aucune autre mutation n'est encore
possible sur un design importé — AD-05/S3 gère l'aiguillage par nature pour
la suite). Alternative rejetée : faire de tout l'import une seule transaction
non annulable (comme avant) — rejetée parce que l'architecture dit
explicitement « annulable » pour AD-04 et que le coût de la bonne version est
nul (même pattern que `SetSegmentationCommand`, déjà dans le dépôt).

Nouveau corps de `importDst` (remplace l'actuel, lignes 6491-6528) :
```cpp
void MainWindow::importDst() {
    const QString file = QFileDialog::getOpenFileName(this, tr("Importer un DST"), QString(),
                                                      tr("Broderie Tajima (*.dst)"));
    if (file.isEmpty()) {
        return;
    }
    if (project_.hasImage() || project_.imported_design || !project_.vector_objects.empty() ||
        !project_.embroidery_objects.empty()) {
        const auto answer = QMessageBox::question(
            this, tr("Importer un DST"), tr("L'import remplace le document en cours. Continuer ?"));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    auto imported = project_io::import_machine_file(std::filesystem::path(file.toStdWString()));
    if (!imported) {
        QMessageBox::warning(this, tr("Import impossible"),
                             QString::fromStdString(imported.error().message));
        return;
    }

    project_ = document::Project{};
    resetDocumentState();
    undoStack_.execute(std::make_unique<commands::SetImportedDesignCommand>(std::move(*imported)),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage(); // régénère sequence_ via effective_sequence (voit project_.imported_design)
    view_->fitCanvas();
    updateActions();

    if (sequence_) {
        const auto stats = stitch::compute_stats(*sequence_);
        statusBar()->showMessage(tr("%1 — %2 points, %3 saut(s), %4 changement(s) de fil")
                                     .arg(QFileInfo(file).fileName())
                                     .arg(stats.stitches)
                                     .arg(stats.jumps)
                                     .arg(stats.color_changes));
    }
}
```
`exportDst` : remplace le corps `formats::write_dst_file(path, *sequence_)`
par `project_io::export_machine_file(project_, "dst", path)` — mêmes
dialogues/résumé (inchangés, ils lisent déjà `*sequence_`/`stats` AVANT
l'écriture). `[DÉCISION S2a]` le résumé pré-export continue de lire
`sequence_`/`compute_stats` (affichage), seule la ligne d'ÉCRITURE change
d'appel ; cohérent avec « le desktop n'a pas de logique métier » (AD-03) —
`project_io` recalcule de son côté `effective_sequence(project_)`, qui est
garanti identique à `*sequence_` (même contenu, cf. invariants de
`refreshImage`), donc pas de double calcul observable par l'utilisateur
différent de l'actuel.

`refreshImage` (voir texte complet dans le diff — résumé des 3 points
touchés) :
1. Sortie sans image : `if (!project_.hasImage() && !project_.imported_design) { ...; return; }`
   (ajout de la condition `&& !project_.imported_design`, reste identique
   sinon) ; le bloc « a une image » existant est encapsulé dans
   `if (project_.hasImage()) { ...sélection + pipeline image inchangés... } else { processed_ = {}; }`
   — comportement IDENTIQUE pour tous les cas déjà couverts (image présente :
   même code ; image absente SANS design importé : toujours retour anticipé
   plus haut), nouveau seulement pour « pas d'image MAIS design importé ».
2. `editStates_.clear(); sequence_.reset();` (inconditionnel — la garde
   `!sequenceImported_` disparaît, `sequence_` est de toute façon regénéré
   juste après par le bloc suivant).
3. `if (!project_.embroidery_objects.empty() || project_.imported_design) { ...refresh_context... }`
   (remplace `if (!sequenceImported_ && !project_.embroidery_objects.empty())`).

`resetDocumentState` : supprime uniquement la ligne `sequenceImported_ = false;`
(le membre disparaît).

`updateEmptyState` : étend `hasContent` avec `|| project_.imported_design`
(sinon un document importé, sans image ni objets, afficherait à tort le
pictogramme « document vide »).

`renderStitches` : juste après la boucle qui construit `visible`/`colorOf`
depuis `project_.embroidery_objects`, bloc additif :
```cpp
if (project_.imported_design) {
    visible[ObjectId{}.value] = true; // 0 = design importé (sequence.hpp) -- toujours visible en P0
    const auto& blocks = project_.imported_design->color_blocks;
    const auto rgb = blocks.empty() ? std::array<std::uint8_t, 3>{0, 0, 0} : blocks.front().rgb;
    colorOf[ObjectId{}.value] = qRgb(rgb[0], rgb[1], rgb[2]);
}
```
`[DÉCISION S2a]` un seul `QRgb` pour tout le design importé (pas de couleur
par bloc) : `colorOf` est une `unordered_map<id, QRgb>` indexée par
`EmbroideryObject::id` — TOUTES les commandes importées partagent la même
`source == ObjectId{}`, donc une seule couleur peut leur être associée par
cette structure. Rendre la couleur RÉELLEMENT par bloc nécessiterait de
changer `colorOf` en fonction de la position dans la séquence (pas juste de
la source), hors scope S2a (DST n'a de toute façon aucune vraie couleur à
afficher, §3.5) — à revoir avec HP-FMT-003 (import PES, couleurs réelles).

`applyLoadedProject` : **aucun changement de corps**. Fonctionne déjà
correctement pour un `Project` qui porte un `imported_design` (chargé depuis
un `.osp`) : `resetDocumentState()` puis `refreshImage()` régénèrent
`sequence_` via le nouveau chemin de `generate_sequence`. La fonction est
listée comme S2a dans la table de propriété par précaution (au cas où un
écart serait découvert à l'implémentation) ; ce plan documente explicitement
qu'aucune ligne n'y est modifiée.

`main_window.hpp` : suppression de `bool sequenceImported_{false};` (ligne
569). `sequence_` reste (AD-04 : « `MainWindow::sequence_` reste »).

## 8. Documentation

- `docs/source/dst-format.md` : note en tête expliquant que l'encodeur/
  décodeur délèguent désormais la normalisation à `normalize_for_machine`
  (`libs/formats/include/openstitch/formats/machine.hpp`) — le format DST lui-
  même et son comportement observable ne changent pas.
- `docs/source/architecture.md` : nouvelle section (ADR-like) « Design importé
  comme donnée source (AD-04) » — réplique le texte de l'architecture (nature
  importée vs exception UI, `effective_sequence` comme seul point d'entrée,
  annulable via `SetImportedDesignCommand`).
- `docs/roadmap-parite-hatch.md` : HP-FMT-001 passe à `☑ Fait` (date du jour,
  commit à renseigner par l'orchestrateur) avec une ligne « Livré : … » SI ET
  SEULEMENT SI §6 est intégralement vérifié par les tests (voir validation).
  Sinon `◐ Partiel` avec la raison précise.
- `THIRD_PARTY_LICENSES.md` : pas de changement attendu (refactor interne +
  migration d'un format déjà documenté, aucune nouvelle donnée/doc tierce
  reprise) — vérifié explicitement, pas supposé.

## 9. Tests (récapitulatif)

| Fichier | Couvre |
|---|---|
| `tests/unit/stitch/test_color_block.cpp` | égalité, bornes, `thread_key` optionnel |
| `tests/unit/formats/test_machine.cpp` | normalisation pure : découpage >121, dérive nulle/10 000 pts, trim Native vs RepeatedZeroJumps, fusion Stop→ColorChange, coalescence des sauts nuls |
| `tests/unit/formats/test_format_registry.cpp` | une entrée "dst", capacités, résolution par extension |
| `tests/unit/formats/test_dst_bytes_regression.cpp` | octets figés pré-refactor (§6.1) |
| `tests/unit/formats/test_dst.cpp` | INCHANGÉ — doit repasser tel quel |
| `tests/unit/stitch_analysis/test_color_blocks.cpp` | segmentation par ColorChange/Stop, couleur objet source, défaut noir si source inconnue, blocs vides omis |
| `tests/unit/document/test_imported_design.cpp` | égalité, intégration dans `Project` (optionnel, défaut `nullopt`) |
| `tests/unit/project_io/test_machine_file.cpp` | export (bytes identiques à `encode_dst` direct), import (ImportedDesign correct), format inconnu → erreur structurée, extension déduite |
| `tests/unit/commands/...` ou ajout à `project_commands` | apply/revert de `SetImportedDesignCommand`, round-trip undo/redo |
| Nouveau test desktop (si `tests/unit/desktop` a un hook pour `MainWindow` headless — à vérifier ; sinon noté comme non couvert par test automatisé, validé manuellement/CLI) | import remplace `sequenceImported_` par la nature importée, `effective_sequence` restitue bien le contenu importé |

## 10. Commandes de validation

```powershell
cmake --build --preset msvc-debug --target openstitch_stitch openstitch_formats openstitch_stitch_analysis openstitch_document openstitch_project_io openstitch_commands openstitch_stitch_generation test_stitch test_formats test_stitch_analysis test_document test_project_io
ctest --preset msvc-debug -R "stitch|formats|document|project_io|commands"
cmake --build --preset msvc-debug   # build complet (desktop inclus), avant de déclarer terminé
ctest --preset msvc-debug           # suite complète
cmake --build --preset linux-core   # garde Qt-free (hors apps/desktop)
git ls-files '*.cpp' '*.hpp' | xargs clang-format --dry-run --Werror
```

## 11. Questions ouvertes / points à trancher pendant l'implémentation

1. `tests/unit/commands/` n'existe peut-être pas encore comme répertoire de
   test séparé — à vérifier ; si absent, le test de `SetImportedDesignCommand`
   rejoint `tests/unit/project_io` (qui exerce déjà des commandes via
   `undo_stack`) ou un nouveau petit répertoire `tests/unit/commands/` est
   créé (fichier nouveau, pas d'ownership en conflit).
2. Couleur par défaut d'un bloc sans objet source = noir (§3.5) : décision
   défendable mais arbitraire — à documenter comme telle, pas comme un fait
   du format.
3. `exportDst`/`importDst` : la boîte de confirmation « replace » utilise
   maintenant un test sur le contenu RÉEL du projet (`hasImage() ||
   imported_design || !vector_objects.empty() || !embroidery_objects.empty()`)
   au lieu de `hasImage() || sequence_` — intentionnel (c'est un meilleur
   test de « il y a quelque chose à perdre »), signalé ici car c'est un
   changement de comportement observable, pas juste un renommage.
4. Validation complète (ctest intégral, build desktop) dépend du temps
   disponible après l'implémentation — si le budget ne permet pas un build
   complet du desktop (Qt), le rapport final le dira explicitement plutôt que
   de supposer un succès.
