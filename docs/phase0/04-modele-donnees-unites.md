# Phase 0 — Modèle de données, unités et représentation des points

## 1. Stratégie d'unités et de coordonnées (ADR-003)

### 1.1 Unité interne : le **micromètre**, en entier 32 bits

```cpp
// libs/core — types forts, pas de using = int nu
struct Micrometers { std::int32_t value; /* opérateurs arithmétiques et comparaisons */ };
struct Millimeters { double value; };     // uniquement pour l'UI et les paramètres utilisateur
struct Pixels      { double value; };     // uniquement côté image
struct Angle       { double radians; };
```

Justifications :

- **Entier** : les étapes sensibles (booléens de polygones via Clipper2, encodage DST) exigent des coordonnées entières exactes ; pas d'accumulation d'erreur flottante, résultats déterministes (requis pour les tests golden).
- **Micromètre** : 1 µm = 1/1000 mm. Le pas DST est 0,1 mm = 100 µm → conversion exacte par division entière. La précision est largement au-delà du besoin physique (une aiguille fait ~0,7 mm).
- **int32** : ±2 147 m de plage — infiniment suffisant pour un cadre de broderie (max ~500 mm), tout en restant compatible avec le type de coordonnées 64 bits de Clipper2 sans risque d'overflow dans les produits croisés intermédiaires (Clipper2 gère cela en 64/128 bits en interne).

### 1.2 Frontières de conversion

| Frontière | Conversion | Où |
|---|---|---|
| Image → document | `Pixels × (mm/pixel défini à l'import) → Micrometers` | `libs/image` (paramètre de résolution de travail obligatoire) |
| UI ↔ document | `Millimeters ↔ Micrometers` (affichage 2 décimales) | couche GUI uniquement |
| Document → DST | `Micrometers / 100 → unités DST (0,1 mm)`, arrondi contrôlé **sur les deltas cumulés** (voir 05-format-dst.md) | `libs/formats` |

Règle absolue (§25/§33) : **aucun `double` nu ne représente une coordonnée** dans le modèle ou les formats. Le flottant est autorisé *à l'intérieur* d'un algorithme (interpolation de Bézier, k-means) mais les entrées/sorties des modules sont en types forts.

### 1.3 Repère

Origine au centre du canevas, X vers la droite, Y vers le **haut** (convention physique/machine), conversion Y inversé faite par la couche de rendu (Qt a Y vers le bas). Documenté dans `docs/units.md` dès la Phase 1.

## 2. Identifiants

```cpp
struct ObjectId  { std::uint64_t value; };  // unique par document, jamais réutilisé
struct RegionId  { std::uint64_t value; };  // régions de segmentation, stables (§4.3)
struct ThreadId  { std::uint64_t value; };
struct ColorId   { std::uint64_t value; };  // entrée de palette du document
```

