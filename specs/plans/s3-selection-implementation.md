# Implementation Plan: S3 - Selection, Clipboard, Transformations

Status: draft implementation plan only. Read-only pass performed against the current worktree. This plan assumes S2a lands first and replaces the imported-DST exception with the document-level imported-design nature described by AI-04. Any S2a renames must be reconciled before coding.

## 1. Scope Recap

S3 delivers the wave-1 object manipulation slice:

- HP-OBJ-001: multi-selection from canvas and Document panel: Ctrl/Shift-click, rubber-band selection, Ctrl+A, select objects by color, common bounding box.
- HP-OBJ-002: cut/copy/paste of vector objects and embroidery objects, including params, RGB/thread-adjacent fields, manual overrides, and system clipboard MIME carrying a serialized `.osp` subset.
- HP-OBJ-003: keyboard move, arrows = 0.1 mm, Shift+arrows = 1 mm, with merged undo for a burst.
- HP-OBJ-004: free rotation of selected objects, numeric angle entry, Shift snapping to 15 degrees, rotation of dependent params, and 360-degree identity at micrometer precision.
- HP-OBJ-006: horizontal/vertical mirror around the selection center, including dependent params.
- HP-OBJ-014: transform whole design, limited in this scope to center-in-hoop, rotate 90 degrees, mirror whole design, and a minimal whole-design scale command needed by the roadmap wording. Full resize UX is not included.
- HP-OBJ-016: Delete removes the current selection, whether it is object selection or region selection, in one undoable command.

Explicit exclusions from the master architecture:

- HP-OBJ-019 multi-object inspector is out of scope. Multi-selection may show summary text only.
- HP-OBJ-005 full resize handles/side handles/numeric W/H UI are out of scope except the minimal whole-design scaling path needed by HP-OBJ-014.
- No Qt in `libs/**`; desktop owns clipboard interaction with `QClipboard`, but core clipboard payload serialization lives in `libs/project_io`.

## 2. New Files

Core and tests:

```text
libs/geometry/include/openstitch/geometry/transform.hpp
libs/geometry/src/transform.cpp

libs/commands/include/openstitch/commands/object_operation.hpp
libs/commands/include/openstitch/commands/transform_objects_command.hpp
libs/commands/include/openstitch/commands/delete_objects_command.hpp
libs/commands/include/openstitch/commands/clipboard_commands.hpp
libs/commands/src/object_operation.cpp
libs/commands/src/transform_objects_command.cpp
libs/commands/src/delete_objects_command.cpp
libs/commands/src/clipboard_commands.cpp

libs/project_io/include/openstitch/project_io/project_subset.hpp
libs/project_io/src/project_subset.cpp

tests/unit/geometry/test_transform.cpp
tests/unit/commands/test_object_operations.cpp
tests/unit/commands/test_transform_objects.cpp
tests/unit/commands/test_clipboard_commands.cpp
tests/unit/project_io/test_project_subset.cpp
```

Desktop and tests:

```text
apps/desktop/selection_model.hpp
apps/desktop/selection_model.cpp

tests/unit/desktop/test_selection_model.cpp
tests/unit/desktop/test_selection_canvas.cpp
```

CMake additions:

- Add `transform.cpp` to `libs/geometry/CMakeLists.txt`.
- Add the four new command `.cpp` files to `libs/commands/CMakeLists.txt`.
- Add `project_subset.cpp` to `libs/project_io/CMakeLists.txt`.
- Add `selection_model.cpp/.hpp` to `apps/desktop/CMakeLists.txt`.
- Add the new test files to their existing test executables or as new QTest executables using `openstitch_add_qt_test(...)`.

## 3. Types and API

### Geometry Transform Helpers

`libs/geometry/include/openstitch/geometry/transform.hpp`:

