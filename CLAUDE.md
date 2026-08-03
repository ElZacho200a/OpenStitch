# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

OpenStitch Studio (temporary name — see `docs/phase0/` ADRs) is a free/open-source
Windows desktop app for machine embroidery digitizing: raster image → editable
vector regions → embroidery objects → stitch points → DST file. C++23, Qt 6
Widgets for the desktop UI only, Apache-2.0. The core never depends on Qt or on
any GPL code (verified by a Linux CI build of the core+CLI without Qt).

Full documentation lives in `docs/source/*.md` (one chapter per topic) and is
compiled into a PDF via `docs/scripts/build-docs.ps1`. Read `docs/source/satin.md`
before touching anything satin-related — it's the detailed technical reference
for that subsystem (skeleton extraction, junction anchoring, routing, guides),
including root-cause writeups for every non-trivial bug fixed there.

## Build & test commands

Requires Visual Studio 2022+ (C++ workload), CMake ≥ 3.27, vcpkg, and Qt 6.8.3
(official binaries, outside vcpkg). Set `VCPKG_ROOT` and `QT_ROOT` env vars first.

```powershell
cmake --preset msvc                       # configure (first run is slow: vcpkg builds OpenCV etc.)
cmake --build --preset msvc-debug         # build Debug
cmake --build --preset msvc-release       # build Release
ctest --preset msvc-debug                 # run all tests (Debug)
ctest --preset msvc-release               # run all tests (Release)
```

`scripts/build.ps1` wraps the above (configure + build Debug and/or Release,
optional `-Test`): `.\scripts\build.ps1 -Test` builds everything and runs the
full suite in one command.

Build/test a single library or its test target directly (faster than a full
build during iteration):

```powershell
cmake --build --preset msvc-debug --target openstitch_stitch_generation test_stitch
ctest --preset msvc-debug -R tatami                              # filter CTest by name substring
build\msvc\tests\unit\stitch\Debug\test_stitch.exe "some name"   # run one Catch2 TEST_CASE by name/substring
build\msvc\tests\unit\stitch\Debug\test_stitch.exe "[tag]"       # or by Catch2 tag
```

`linux-core` preset builds the core libs + CLI **without Qt** — a portability
guard, not a full app build; there is no desktop build on Linux yet.

Format check (matches CI, does not modify files):
```bash
git ls-files '*.cpp' '*.hpp' | xargs clang-format --dry-run --Werror
```

Regenerate the documentation PDF after editing anything in `docs/source/`:
```powershell
powershell -File docs/scripts/build-docs.ps1
```

## Architecture

**Layering is strict and acyclic**: `apps/{desktop,cli} → libs/* → libs/core`.
Only `apps/desktop` depends on Qt; everything else must stay Qt-free (this is
enforced by the `linux-core` CI job, which builds the core + CLI with no Qt at
all). Each lib is one CMake target `openstitch::<name>` with its own test
directory under `tests/unit/<name>/`. Third-party libraries are encapsulated
behind an internal interface and never leak their types across a lib boundary:
Clipper2 lives inside `geometry`, OpenCV inside `image`/`segmentation`/
`vectorization`, the DST codec inside `formats`, JSON/minizip inside `project_io`.

Module responsibilities (see `docs/source/module-reference.md` for the full
table and "where do I change X" shortcuts):

| Module | Responsibility |
|---|---|
| `core` | strong unit types (µm/mm/px), ids, `Result<T>`, logging |
| `geometry` | paths, simplification, boolean ops/offsets, arc-length |
| `image` | loading, non-destructive preprocessing |
| `segmentation` | CIELAB quantization, connected regions |
| `vectorization` | regions → clean vector contours |
| `document` | project/object data model |
| `stitch` | machine commands, stats |
| `stitch_generation` | running/tatami/satin point generation, routing, manual overrides |
| `auto_satin` | skeleton extraction → satinability → auto rails/rungs, multi-section networks |
| `stitch_analysis` | pre-export validation rules |
| `optimization` | stitch order |
| `autodigitize` | image → editable objects automatically |
| `commands` | undo/redo (Command pattern) |
| `formats` | DST codec, diagnostic SVG export |
| `project_io` | `.osp` project format (JSON + ZIP) |

**No coordinate is ever a raw `double`.** Internal geometry is `Micrometers`
(`int32`) / `Vec2um`, Y axis pointing up. Convert at boundaries only: px→µm on
import (explicit resolution), µm→mm for display, µm→0.1mm for DST export.

**All document mutation goes through a `commands::ICommand`** pushed onto an
`UndoStack` (`apply`/`revert`/`name`) — never mutate `document::Project` directly
from UI code.

**Stitch points are never stored** — they're derived. `generate_sequence(project)`
computes the raw sequence from the document; `apply_manual_overrides` then
patches it with any manual edits (Lot 8, ADR-014). Every production consumer
(preview, export, analysis, simulation) MUST call
`stitch_generation::effective_sequence(project)` (which chains both steps), not
`generate_sequence` directly. This is enforced by a structural CTest
(`tests/check_no_raw_sequence_bypass.cmake`) that fails the build if a new
production call site outside `libs/stitch_generation/` calls `generate_sequence`
without an explicit `raw-sequence-ok:` annotation (the only legitimate exceptions
today are synthetic CLI debug generators).

**The desktop app has no business logic.** `apps/desktop` wires actions, renders,
and triggers recomputation; everything else (image ops, segmentation,
generation, analysis, order, formats) lives in the libs — this is directly
checkable since the CLI exercises the same modules without Qt.

**No async task infrastructure exists yet** — heavy operations (segmentation,
fills) run synchronously. Write new generation code as pure functions over
immutable snapshots so it stays compatible with a future background-task move.

## Testing conventions

- Catch2 v3 for core/CLI (`tests/unit/<lib>/test_*.cpp`, one executable per lib);
  Qt6::Test (QTest/QSignalSpy) for desktop UI (`tests/unit/desktop/`), always run
  **headless** (`QT_QPA_PLATFORM=offscreen`), no pixel comparisons, no `sleep`.
- **Catch2 `TEST_CASE` names must be plain ASCII** — accented characters break
  test discovery under the Windows console encoding, even though French prose
  elsewhere in the codebase/docs uses accents freely.
- Determinism is a hard requirement almost everywhere: same input → byte-identical
  output across two runs (checked explicitly in many tests). Avoid unordered
  containers where iteration order would leak into output; sort explicitly.
  DST round-trip is checked byte-for-byte.
- Golden SVGs under `tests/golden/` are diagnostic references, never rewritten by
  tests — regenerate explicitly via `openstitch-cli stitchdebug` /
  `auto-satin-debug --output-svg`.
- To add a test: a `TEST_CASE` in the module's `test_*.cpp`, or a new file wired
  into that test dir's `CMakeLists.txt`. For desktop: a function in an existing
  QTest suite, or a new file + `openstitch_add_qt_test(...)`.

## Known pitfalls in this codebase

- spdlog header is `stdout_color_sinks.h`.
- A member named `slots` is forbidden (Qt macro collision) — pick another name.
- Forward-declare Qt classes at **global scope**, not inside `namespace
  openstitch::...`.
- No `M_PI` under MSVC — use `std::numbers::pi` (`<numbers>`).
- Exact vcpkg target names: `Clipper2::Clipper2`, `MINIZIP::minizip-ng`.
- `RasterTransform`/µm↔pixel conversions in `libs/auto_satin` truncate toward
  zero, not round — matters at sub-pixel precision only.