Générés par un compteur monotone stocké dans le document (pas d'UUID : déterminisme des tests, lisibilité des fichiers projet).

## 3. Modèle de document (lib `document`)

```
Document
├── DocumentMeta        : version schéma, titre, auteur, dates, notes
├── Canvas              : taille physique (µm), cadre actif (machine_profiles), grille
├── SourceImages[]      : image originale (intacte) + pile de transformations rejouables
├── SegmentationState   : carte de régions (RegionId), palette de quantification
├── ThreadPalette       : ColorId → ThreadColor (RGB + Lab + réf. fabricant optionnelle)
├── Layers[] (ordonnés)
│   └── items : Group | EmbroideryObject   (arbre)
├── StitchSequence      : cache des points générés (voir §5) — recalculable
├── SewingOrder         : liste ordonnée d'ObjectId + verrous utilisateur
└── AnalysisReport      : avertissements (recalculable)
```

### 3.1 EmbroideryObject — séparation intention / résultat

```cpp
struct EmbroideryObject {
    ObjectId              id;
    std::string           name;
    ObjectKind            kind;        // variant, voir §4
    ObjectGeometry        geometry;    // ÉDITABLE : chemins en Micrometers
    ColorId               color;
    StitchParams          params;      // spécifiques au kind (variant aligné)
    UnderlaySpec[]        underlays;   // vide au MVP
    TieSpec               ties;        // points d'arrêt entrée/sortie (MVP : simple)
    std::optional<RegionId> sourceRegion;  // lien vers la segmentation d'origine
    bool                  visible, locked;
    GenerationState       genState;    // Clean | Dirty | ManuallyEdited (voir §6)
};
```

Les **points générés ne sont pas stockés dans l'objet** mais dans `StitchSequence`, indexés par `ObjectId`. Régénérer un objet ne touche jamais sa géométrie ; modifier la géométrie marque le cache `Dirty`.

### 3.2 Géométrie éditable

```cpp
// libs/geometry
struct PathNode  { Vec2um pos; NodeType type; /* Corner | Smooth */ std::optional<Vec2um> tanIn, tanOut; };
struct Path      { std::vector<PathNode> nodes; bool closed; };
struct PathSet   { Path outer; std::vector<Path> holes; };   // région avec trous
```

Les Béziers sont portés par les tangentes des nœuds ; l'aplatissement en polylignes (tolérance en µm) est un service de `geometry`, utilisé par la génération de points — la source reste la courbe.

## 4. Types d'objets (MVP puis extensions)

`ObjectKind` = `std::variant` :

- **MVP** : `RunningStitchPath` (simple/double/triple, sur `Path`), `TatamiFill` (sur `PathSet`), `ManualStitches` (séquence brute, produit d'un import DST ou d'une conversion), `ColorChange`, `StopCommand`, `TrimCommand` (objets de commande insérables dans l'ordre).
- **Phase 8+** : `SatinColumn` (deux rails + sections), `MotifPath`, `StemStitchPath`…

Chaque kind a sa struct de paramètres (ex. `RunningParams { StitchLength len; Repeats n; }`, `TatamiParams { Angle angle; Micrometers spacing; StitchLength len; RowOffset offset; InsetSpec inset; }`) avec des valeurs par défaut documentées.

## 5. Représentation interne des points et commandes (lib `stitch`)

```cpp
enum class CommandType : uint8_t { Stitch, Jump, Trim, ColorChange, Stop, End };

struct StitchCommand {
    Vec2um       pos;          // position absolue en µm (les deltas sont un détail du codec DST)
    CommandType  type;
    ObjectId     source;       // objet générateur (0 = manuel/importé)
    ColorId      color;
};

struct StitchSequence {
    std::vector<StitchCommand> commands;   // séquence globale, ordonnée
    // index par objet et par couleur construits à la demande
};

struct StitchStats {  // calculées depuis la séquence
    std::size_t stitches, jumps, trims, colorChanges;
    Micrometers threadLengthEstimate;      // somme des longueurs + facteur configurable
    BoundingBoxUm bounds;
};
```

Choix : positions **absolues** en interne (édition et affichage simples, pas d'accumulation d'erreur) ; l'encodage en deltas est confiné au codec DST.

## 6. Édition manuelle vs régénération (§11 du cahier des charges)

Machine à états par objet :

- `Clean` : points en cache cohérents avec la géométrie.
- `Dirty` : géométrie ou paramètres modifiés → régénération automatique (ou sur demande).
- `ManuallyEdited` : l'utilisateur a retouché les points générés. Toute action qui déclencherait une régénération affiche un choix explicite : **conserver les retouches (bloquer la régénération) / régénérer (perdre les retouches) / convertir en `ManualStitches`** (définitif). Jamais d'écrasement silencieux.

## 7. Format de projet interne `.osp` (Phase 10, spécifié dès maintenant)

- **Conteneur ZIP** (minizip-ng) : `project.json` (tout le modèle, JSON versionné, clés stables) + `images/` (originaux PNG intacts) + `cache/stitches.bin` (optionnel, régénérable).
- `schemaVersion` entier + migrations ascendantes ; validation à la lecture ; un fichier inconnu → erreur claire, jamais de crash.
- Sauvegarde **atomique** : écriture dans un fichier temporaire puis rename ; auto-sauvegarde périodique dans un fichier `.osp.autosave` ; récupération proposée au démarrage.
- Schéma documenté publiquement dans `docs/project-format.md`.