```cpp
#pragma once

#include <cstdint>
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

enum class MirrorAxis { Horizontal, Vertical };

struct BoundsUm {
    Vec2um min{};
    Vec2um max{};
    [[nodiscard]] Vec2um center() const;
    [[nodiscard]] bool empty() const;
};

struct AffineTransformUm {
    // 2x2 + translation. Coordinates are accepted/emitted as integer um.
    // Internals may use double for trig/matrix math, but document coordinates
    // are rounded once at the boundary using round-half-away-from-zero via lround.
    double m00{1.0};
    double m01{0.0};
    double m10{0.0};
    double m11{1.0};
    Vec2um translate{};

    [[nodiscard]] static AffineTransformUm translation(Vec2um delta);
    [[nodiscard]] static AffineTransformUm rotation(Vec2um pivot, Angle radians);
    [[nodiscard]] static AffineTransformUm mirror(Vec2um pivot, MirrorAxis axis);
    [[nodiscard]] static AffineTransformUm scale(Vec2um pivot, double sx, double sy);
};

[[nodiscard]] Vec2um transform_point(Vec2um p, const AffineTransformUm& t);
[[nodiscard]] std::optional<Vec2um> transform_tangent(std::optional<Vec2um> tangent,
                                                      const AffineTransformUm& t);
void transform_path(Path& path, const AffineTransformUm& t);
void transform_path_set(PathSet& set, const AffineTransformUm& t);
[[nodiscard]] BoundsUm bounds_of(const Path& path);
[[nodiscard]] BoundsUm bounds_of(const PathSet& set);
[[nodiscard]] BoundsUm merge_bounds(BoundsUm a, BoundsUm b);

[[nodiscard]] Angle transform_angle(Angle radians, const AffineTransformUm& t);
[[nodiscard]] Angle mirror_angle(Angle radians, MirrorAxis axis);

} // namespace openstitch::geometry
```

Implementation notes:

- Use `<numbers>` for constants, never `M_PI`.
- `rotation(pivot, Angle{2 * std::numbers::pi})` must be identity at the micrometer level. Special-case multiples of full turns within a small angular epsilon before applying trig.
- Path node positions and optional Bezier tangents are transformed. Tangents are relative vectors, so translation is ignored for tangents.

### Selection Model

`apps/desktop/selection_model.hpp`:

```cpp
#pragma once

#include <QObject>
#include <optional>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/geometry/transform.hpp"

namespace openstitch::document {
struct Project;
}

namespace openstitch::desktop {

enum class SelectionKind { None, Region, VectorObject, EmbroideryObject };

struct SelectionItem {
    SelectionKind kind{SelectionKind::None};
    std::uint64_t id{0};

    [[nodiscard]] static SelectionItem region(RegionId id);
    [[nodiscard]] static SelectionItem vectorObject(ObjectId id);
    [[nodiscard]] static SelectionItem embroideryObject(ObjectId id);

    auto operator<=>(const SelectionItem&) const = default;
};

class SelectionModel final : public QObject {
    Q_OBJECT

public:
    explicit SelectionModel(QObject* parent = nullptr);

    [[nodiscard]] bool empty() const;
    [[nodiscard]] int size() const;
    [[nodiscard]] bool single() const;
    [[nodiscard]] bool contains(SelectionItem item) const;
    [[nodiscard]] std::vector<SelectionItem> items() const;
    [[nodiscard]] std::vector<ObjectId> vectorObjectIds() const;
    [[nodiscard]] std::vector<ObjectId> embroideryObjectIds() const;
    [[nodiscard]] std::vector<RegionId> regionIds() const;
    [[nodiscard]] std::optional<SelectionItem> primary() const;
    [[nodiscard]] std::optional<ObjectId> primaryVectorObject() const;
    [[nodiscard]] std::optional<ObjectId> primaryEmbroideryObject() const;
    [[nodiscard]] std::optional<RegionId> primaryRegion() const;

    [[nodiscard]] std::optional<geometry::BoundsUm> objectBounds(
        const document::Project& project) const;

    void clear();
    void setSingle(SelectionItem item);
    void setMany(std::vector<SelectionItem> items);
    void add(SelectionItem item);
    void remove(SelectionItem item);
    void toggle(SelectionItem item);
    void selectAllObjects(const document::Project& project);
    void selectObjectsByColor(const document::Project& project,
                              std::array<std::uint8_t, 3> rgb);
    void retainExisting(const document::Project& project);

signals:
    void selectionChanged();

private:
    void normalizeAndEmitIfChanged(std::vector<SelectionItem> next);
    std::vector<SelectionItem> items_;
};

} // namespace openstitch::desktop
```

Rules:

- Stable deterministic order: vectors in `project.vector_objects` order, then embroidery in `project.embroidery_objects` order, then regions in segmentation slot order.
- Regions are mutually exclusive with object selections for S3. Selecting a region clears object selection; selecting objects clears regions.
- `primary()` is the last clicked item when possible, otherwise the first normalized item. This preserves single-object inspector behavior.
- `retainExisting(project)` replaces current ad hoc invalidation in `refreshImage`.

### Per-Nature Operation Contract (AI-06)

`libs/commands/include/openstitch/commands/object_operation.hpp`:

```cpp
#pragma once

#include <optional>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/transform.hpp"

namespace openstitch::commands {

enum class ObjectNature { VectorObject, EmbroideryObject, ImportedDesign };

struct ObjectRef {
    ObjectNature nature{};
    ObjectId id{};
    auto operator<=>(const ObjectRef&) const = default;
};

struct ObjectSnapshot {
    ObjectRef ref{};
    std::optional<document::VectorObject> vector_object;
    std::optional<document::EmbroideryObject> embroidery_object;
    // After S2a: optional imported-design snapshot field goes here.
};

struct ObjectOperationHandler {
    ObjectNature nature;
    bool (*exists)(const document::Project&, ObjectId);
    std::optional<geometry::BoundsUm> (*bounds)(const document::Project&, ObjectId);
    bool (*can_transform)(const document::Project&, ObjectId);
    ObjectSnapshot (*copy)(const document::Project&, ObjectId);
    void (*transform)(document::Project&, ObjectId, const geometry::AffineTransformUm&);
    void (*remove)(document::Project&, ObjectId, ObjectSnapshot* removed);
    void (*restore)(document::Project&, const ObjectSnapshot&);
};

[[nodiscard]] const ObjectOperationHandler* handler_for(ObjectNature nature);
[[nodiscard]] std::vector<ObjectRef> expand_to_operation_refs(const document::Project& project,
                                                              std::vector<ObjectRef> refs);
[[nodiscard]] std::optional<geometry::BoundsUm> bounds_of_refs(const document::Project& project,
                                                               const std::vector<ObjectRef>& refs);

} // namespace openstitch::commands
```

Initial handlers:

- `VectorObject`: transform path geometry and dependent embroidery params whose `source_vector` matches; delete vector plus dependent embroidery objects, preserving original indices.
- `EmbroideryObject`: transform only self-contained geometry and dependent params. For current code this primarily means satin rails/rungs/entry/exit points and manual stitch overrides. Running/tatami/directional objects tied to a source vector should normally be transformed through their vector source to avoid double-transforming the same shape.
- `ImportedDesign`: from S2a. In P0 it is copyable/deletable, not transformable unless S2a exposes a documented transformable representation. If non-transformable, transform commands skip it and report no-op.

Open point: the exact imported-design type is not present in the current worktree; wire this after S2a with the same handler shape.

### Commands

`transform_objects_command.hpp`:

```cpp
class TransformObjectsCommand final : public ICommand {
public:
    TransformObjectsCommand(std::vector<ObjectRef> refs,
                            geometry::AffineTransformUm transform,
                            std::string label,
                            std::string mergeKey = {});

    void apply(document::Project& project) override;
    void revert(document::Project& project) override;
    [[nodiscard]] std::string name() const override;

    [[nodiscard]] std::string mergeKey() const;
    [[nodiscard]] bool mergeWith(const TransformObjectsCommand& next,
                                 document::Project& project);

private:
    std::vector<ObjectRef> refs_;
    geometry::AffineTransformUm transform_;
    std::string label_;
    std::string mergeKey_;
    std::vector<ObjectSnapshot> before_;
    std::vector<ObjectSnapshot> after_;
    bool applied_{false};
};
```

`delete_objects_command.hpp`:

```cpp
class DeleteObjectsCommand final : public ICommand {
public:
    explicit DeleteObjectsCommand(std::vector<ObjectRef> refs,
                                  std::string label = "Supprimer la selection");

    void apply(document::Project& project) override;
    void revert(document::Project& project) override;
    [[nodiscard]] std::string name() const override;

private:
    std::vector<ObjectRef> refs_;
    std::vector<ObjectSnapshot> removed_;
    std::string label_;
    bool applied_{false};
};
```

`clipboard_commands.hpp`:

```cpp
struct PasteOptions {
    Vec2um offset{Micrometers{1'000}, Micrometers{-1'000}}; // normal Ctrl+V
    bool preserve_position{false};                          // Ctrl+Shift+V
};

class PasteObjectsCommand final : public ICommand {
public:
    PasteObjectsCommand(std::vector<ObjectSnapshot> sourceSnapshots,
                        PasteOptions options,
                        std::string label = "Coller");

    void apply(document::Project& project) override;
    void revert(document::Project& project) override;
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<ObjectRef> pastedRefs() const;

private:
    std::vector<ObjectSnapshot> source_;
    std::vector<ObjectSnapshot> pasted_;
    PasteOptions options_;
    std::string label_;
    bool applied_{false};
};
```

Cut is implemented as desktop clipboard write followed by `DeleteObjectsCommand`; it is one undoable document command. Copy is not an `ICommand` because it does not mutate the document.

### Command Merging (AI-07)

Extend `ICommand` minimally:

```cpp
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void apply(document::Project& project) = 0;
    virtual void revert(document::Project& project) = 0;
    [[nodiscard]] virtual std::string name() const = 0;

    [[nodiscard]] virtual std::string mergeKey() const { return {}; }
    virtual bool mergeWith(const ICommand& next, document::Project& project) {
        (void)next;
        (void)project;
        return false;
    }
};
```

Modify `UndoStack::execute`:

1. Apply the incoming command first.
2. If `undo_.back()->mergeKey()` is non-empty and equals incoming `mergeKey()`, call `undo_.back()->mergeWith(*incoming, project)`.
3. If merge succeeds, do not push incoming; clear redo.
4. Otherwise push as today.

For transform drags and keyboard nudge bursts, the merge key should include operation kind plus normalized refs, for example `transform:nudge:<ids>` or `transform:drag:<ids>`. `mergeWith` stores the latest `after_` snapshot while keeping the original `before_`, so one undo returns to the pre-burst state exactly.

### Clipboard Serialization Format

`libs/project_io/include/openstitch/project_io/project_subset.hpp`:

```cpp
#pragma once

#include <string>
#include <vector>

#include "openstitch/commands/object_operation.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::project_io {

inline constexpr std::string_view kOpenStitchClipboardMime =
    "application/vnd.openstitch.project-subset+json";

struct ProjectSubset {
    int schema_version{};
    std::vector<commands::ObjectSnapshot> objects;
};

[[nodiscard]] Result<std::string> serialize_project_subset(
    const document::Project& project,
    const std::vector<commands::ObjectRef>& refs);

[[nodiscard]] Result<ProjectSubset> deserialize_project_subset(std::string_view jsonText);

} // namespace openstitch::project_io
```

JSON shape:

```json
{
  "openstitchClipboard": 1,
  "schemaVersion": 3,
  "objects": {
    "vectorObjects": [],
    "embroideryObjects": [],
    "importedDesigns": []
  }
}
```

Rules:

- Reuse existing `.osp` JSON object sections and field names where possible.
- No image, segmentation map, preprocessing ops, canvas, or `objectIdLast` in the payload.
- Include closure dependencies automatically: copying an embroidery object includes its source vector if required for regeneration; copying a vector object includes dependent embroidery objects if they are part of the selected logical object set. This must be deterministic and documented in tests.
- Desktop owns `QMimeData`: set the custom MIME with UTF-8 JSON, and optionally set plain text like `OpenStitch object subset (N objects)`.

## 4. MainWindow / CanvasView Integration

Current code checked in this worktree still has:

- `selectedRegion_`, `selectedObject_`, `selectedEmbroidery_` in `apps/desktop/main_window.hpp`.
- Single-object keyboard nudge connected in the constructor around current `main_window.cpp:447-461`.
- `Delete` bound to `deleteSelectedRegion()` in `buildMenus()` around current `main_window.cpp:607-610`.
- Single-select canvas hit testing in `onCanvasClicked()` around current `main_window.cpp:6595-6765`.
- Selection invalidation at the head of `refreshImage()` around current `main_window.cpp:2138-2145`.
- Single-select rendering/handles in `renderBase()` around current `main_window.cpp:2227+`.
- `DocumentPanel` is single-selection (`embroiderySelected(ObjectId)`, `regionSelected(RegionId)`).

These line numbers are current before S2a; because S2a is expected to edit adjacent code, implementers should re-run the same searches after rebasing.

S3-owned functions/sections per architecture, and planned changes:

- `apps/desktop/main_window.hpp`: remove `selectedRegion_`, `selectedObject_`, `selectedEmbroidery_`; add `SelectionModel* selection_{nullptr};` and QActions for copy/cut/paste/delete/select-all/rotate/mirror/center/whole-design transforms.
- Constructor / `buildMenus` / `buildMainToolbar` / `buildToolPalette`: instantiate `SelectionModel`, connect `selectionChanged` to `displayImage(processed_)` and `updateActions()`. Add Edit menu actions: Copy, Cut, Paste, Paste in Place, Delete, Select All. Add Object/Transform actions or Broderie submenu actions for Rotate, Mirror Horizontal, Mirror Vertical, Center in Hoop, Rotate Design 90, Mirror Design H/V.
- Existing `buildMenus` Edit section: add S3 actions after Undo/Redo. Move Delete shortcut off the segmentation-only action; `Delete` triggers new `deleteSelection()`.
- `resetDocumentState`: replace three reset calls with `selection_->clear()`.
- `refreshImage`: replace current ad hoc invalidation with `selection_->retainExisting(project_)`; keep this near the current invalidation head so S9 can later take over the same function.
- `onCanvasClicked`: route hit results through `selection_`.
  - Plain click: set single hit or clear.
  - Ctrl/Shift-click: toggle hit.
  - Satin direct hit remains asynchronous as today, but it calls `selection_->setSingle(embroideryObject(hitId))`.
  - Region selection remains possible only when no object hit and segmentation is visible; it clears object selection.
- `onBoxDrawn`: currently only draw tools use box rubber-band. In `Tool::Select`, treat `boxDrawnMm` as selection rectangle instead of shape creation. Add a `CanvasView` selection/rubber-band mode for Select without colliding with crop/draw modes. Select objects whose bounds intersect the box; Shift/Ctrl adds/toggles.
- `CanvasView::mousePressEvent/mouseMoveEvent/mouseReleaseEvent`: preserve existing draw/crop behavior; add selection rubber-band only for select mode and empty-space drag. Also add object-drag signal if S3 chooses to move selected object bodies from `CanvasView` instead of `VectorObjectBodyItem`.
- `CanvasView::keyPressEvent`: keep arrow emission, but make deltas exactly 0.1 mm and Shift = 1 mm. Let MainWindow decide selection applicability.
- Constructor `nudgeRequestedMm` lambda: replace single `TranslateVectorObjectCommand` with `TransformObjectsCommand(selectionRefs, translation(delta), "Deplacement", mergeKey)`.
- `renderBase`: use `selection_->contains(...)` for highlighting. For multi-selection, draw a common bounding box and transform handles. Per-node handles should only show for a single selected vector object to avoid ambiguous edits.
- `syncDocumentSelection`, `refreshDocumentPanel`, `buildDocumentPanel`: change DocumentPanel signals to multi-selection. Canvas-to-panel sync calls `DocumentPanel::syncSelection(std::vector<SelectionItem>)`.
- `buildPropertiesPanel` / `updateInspector`: preserve single-object inspector when `selection_->single()`. For multi-selection, show compact summary only; do not implement HP-OBJ-019 editable multi-inspector.
- `resolveSelectedEmbroidery`, `currentFillObject`, `updateContextToolbar`, creation commands: read `selection_->primary...()` to preserve current single-selection behavior.
- `deleteVectorObject`, `deleteEmbroideryObjectOnly`, `deleteSelectedRegion`: keep helper functions if needed for context menus, but Delete key should call `deleteSelection()` and dispatch to `DeleteObjectsCommand` or existing `RemoveRegionCommand`/new multi-region command.
- `duplicateVectorObject`, `offsetVectorObjectCore`: leave single-object behavior, or use primary item only; broad multi-duplicate is not in S3 except clipboard paste.
- `buildOrderPanel`, `refreshOrderPanel`, `moveObjectUp`, `moveObjectDown`: enable multi-row selection in `orderList_`; selection changes update `SelectionModel`. Reordering multiple rows is not required unless already straightforward; do not expand scope beyond selection synchronization.
- `apps/desktop/tools.hpp`: add a `Tool::Select` selection-rubber-band distinction if current enum lacks enough state.

## 5. Tests

Catch2 - geometry:

- `transform_point translation uses integer micrometers exactly`
- `rotation by 360 degrees is identity at micrometer precision`
- `rotation by 90 degrees around pivot transforms path nodes and tangents`
- `mirror horizontal around center flips y and mirrors angles`
- `mirror vertical around center flips x and mirrors angles`
- `bounds_of path_set includes holes and all pieces`
- `transform_path preserves closed flag`

Catch2 - commands:

- `TransformObjectsCommand translates multiple vector objects in one undo`
- `TransformObjectsCommand rotates tatami angle with geometry`
- `TransformObjectsCommand rotates directional guides and break lines`
- `TransformObjectsCommand mirrors satin rails rungs entry and exit points`
- `TransformObjectsCommand skips non transformable imported design without partial mutation`
- `DeleteObjectsCommand removes multiple vectors and dependent embroideries preserving undo order`
- `PasteObjectsCommand allocates new ids and rewires source_vector links`
- `PasteObjectsCommand paste in place preserves coordinates`
- `PasteObjectsCommand normal paste applies deterministic offset`
- `UndoStack merges consecutive transform commands with the same merge key`
- `UndoStack does not merge after undo branch or different selection`
- `whole design center command moves all transformable refs into hoop center`
- `whole design rotate 90 command is one undoable command`

Catch2 - project_io:

- `serialize_project_subset writes only selected object subset`
- `deserialize_project_subset round trips vector plus embroidery params overrides`
- `subset includes source vector for copied embroidery object`
- `subset preserves deterministic object order`
- `subset rejects unknown clipboard marker or future incompatible version`

QTest - desktop:

- `SelectionModel set add toggle clear emits one selectionChanged per change`
- `SelectionModel retainExisting drops deleted ids`
- `SelectionModel selectAllObjects follows document order`
- `DocumentPanel supports multi selecting embroidery rows`
- `DocumentPanel syncSelection reflects canvas multi selection without reemitting`
- `Canvas shift click adds object to selection`
- `Canvas ctrl click toggles object out of selection`
- `Canvas rubber band selects all intersecting objects`
- `CtrlA selects all visible/selectable objects`
- `Delete removes selected objects with one undo`
- `Delete on selected region still removes the region`
- `CtrlC CtrlV round trips object through QClipboard`
- `CtrlX writes clipboard and deletes selection with one undo`
- `Arrow nudge moves all selected objects by 0.1 mm`
- `Shift arrow nudge moves by 1 mm`
- `Consecutive arrow nudges undo as one command`
- `Rotate exact angle updates selected object and tatami angle`
- `Rotate 360 leaves geometry identical at micrometer precision`
- `Mirror horizontal and vertical update geometry and dependent params`
- `Transform whole design center in hoop affects all objects`
- `Multi selection shows summary and does not expose editable multi inspector`

## 6. Docs to Update

`docs/roadmap-parite-hatch.md`:

- Update only HP-OBJ-001, HP-OBJ-002, HP-OBJ-003, HP-OBJ-004, HP-OBJ-006, HP-OBJ-014, HP-OBJ-016.
- Follow section 0.1 entry format and 0.3 status convention.
- Mark as done only after tests pass, with date and commit. Include a short `Livre : ...` line per entry and leave exclusions explicit for HP-OBJ-005/019.

Architecture ownership docs:

- Add a new `docs/source/selection-and-transforms.md` page, as allowed by the S3 row in the documentation table.
- Add S3 bullets/lines to `docs/source/module-reference.md` and `docs/source/limitations.md` only. The table says structure unchanged; use additive bullets.
- Do not regenerate the docs PDF; S15 owns the derived PDF.

## 7. Validation Commands

From `CLAUDE.md`, use actual preset names:

```powershell
cmake --build --preset msvc-debug --target openstitch_geometry openstitch_commands openstitch_project_io openstitch_desktop_widgets
ctest --preset msvc-debug -R "geometry|commands|project_io|selection|canvas|document_panel|main_window"
cmake --build --preset linux-core --target openstitch_geometry openstitch_commands openstitch_project_io
git ls-files '*.cpp' '*.hpp' | xargs clang-format --dry-run --Werror
```

Full-suite check before merge:

```powershell
cmake --build --preset msvc-debug
ctest --preset msvc-debug
cmake --build --preset linux-core
```

If docs/source is updated:

```powershell
powershell -File docs/scripts/build-docs.ps1
```

## 8. Open Questions / Risks

1. AI-06 exact dispatch signature is architecture-owned by S3 but not prescribed. The function-pointer handler table above is deliberately simple and Qt-free; validate before coding.
2. Decide whether to keep separate `RotateObjectsCommand` / `MirrorObjectsCommand` classes or use one parameterized `TransformObjectsCommand`. This plan recommends one parameterized command with labels because move/rotate/mirror share snapshots, merging, and per-nature dispatch.
3. Clipboard subset format should reuse existing `.osp` JSON sections, but current `json_serialize` helpers are private. Implementation may need to expose narrowly scoped subset helpers without exposing nlohmann_json in public headers.
4. S2a imported-design type is absent in this worktree. S3 must wire its handler after S2a lands; default P0 behavior should be copy/delete yes, transform no unless S2a provides transformable geometry.
5. Multi-selection and existing single-object node handles conflict visually. This plan keeps node/Bezier handles single-selection only and shows common bbox handles for multi-selection.
6. Current Delete shortcut is segmentation-specific. Rebinding it changes behavior intentionally for HP-OBJ-016, but region deletion must remain reachable through the unified selection path.
7. Rotation/mirror of manual stitch overrides is load-bearing: overrides store generated point deltas in document coordinates. If an object is transformed, overrides should transform with it or be discarded/marked dirty by explicit policy; decide before implementation.