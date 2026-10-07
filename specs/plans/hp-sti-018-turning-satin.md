# Redesign Plan: Satin Junction Core Replacement + Turning Satin (HP-STI-018)

Status: draft (architecture ready for review gate; clean-slate scope, test regression explicitly authorized)

This plan replaces an earlier, narrower draft that proposed an additive ring-peel feature reused on top of the existing junction-resolution code. That draft is not wrong in its mechanics (the ring-peel construction is carried forward unchanged, §2.3 below) — it was wrong in scope: it treated `libs/auto_satin/src/satin_column.cpp`'s junction-resolution pile as stable ground to build on. It is not. This plan replaces the pile's root cause instead.

## 0. Grounding: what the code actually does today

`auto_satin::build_satin_columns(region, params)` (`libs/auto_satin/src/satin_column.cpp:2815-3102`) pipelines: rasterize → `distance_transform` (`distance_field.hpp:28`, a per-pixel unsigned distance-to-boundary field, **already a discretized SDF**) → Zhang-Suen thinning → `build_skeleton_graph` (`skeleton_graph.hpp:44`, carries `local_radii_um` **sampled straight from that same distance field** along every edge) → `evaluate_satinability` → per-skeleton-edge column construction.

Column construction (`compute_column_stations`, `satin_column.cpp:871-1220`) does **not** reuse `local_radii_um`. Instead, at every arc-length station it computes a tangent by central difference, rotates it 90° to a normal, and calls `cross_section` (`satin_column.cpp:161-208`): a single ray cast from the axis point along that normal, intersected against the whole contour, keeping the smallest bracketing interval. This is the one and only width/rail-touch-point measurement primitive in the file; `build_column` (Legacy) and `build_parametric_object` (Parametric) both consume it unchanged (confirmed shared, `docs/source/satin.md:4404-4405`).

Everything downstream of a junction node exists to patch failures of that one primitive near a confluence: `trim_unstable_junction_tail` (`satin_column.cpp:830-870`, amputates stations whose width "drifts" vs. a representative-width heuristic), `StableBranchEnd`/`JunctionSeparator`/`JunctionSectorInfo`/`JunctionCore` (`satin_column.hpp:158-205`, built by `resolve_junction`, `satin_column.cpp:2568-2734`), and inside `resolve_junction` itself a **bounded iterative retraction loop** (`satin_column.cpp:2595-2629`) that exists specifically because a width-stable station can still be geometrically inside a neighboring branch's corridor (`docs/source/satin.md:690-703`) — i.e., because `cross_section`'s ray can silently measure the wrong lobe of the boundary near a confluence. `extend_tip` (`satin_column.cpp:681-780`) is a second, independent re-implementation of "march + shrink-detect + bisect," also built on `cross_section`.

**A key fact informing this redesign** comes from the junction code's neighborhood, though not from it directly. The 2026-08-30 "topological decomposition" investigation (`docs/source/satin.md:5245-5407`) isolated a cause of the `E` fixture's 100%-residual refusal: a **90° corner with zero junctions**, where a single `cross_section` ray cast through a concave-to-convex width transition surfaces far from the real local width. **Correction after review (the first draft of this plan mis-cited the mechanism — verify this version against the doc directly before trusting it further):** the doc's own Hypothesis 1 experiment (lines 5310-5317) *tested and rejected* the "diagonal central-difference tangent" explanation — swapping in one-sided tangents produced "AUCUN changement — échec identique à l'octet près," and the doc explicitly states "le problème n'est donc pas la direction de la tangente au point d'échantillonnage." The validated mechanism (Hypothesis 3, lines 5326-5333) is **direction-independent**: "quelle que soit sa direction locale," a ray through a concave notch at a width transition can surface on the far convex side regardless of which way it's cast. Three targeted patches were prototyped and all three were rejected (two no-ops, one fixed `E` but regressed `trident`'s legitimate tapering tip) — because all three patched *which direction* to cast the ray rather than the *mechanism* (a single ray, cast any direction, can overshoot past a nearby feature at a width transition).

**Architectural bet, not an established fact — flagged explicitly per review finding B3:** this plan's premise that junction-drift (`StableBranchEnd`/`JunctionSeparator`'s reason to exist, documented separately at `docs/source/satin.md:636-725`/`690-703`) and the `E` corner refusal share the same root defect (`cross_section`'s single-ray overshoot) is **this plan's own inference**, not a conclusion the cited documentation states. The same `satin.md:5245-5407` section explicitly warns against exactly this kind of generalization: `E`, `multi_neck`, and `two_holes` share the same *surface symptom* (100% residual refusal) but "relèvent de TROIS causes distinctes — aucun correctif unique ne les résoudra toutes les trois" (lines 5293-5301). The unification bet is plausible (both bug classes do consume the same `cross_section` primitive) and is the actual justification for building `nearest_boundary_feet` as a shared replacement — but it is a hypothesis this plan's Phase B/C must validate empirically against the corpus (§3, §4), not a premise to treat as already proven. If Phase B's side-by-side harness shows the `E` fixture succeeds under the new primitive while the junction-drift corpus (`t`/`trident`) does not improve, the two bug classes are more separate than this plan assumes, and §1's "one root cause" framing should be revisited rather than forced.

This also explains why SGSD (`libs/satin_planning`) is not dead weight and cannot be ignored: `try_local_satin` (`satin_plan.cpp:58-60`) calls `build_satin_columns` on the whole region before any decomposition is attempted; and `decompose_and_recurse`'s second cut-candidate family harvests `junction_separators` by calling `build_satin_columns` a second time in forced-`Legacy` mode purely as a side channel (`satin_plan.cpp:249-253`: `legacyParams.geometry_mode = Legacy; const auto legacyResult = build_satin_columns(...); baseCutParams.junction_separators = legacyResult.junction_separators;`). That dependency must be replaced with a real API, not preserved as a side effect of a full column build.

## 1. Decision

Replace the single ray-cast cross-section primitive (`cross_section`) with a **nearest-boundary-feet** primitive, and rebuild junction handling on top of a **principled stability criterion** (foot multiplicity) instead of width-drift heuristics + iterative crossing-retraction. Keep `JunctionSeparator`/sector/`JunctionCore` partition logic (already principled — contour-contiguity search, star-polygon-by-construction core) but feed it from the new, reliable corridor-end data instead of deleting it. Fold HP-STI-018 in as the **second canonical rail-construction recipe** available from the same upstream analysis (iso-offset ring peeling for shapes whose medial axis degenerates), dispatched alongside corridor tracing rather than bolted on. Give `libs/satin_planning` a dedicated analysis-only API instead of a side-channel full build.

This is not a generic "switch to SDF/medial-axis theory" rewrite for its own sake — it is justified by one specific, already-documented, already-investigated defect (`cross_section`'s ray overshoot) that is independently responsible for both the corner-refusal bug class (`E`) and the junction-drift bug class (StableBranchEnd's entire reason to exist). Fixing the shared cause lets the patch pile shrink to what's actually load-bearing.

### 1.1 Why not keep `cross_section` and only rewrite the junction layer (i.e., closer to the superseded plan's spirit)

Rejected: it leaves the `E`-class corner defect in place (it is not a junction bug, `satin_column.cpp` has no code path specific to corners), and it leaves `resolve_junction`'s iterative crossing-retraction loop as a necessary safety net rather than an unnecessary one — the retraction loop only exists because `cross_section` can't be trusted not to measure a neighbor's corridor near a confluence. A junction-only rewrite would still be patching a bad measurement primitive, just with nicer names.

### 1.2 Why not a full SDF/medial-axis continuous reformulation (grid marching squares, Voronoi diagram of the boundary, etc.)

Rejected as unnecessarily large: the polygon contour is already available exactly (not rasterized) everywhere `cross_section` is called — `region_polys(region)` (used throughout `satin_column.cpp`) gives exact µm-integer polygons, and `distance_to_polys`/`project_to_contour` (already present, `satin_column.cpp:101-115`, `318-...`, today used only for diagnostics) already do exact nearest-point-on-polygon queries. Nearest-boundary-feet can be built directly on the exact polygon contour with no new raster/Voronoi machinery, reusing primitives that already exist and are already tested indirectly (via `distance_to_polys`'s use in `JunctionCore` diagnostics). A full continuous SDF reformulation would duplicate this for no additional correctness benefit and would risk breaking the µm-integer/determinism discipline (Tier 1) by introducing a second geometric representation of the same contour.

## 2. New architecture

### 2.0 Prerequisite extraction (Phase A0 — added per review finding B1)

`P2`, `Poly`, `region_polys`, `in_region`, `distance_to_polys`, `project_to_contour`, and `cross_section` all live inside a single anonymous namespace spanning `satin_column.cpp:20-2766` — invisible outside that translation unit (confirmed: zero matches for `P2`/`Poly` under `libs/auto_satin/include/`). The new headers below (§2.1/§2.2) cannot compile against these types until they're promoted out. **Before Phase A**: extract `P2`, `Poly`, `region_polys`, `in_region`, `distance_to_polys`, `project_to_contour` (signatures and bodies unchanged — pure move, not a rewrite) into a new internal header `libs/auto_satin/src/geometry_detail.hpp`, included by `satin_column.cpp` and the new `medial_field.cpp`/`corridor.cpp`. This is a behavior-preserving refactor with its own CTest run (existing `test_auto_satin`/`test_satin_planning` must pass unchanged) before any new code lands on top — treat it as "Phase A0," gating Phase A.

### 2.1 `medial_field.hpp` / `medial_field.cpp` (new, `libs/auto_satin`)

The one new geometric primitive. Operates on the same `std::vector<Poly>` (`region_polys` output) `cross_section` already consumes — no new input representation.

```cpp
// Point on a source contour nearest to a queried interior point, with enough
// identity to support continuity-based tracking across consecutive stations
// (never re-chosen by raw distance alone, which cannot distinguish two
// near-equal feet at a corner or confluence).
struct BoundaryFoot {
    Vec2um point;
    std::size_t poly_index;  // index into region_polys() (0 = outer, 1.. = holes)
    std::size_t edge_index;  // edge of that polygon carrying this foot
    double edge_t{0.0};      // 0..1 along that edge
    double distance_um{0.0};
};

struct FootQuery {
    int max_feet{3};                  // 2 = ordinary corridor; a 3rd arriving signals a junction event
    double tolerance_relative{0.02};  // feet within 2% of the minimum distance are "tied" (multiplicity)
};

// Nearest point(s) on the region's contour(s) to an interior point `p`, up to
// `max_feet`, sorted by distance, excluding points whose distance exceeds
// min_distance * (1 + tolerance_relative). O(E) per query (same complexity
// class as cross_section's intersection loop, no asymptotic regression).
// Deterministic tie-break on exact ties: smaller poly_index, then edge_index.
[[nodiscard]] std::vector<BoundaryFoot> nearest_boundary_feet(const std::vector<Poly>& polys, P2 p,
                                                                const FootQuery& query = {});
```

This directly replaces `cross_section`'s ray-intersection: instead of casting one ray and hoping it brackets the axis point correctly, it finds the actual nearest contour point(s), which by definition cannot overshoot past a corner or into a neighboring branch's territory the way a straight ray can.

### 2.2 `corridor.hpp` / `corridor.cpp` (new, `libs/auto_satin`; absorbs most of `satin_column.cpp`'s junction code)

```cpp
struct CorridorStation {
    P2 axis_point;
    BoundaryFoot foot_a;        // left-of-travel foot (replaces rail_a_point)
    BoundaryFoot foot_b;        // right-of-travel foot (replaces rail_b_point)
    P2 tangent;
    double width_um{0.0};
    int foot_multiplicity{2};   // 2 = ordinary; >=3 = a third contour feature is within tolerance (junction proximity)
    bool interpolated{false};   // unchanged semantics from today's Station::interpolated
};

// Replaces compute_column_stations's inner measurement step. Walks the given
// axis polyline, querying nearest_boundary_feet at each arc-length sample and
// selecting foot_a/foot_b by CONTINUITY with the previous station (same edge,
// or the nearest-adjacent edge on the same polygon) rather than a fresh
// left/right-of-tangent test every time -- generalizes the existing station
// code's stated invariant ("jamais une association par index") to feet
// instead of ray-intersection intervals. First station of a branch falls back
// to a left/right-of-tangent split among the returned candidate feet.
[[nodiscard]] std::vector<CorridorStation> trace_corridor(const std::vector<P2>& axis,
                                                            const std::vector<Poly>& polys,
                                                            const SatinColumnsParameters& params);

// Replaces trim_unstable_junction_tail's width-drift/plateau heuristic with a
// single structural criterion: walking inward from a junction-node end,
// the StableCorridorEnd is the first station whose foot_multiplicity == 2
// AND whose next `junction_stability_margin_stations` consecutive stations
// also have foot_multiplicity == 2 (new SatinColumnsParameters field,
// default 3). A station still "contaminated" by a neighboring branch's
// boundary necessarily has foot_multiplicity >= 3 at that radius -- this is
// true by construction of nearest_boundary_feet, not by inference -- so a
// StableCorridorEnd can never be geometrically inside a neighbor's corridor.
// This eliminates resolve_junction's iterative crossing-retraction loop as
// the PRIMARY mechanism; a small bounded retraction is kept as a defensive
// fallback only (see §4).
struct CorridorEnd {
    std::uint32_t edge_id{0};
    bool at_end{false};
    CorridorStation station;
};
[[nodiscard]] CorridorEnd find_stable_corridor_end(const std::vector<CorridorStation>& stations,
                                                     bool atEnd, const SatinColumnsParameters& params);
```

`JunctionSeparatorInfo`/`JunctionSectorInfo`/`JunctionCore` (`satin_column.hpp:158-205`) and the separator/sector/core *construction* logic (`build_separator`, `build_sector`, the `JunctionCore` two-pass simple-polygon assembly, `satin_column.cpp:2535-2734` minus the retraction loop) are **kept**, moved into `corridor.cpp`, and re-fed by `CorridorEnd` instead of `StableBranchEnd`. This logic was already the fix for the "region-entire-minus-columns" and "wrong-side self-crossing polygon" bugs (`docs/source/satin.md:636-725`), is already principled (contour-contiguity search, not raw angular sectors; `leading_point`/`trailing_point` derived from `tangent` rotated 90°, not from ambiguous `rail_a`/`rail_b` labels), and is not part of the ad hoc pile this redesign is targeting — it is kept verbatim in behavior, only its inputs change.

`extend_tip` (`satin_column.cpp:681-780`) keeps its outer algorithm (march along tangent, bisection closure on `in_region`, bounded step/bisection counts) — that part is sound and general-purpose — but its per-step width probe is swapped from `cross_section` to `nearest_boundary_feet`-derived width, so open-tip extension also becomes corner-robust.

`RailConstructionMethod` (`satin_column.hpp:299-311`) gains a third value:

```cpp
enum class RailConstructionMethod : std::uint8_t { Corridor, IsoOffsetRing, ContourCorrespondence };
// Corridor: renamed from AxisStation, same role, new implementation (§2.2).
// IsoOffsetRing: new (§2.3).
// ContourCorrespondence: unchanged, still reserved/unused.
```

### 2.3 Turning satin (HP-STI-018) as the degenerate-axis recipe, not a bolt-on

A round or wide shape's issue is not that `trace_corridor`'s width measurement is wrong — it's that there is no good 1D medial-axis branch to trace in the first place (Zhang-Suen skeletonization on a disc is unstable/near-trivial, which is exactly why `evaluate_satinability` already flags it `Ambiguous`). The corridor-tracing fix does not solve this; a structurally different recipe is needed, built on the *same* upstream `DistanceField`/contour data: concentric iso-distance offset contours as the two rails.

This is exactly what the superseded plan's design already covered (`build_ring_band_sections` extracted from `build_annular_sections`, `build_turning_satin_sections` peeling via `geometry::inset_path_set`, `turning_satin_ring_width`/`turning_satin_max_rings` params, partial-acceptance policy relying on `satin_planning`'s existing residual-repair/structural-gap machinery). That design work is **carried forward unchanged** into this redesign — it was not wrong, it was mis-scoped as an independent feature. It becomes the `IsoOffsetRing` branch of one dispatcher inside `build_satin_columns`:

```cpp
// Inside build_satin_columns, replacing the current hardcoded
// "if (region.holes.size() == 1) { build_annular_sections(...) }" special case:
if (region.holes.size() == 1) {
    // same mechanics as today's build_annular_sections, now literally
    // build_ring_band_sections(outer, inner, ...) -- unconditional single ring.
} else if (region.holes.empty() &&
           (status == Ambiguous || (status == Unsuitable && report.has_wide_area))) {
    // build_turning_satin_sections: peel region.outer into concentric rings
    // via inset_path_set, call build_ring_band_sections per band, accept
    // partial results (carried from the superseded plan's design, verbatim).
} else {
    // Corridor dispatch (§2.2), today's RequiresDecomposition/Suitable paths.
}
```

No new geometric design is required here beyond what was already designed previously; the only change is organizational (it is a branch of the main dispatcher, sharing `RailConstructionMethod` bookkeeping, not a separately-invoked feature) and the open questions from that earlier design carry forward unresolved — see §8 below.

### 2.4 `libs/satin_planning` interface (replaces the side-channel harvest)

New pure analysis function in `libs/auto_satin`, no rail/rung construction, reusing `analyze_region` + `trace_corridor` + the separator-finding logic only:

```cpp
// junction_description.hpp (new)
struct JunctionSeparatorPoint {
    Vec2um point;
    std::uint32_t branch_before_edge_id;  // SkeletonEdge::id
    std::uint32_t branch_after_edge_id;
};
struct JunctionDescription {
    std::uint32_t node_id;
    Vec2um position;
    std::vector<JunctionSeparatorPoint> separators;  // corridor-contiguity order
    double local_radius_um{0.0};
};
[[nodiscard]] std::vector<JunctionDescription> describe_junctions(const geometry::PathSet& region,
                                                                    const SatinColumnsParameters& params);
```

`satin_plan.cpp:249-253` changes from `build_satin_columns(region, legacyParams).junction_separators` to `describe_junctions(region, legacyParams)` — same information, no rail/rung/stitch-order construction wasted per call, and the dependency is now a documented API rather than a side effect of a full build keyed off a hidden `geometry_mode` flag. This is cheaper (skips `build_column`'s rail/rung assembly entirely) and removes the "narrow harvesting of a diagnostic field" smell. `SatinJunction` in `libs/satin_planning/include/openstitch/satin_planning/topology.hpp:66-71` is unaffected — it is `satin_planning`'s own node/branch description derived straight from `SkeletonGraph`, orthogonal to this.

### 2.5 What is explicitly deleted

- `cross_section` and `CrossSectionFailure` (replaced by `nearest_boundary_feet` + a thinner failure enum: `AxisOutsideRegion`, `NoFeetFound`, `TooNarrow`, `IntervalOutsideRegion` — `TooWide` has no equivalent because nearest-point search cannot overshoot).
- `trim_unstable_junction_tail`, `representative_station_width` (replaced by `find_stable_corridor_end`'s multiplicity criterion).
- `resolve_junction`'s iterative crossing-retraction loop (`satin_column.cpp:2595-2629`) as the *primary* mechanism — see §4 for what (if anything) remains as a defensive fallback.
- `reflex_vertices`'s role as a *global* list searched per-branch independently is gone (it was already replaced by contour-contiguity search per the `docs/source/satin.md:676-689` fix) — kept only inside `build_separator`, unchanged.

### 2.6 Parametric mode

`compute_column_stations` is the shared dense-analysis layer for both `SatinGeometryMode::Legacy` and `Parametric` (`satin_column.hpp:40-47`, confirmed at both call sites `satin_column.cpp:1226` and `:1958`). Swapping its internals to `trace_corridor` benefits Parametric mode's corner-robustness, with zero *source code* change to Parametric's own junction mechanism (`extend_into_confluence`/`junction_overlap_target`, `docs/source/satin.md:1246-1270`). **Correction per review finding B4: this is not behavior-neutral.** `docs/source/satin.md:1246-1270` states `extend_into_confluence` runs "après amputation de la queue instable (`trim_unstable_junction_tail`, inchangée)" — i.e. Parametric's overlap measurement starts from wherever `trim_unstable_junction_tail` leaves the last station. §2.5 deletes `trim_unstable_junction_tail`, replacing it with `find_stable_corridor_end`'s multiplicity criterion in the *shared* layer — this moves where every Parametric confluence's overlap starts measuring from, a real geometric change, not a side-effect-free bystander. Treat Parametric as touched: Phase C must add explicit per-fixture Parametric junction-geometry checks (`t`/`cross`/`trident`, not just "re-run the existing suite, confirm no regression") to actually verify the new starting point produces correct overlap, not merely that nothing crashes.

## 3. Correctness risk and validation strategy

Each historical bug class, how the new design prevents it, and how it's proven:

| Bug class | Old mechanism | New prevention | Proof |
|---|---|---|---|
| Self-crossing rails | Generic anti-crossing station cleanup (kept unchanged — not part of the ad hoc pile) | Same cleanup, now a true backstop rather than load-bearing, since nearest-feet measurement is less prone to producing crossing candidates in the first place | Extend existing anti-crossing invariant test to full corpus incl. new fixtures |
| Junction core wrong size/shape | Two historical fixes already kept (contour-contiguity separator search; `leading_point`/`trailing_point` via tangent) | Unchanged logic, fed by `CorridorEnd` instead of `StableBranchEnd` | Assert core stays simple (non-self-intersecting) and bounded by `local_radius_um` on `trident`/`y`/`t`/`cross`/`h`; **do not** pin old mm² values (expected to change) |
| Bridge on wrong side of neighbor branch | Iterative crossing-retraction in `resolve_junction` | `find_stable_corridor_end`'s multiplicity criterion prevents acceptance of a contaminated station by construction | New test on `t` (the exact fixture that triggered the original bug, `docs/source/satin.md:690-703`): assert the thin branch's `StableCorridorEnd` lies outside the wide branch's rail-to-rail span |
| Open tip retracting short of real edge | `extend_tip`'s march+bisect | Unchanged algorithm, width probe only | Re-assert existing `capsule`/`rectangle` end-to-end length checks (38.8mm→45mm, etc.) unchanged |
| Corner/bend false refusal (`E`) | Three rejected patches, none addressing the mechanism | Nearest-point search cannot overshoot past a corner by construction | **New named fixture**: promote the isolated `"]"`-shaped `E`-trunk shape from the Aug 30 investigation (`docs/source/satin.md:5249-5270`) into `shapes.cpp` permanently; assert it now builds without refusal, with no `TooWide`-equivalent failure, and that `trident`'s legitimate tapering tip is unaffected (the exact case where the rejected patch #3 broke) |
| New risk: foot-selection flicker at near-degenerate corners (two feet nearly equidistant, continuity choice ambiguous) | N/A (new risk introduced by this design) | Continuity-first selection (prefer same/adjacent edge as previous station) before falling back to left/right-of-tangent | New `test_medial_field.cpp`: analytic shapes (circle, rectangle, L-shape) with hand-computed expected feet; assert no flicker across a dense station sweep |

Determinism (Tier 1): `nearest_boundary_feet` is a pure O(E) scan with explicit tie-breaks (poly_index then edge_index) — same determinism discipline as the rest of the file (e.g. `(y,x)`-ordered junction-pixel consolidation, `docs/source/satin.md:415-416`). Two-run byte-identical assertions extended to all new/changed test modules.

Performance: `analyze_region`'s rasterization/distance-transform/skeletonization/`SkeletonCacheScope` memoization (`auto_satin.hpp:38,62-71`) is **entirely unchanged** — this redesign only touches what happens after the skeleton graph is built. No cache-key or cache-hit-rate impact expected; confirm with the existing `docs/performance-audit.md` benchmark re-run before cutover (Tier 2 guardrail).

## 4. Phasing

Incremental, side-by-side validation before cutover, matching this codebase's own established discipline for exactly this kind of change (`docs/source/satin.md` repeatedly: "§21/§22: analyser d'abord... jamais de patch opportuniste sous la pression d'une seule fixture", and the 2026-08-30 topological refonte was deployed in explicit numbered phases, each "testable and committed separately, never touching the existing rail generator before the decomposition itself is validated", `docs/source/satin.md:2343-2345`). Rejecting big-bang: the surface touched (`satin_column.cpp`'s core measurement primitive) feeds both `SatinGeometryMode`s, two existing test suites totaling ~9000 lines (`tests/unit/auto_satin` + `tests/unit/satin_planning`), golden SVGs, a performance-sensitive cache, and `satin_planning`'s structural dependency — the house style of validating each layer against the full fixture corpus before wiring it into production is directly applicable and lower-risk than replacing everything in one commit, even with test regression authorized.

- **Phase A0** — extraction prerequisite (§2.0): promote `P2`/`Poly`/`region_polys`/`in_region`/`distance_to_polys`/`project_to_contour` out of the anonymous namespace into `geometry_detail.hpp`. Pure move, existing tests must pass unchanged — gates everything below.
- **Phase A** — `medial_field.hpp/.cpp`, `nearest_boundary_feet`. Pure new code, zero wiring into `satin_column.cpp`. `test_medial_field.cpp` on analytic shapes only.
- **Phase B** — `corridor.hpp/.cpp`, `trace_corridor`/`CorridorStation`. Wire behind a **test-only** `SatinColumnsParameters` internal flag (never exposed to CLI/desktop) so both old (`cross_section`) and new (`nearest_boundary_feet`) measurement can run side-by-side on the full `shapes.cpp` corpus in the same CTest run — this flag affects BOTH Legacy and Parametric modes the moment it flips, since `compute_column_stations` is shared (§2.6); treat both as in scope from here, not just Legacy. Regenerate (uncommitted, inspection-only) golden SVGs for visual comparison. Verify the `E`-fixture prediction empirically here, and explicitly check whether it actually correlates with any improvement on the junction-drift corpus (`t`/`trident`) — per §1's now-flagged "architectural bet, not proven fact," this is the point where that bet gets its first real evidence.

  **Phase B results (done, commit `51c4369`→corridor commit, reviewed):** `e_trunk_isolated` and the full `E` fixture both succeed under `trace_corridor` where `cross_section` refuses them — confirms the corner-overshoot hypothesis empirically, first real evidence for §1's architectural bet. **Real regression found and root-caused, not hidden**: `s` (S-curve, high curvature) and `multi_neck` (circles + thin necks) *succeed* under the old path but *fail* under `trace_corridor` (`"axe principal inexploitable"`). Root cause (confirmed by the Phase B reviewer with an independent throwaway diagnostic, not just asserted): `nearest_boundary_feet`'s candidate gathering is **direction-blind** — it returns the globally nearest contour points with no concept of "look left" vs "look right," unlike `cross_section`, which is direction-aware *by construction* (casts along ±normal, structurally guaranteed to find something on each side when something exists). At tight curvature or a sharp width transition, the two objectively-nearest points can both land on the *same* physical side, starving `trace_corridor`'s `select_feet` and producing a degenerate/missing foot; once a station picks wrong, continuity-based selection locks the next station onto the same error, so it propagates across the whole high-curvature run rather than self-correcting. A "two-query split" (wide/loose query for selection, strict query only for `foot_multiplicity`) fixed an earlier, broader regression (`ribbon`/`notch`/`pinch`) but tuning alone cannot fix this specific failure mode — it needs direction-aware gathering (query each half-plane relative to the local normal separately, not one global-nearest-K query followed by classification). Deliberately **not fixed in Phase B** (would be exactly the "patch opportuniste sous pression d'une seule fixture" this project's own norms proscribe, §21/§22 of `docs/source/satin.md`) — tracked as **Phase B.5** below, required before Phase F cutover.

- **Phase B.5 (added after Phase B review; SCOPE WIDENED after Phase C review — read this before starting Phase D/E) — direction-aware foot gathering.** Fix the root cause above: make `nearest_boundary_feet`/`trace_corridor`'s candidate gathering direction-aware (e.g., query the nearest foot per half-plane relative to the local normal independently, rather than one global-nearest-K query followed by left/right classification after the fact). Re-promote the affected shapes from `kKnownCurvatureLimitations` into the hard-asserted corpus in `test_corridor.cpp` once fixed.

  **Scope as of Phase C (widened, not still just "2 edge-case shapes"):** `s`, `multi_neck` (found in Phase B) **plus `y`, `y_symmetric`, `trident`** (found in Phase C, same root mechanism, confirmed independently by the Phase C reviewer via a standalone diagnostic — not a new defect, just more fixtures hitting the one Phase B already found). That is 5 of ~29 corpus shapes, and critically: **Y-forks and tapering/acute branches are not an exotic corner of the corpus — they are among the most common real branch topologies** (the letters Y/K/X/V, arrowheads, many logo/icon glyphs). `t`/`cross`/`h` succeeding is reassuring for right-angle junctions only; it is not evidence the architectural bet in §1 holds for junctions in general.

  **Phase C reviewer's recommendation, carried forward here verbatim because it is a real strategic call, not a implementation detail: pause before starting Phase D/E and fix Phase B.5 first.** Reasoning: (1) the failure list grew from 2 to 5 shapes without anyone touching `trace_corridor`'s selection algorithm — simply looking at more fixtures found more casualties of the same defect, suggesting it is a property of the primitive, not a couple of unlucky fixtures; (2) Phase D (`IsoOffsetRing`) is mostly orthogonal and could proceed in parallel; but Phase E (`describe_junctions`) is built directly on `trace_corridor` + the separator logic — if Phase B.5's fix changes `trace_corridor`'s selection algorithm (it will have to), Phase E's API needs re-validating against the fixed primitive anyway, so building it now means doing that validation twice.

  **Phase F (cutover) may not proceed until Phase B.5 is complete, OR an explicit, conscious human decision accepts the regression permanently** — shipping an irreversible loss of working corpus shapes with no fix and no sign-off would violate the Tier 2 "no measurable test-coverage regression" guardrail even under the clean-slate authorization (that authorization covers *test assertions* changing, not an unexamined *capability* loss).

  **Known gap in the plan's own central proof point (§3 "bridge on wrong side of neighbor branch" row), surfaced by Phase C's reviewer, must be resolved by Phase B.5 or explicitly accepted before Phase F:** the historical asymmetric-junction bug this row cites (`satin.md:636-725`/`690-703`) was originally found via `tests/unit/autodigitize/test_autodigitize.cpp`'s real segmented-image T-junction fixture, not `shapes.cpp`'s `"t"` (which is, and appears to always have been, symmetric — the plan's citation of `"t"` for this row is a mis-citation carried through drafts). `"trident"` is the actual synthetic fixture that reproduces the asymmetric case today, but it doesn't build under the corridor flag (Phase B.5 territory, above). The real historical fixture has never been run under `use_corridor_tracing_dev_only` at all (it only runs through `auto_digitize`/`create_satin_plan` with default params). **Net effect: as of Phase C, there is currently no empirical proof — old fixture or new — that this specific historical bug class is actually fixed by the new architecture.** This must be closed (get the real T-junction fixture running under the flag, or get `"trident"` building via Phase B.5) before claiming this row of §3 as validated.

  **Phase B.5 results (done, reviewed — APPROVE, zero regressions): 1/5 fixed as chartered; the other 4 reclassified into 3 distinct, individually root-caused mechanisms, none of them "unexamined."** Direction-aware gathering (`nearest_boundary_feet_oriented` in `medial_field.hpp/.cpp`, independent per-side bounded queries merged by a verified two-pointer merge, same O(E) discipline as Phase A) fixes `"s"` exactly (promoted into the hard-asserted corpus, +236 assertions, zero regressions elsewhere — independently re-verified). `"multi_neck"` and `"trident"`'s remaining wedge branch are **not** gathering-direction bugs (see Phase D's note and Phase C's results above, respectively) — fixing them here would have been solving the wrong problem. `"y"`/`"y_symmetric"` are real, scoped, and tracked as **Phase B.5b** (added below, between Phase E and Phase F) — confirmed via a scoped experiment (`extend_open_ends=false` isolates the failure entirely inside `extend_tip`) rather than left as a guess. The §3 proof-point gap directly above is **partially closed**: `"trident"`'s lateral branch (one of its two incident branches) now builds correctly under Phase B.5, though its wedge-tip branch and the real image-based historical fixture remain open — still short of full closure, tracked explicitly rather than claimed done.
- **Phase C** — Junction rewrite: `find_stable_corridor_end`, `resolve_junction` rebuilt on `CorridorEnd`, retraction loop reduced to a defensive fallback (open question, §8.3). Same side-by-side harness as Phase B, extended to `trident`/`y`/`t`/`cross`/`h`, **including explicit Parametric-mode fixtures** (`t`/`cross`/`trident` under `SatinGeometryMode::Parametric`) per §2.6's correction — assert `extend_into_confluence`'s overlap still lands correctly from the new `CorridorEnd`-derived starting station, not just "existing suite still green."

  **Phase C results (done, commit `c0e7e02`, reviewed — APPROVE WITH CHANGES):** `find_stable_corridor_end`/`find_stable_corridor_end_index` correct and safely bounded. Parametric mode confirmed automatically fixed for free (`extend_into_confluence` reads the shared, already-trimmed `Station` vector, never calls `trim_unstable_junction_tail` itself — `t`/`cross` get nonzero overlap under Parametric for the first time, verified). `junction_stability_margin_stations=6` calibrated empirically on `"h"` alone (flagged as real but non-blocking debt — should become distance/width-scaled, not a raw station count, before Phase F). Retraction loop fires zero times on the reachable corpus but **this is not proof it's unnecessary** — `"trident"`, the one fixture that would actually exercise it, doesn't reach `resolve_junction` (confirmed: 1/204 intervals do cross at margin=6 when tested directly on the raw primitive). **Regression list grew from 2 to 5 shapes** (`y`/`y_symmetric`/`trident` joined `s`/`multi_neck`) by simply testing more fixtures against an unchanged `trace_corridor` — see Phase B.5's widened scope above, which this finding fed directly.

- **Phase D** — `IsoOffsetRing` dispatch branch: port `build_ring_band_sections`/`build_turning_satin_sections` from the superseded plan's design, wire as described in §2.3. New fixtures `disc_15mm`, `petal`, tight-inner-ring disc. **Note added after Phase B.5**: `multi_neck`'s remaining failure (degenerate axis sample at a circular hub's geometric center) is architecturally this phase's territory, not a `trace_corridor` gathering bug — keep it in mind as a second motivating case for `IsoOffsetRing`, beyond the original round/wide-shape rationale.

  **Phase D results (done, reviewed — APPROVE WITH CHANGES, all fixes applied): the actual feature HP-STI-018 asked for is delivered and correctly tested.** `build_ring_band_sections` extracted from `build_annular_sections` as a pure, verified behavior-preserving refactor (`build_annular_sections` becomes a thin wrapper). `build_turning_satin_sections` peels a hole-free region into concentric rings via `geometry::inset_path_set`, with real partial-acceptance (an inner-ring failure keeps outer rings, proven by a dedicated notched-disc test, not just implemented) and a correct termination guarantee. Dispatch wired into `build_satin_columns` for `Ambiguous`/`Unsuitable-with-has_wide_area` regions, with the "can never regress an already-succeeding shape" claim verified directly against `evaluate_satinability`'s real mutually-exclusive-status logic (not just the inline comment's say-so). `turning_satin_ring_width` set to **3mm, not the plan's suggested 4mm** — empirically justified and tested (4mm collapses `disc_15mm`, the roadmap's own named shape, to a single degenerate ring instead of demonstrating real iterative peeling), still flagged as needing recalibration against regenerated golden SVGs (§9 item 5), not final. `disc_15mm`/`petal` confirmed to genuinely classify `Ambiguous` (asserted against `analyze_region`, not assumed).

  **One real finding from this phase's review, now corrected rather than left to mislead someone later:** an initial test asserted the `multi_neck` connection speculated in this bullet's note above was "confirmed" — independent reproduction (`openstitch-cli sgsd-debug --shape multi_neck`) showed this does NOT hold for the real fixture: `multi_neck`'s full skeleton has 0 junctions/1 arc, so `satin_planning`'s decomposition never cuts it into isolable lobes at all, and its real coverage (40.69%, FAIL) is identical before and after this phase. The surviving test proves the ring-peeling mechanism itself works correctly on an isolated circular mass built by hand — it does **not** prove `multi_neck` itself is fixed. `multi_neck` remains an open gap, correctly still filed as this phase's architectural territory (degenerate axis at a hub) for whoever picks it up next, but not something to claim as resolved.
- **Phase E — ABANDONED (not merged), superseded by §10 below.** Original scope: `describe_junctions` new API; migrate `satin_plan.cpp:249-253`; re-run `tests/unit/satin_planning` in full, including `test_region_split.cpp`'s JunctionSeparator-reuse-family test.

  **Why abandoned.** Implemented (coder subagent rate-limited mid-task; verified from scratch, not from a self-report), then found to regress real stitch coverage on the one real-image-based fixture in the whole corpus: `tests/unit/autodigitize/test_autodigitize.cpp`'s "réseau en T" test (a genuine segmented raster image, not a synthetic shape) went from 20/20 assertions passing at the Phase D commit to a leftover uncovered area of **2.03mm²** against a <0.5mm² guarantee — confirmed by isolating against the Phase D commit directly (stash/rebuild/retest), not inferred. Root cause: `describe_junctions`'s separator points are not bit-identical to the old Legacy-harvest's (different stable-station criterion → `build_separator` anchors on a different reflex vertex) — already documented honestly in this plan as an expected property, but its consequence was wrongly assumed benign ("can only add candidates, never regress," per the abandoned code's own inline comment). A same-junction, differently-positioned separator can steer `generate_cut_candidates` toward a *worse* cut on specific real geometry, which is exactly what happened. Unlike the `y_symmetric` margin finding (a test assertion catching up to a legitimately better outcome), this is a genuine increase in real uncovered fabric — the exact failure class this codebase's entire satin history has fought hardest against. Working tree reverted to the Phase D commit (`61a3d23`); nothing from the Phase E attempt was merged.

  **What this revealed, beyond the one bug**: the human's own read of the situation (independent of this specific regression, informed by the accumulated experience of this entire chantier plus `satin_planning`'s own torture-corpus history — `star5`/`comb`/`E`/`multi_neck`/`two_holes` never reaching `Complete`, `trident` still not reaching `resolve_junction`, performance costs necessitating oracle-evaluation budgets) is that the whole region-*decomposition* strategy (cut a branched/complex region into separate sub-regions, each simple enough for a single satin column) produces poor results as a class, not just this one bug. Decision: abandon `libs/satin_planning`'s decomposition layer as the strategy for handling complexity, in favor of handling it *within* a single column — see §10.
- **Phase B.5b (new, added after Phase B.5 — required before Phase F, not before Phase D/E)** — migrate `extend_tip`'s per-step width probe off `cross_section` onto a direction-aware primitive, per the original §2.2 intent ("its per-step width probe is swapped... so open-tip extension also becomes corner-robust") which Phase B/C did not actually implement. Root cause of `y`/`y_symmetric`'s remaining failure, confirmed precisely: with `extend_open_ends=false` (isolating junction resolution from tip extension) both shapes build correctly end-to-end — the failure is 100% inside `extend_tip`, not `trace_corridor`/`find_stable_corridor_end`. A plain half-plane split (Phase B.5's fix, reused as-is) regresses `rectangle`/`notch`/`t` here because `extend_tip`'s marching probe sits very close to the open end-cap by construction, so "nearest point in a half-plane" picks the cap wall instead of the true side rail — needs a narrower, angular-cone-restricted query instead (tried and reverted once already; this is real, scoped debt, not unexplored). `"trident"`'s remaining wedge-tip branch is a **different, non-bug** case (the two sides genuinely converge to the same contour point near a sharp point — `foot_multiplicity==1` pervasively, confirmed) and does not need this fix; only its lateral branch benefited from Phase B.5, which is already landed.
- **Phase F (cutover)** — Gated on Phase B.5 **and B.5b** (above) being complete, or explicitly, consciously waived by a human for whichever part remains (e.g. accepting `multi_neck`/Phase D's own territory is a separate, legitimate gate, not blocking on B.5b). Remove the Phase B dev-only flag and the old `cross_section`/`trim_unstable_junction_tail`/retraction-loop code paths entirely. Deliberately rewrite (not silently leave failing) every test in `test_columns.cpp`/`test_coverage_regression.cpp`/`test_skeleton_equivalence.cpp`/`test_satin_plan.cpp`/`test_torture_corpus.cpp` whose assertions pin old exact geometry (JunctionCore mm², StableBranchEnd exact points) — replace with bounded/structural invariants per §3's table. Regenerate and commit golden SVGs for the full shape gallery per the `openstitch-cli-debug` skill. Update `docs/source/satin.md` with a new dated section (following the file's existing root-cause-writeup convention) and flip `docs/roadmap-parite-hatch.md`'s HP-STI-018 status.
- **Parametric mode**: no dedicated phase; verify at the end of Phase C/F via its existing test suite that it benefits from the corner fix with zero code change of its own (§2.6).

## 5. Test plan

- `tests/unit/auto_satin/test_medial_field.cpp` (new): analytic-shape correctness of `nearest_boundary_feet` (circle, rectangle, L-shape), multiplicity-signal correctness near a synthetic Y branch point.
- `tests/unit/auto_satin/test_corridor.cpp` (new): per-fixture (full existing `shapes.cpp` corpus) no-crossing, no-overshoot-at-corner, `StableCorridorEnd`-not-in-neighbor-corridor checks; determinism (two-run byte-identical).
- `tests/unit/auto_satin/test_columns.cpp`: rewritten per §3/§4-F — structural invariants replace pinned exact values; `capsule`/`rectangle` end-to-end length regressions kept as-is.
- New fixture `e_trunk_isolated` (promoted from the Aug 30 investigation, `satin.md:5249-5270`) in `shapes.cpp`: asserts corner refusal is gone.
- New fixtures `disc_15mm`, `petal`, tight-inner-ring disc for the `IsoOffsetRing` branch.
- `tests/unit/auto_satin/test_pipeline.cpp`, `test_coverage_regression.cpp`, `test_skeleton_equivalence.cpp`: audited for assumptions tied to `Station`'s old internals; expected to need only pinned-value updates, since `Station`'s external field contract (`axis_point`, `rail_a_point`/`rail_b_point` naming kept, `width_um`, `tangent`) is preserved.
- `tests/unit/satin_planning/test_region_split.cpp`: JunctionSeparator-reuse-family test migrated to `describe_junctions`; same assertions (separator candidates tested first, prioritize beam budget) expected to hold since separator-finding logic itself is unchanged.
- `tests/unit/satin_planning/test_satin_plan.cpp`, `test_torture_corpus.cpp`: full corpus coverage-ratio table re-measured and re-baselined explicitly (not silently accepted); `E`/`multi_neck`/`two_holes` re-run with the specific expectation (to verify, not assume) that `E` now succeeds via the corner fix alone, independent of any future contour-correspondence work.
- `tests/unit/autodigitize`: full suite re-run (real-image T-junction regression fixture referenced in `satin.md:533` lives here).
- Golden SVGs under `tests/golden/`: regenerated for the full shape gallery, visual review mandatory before Phase F is declared done.

## 6. Validation commands

```powershell
cmake --build --preset msvc-debug --target openstitch_auto_satin test_auto_satin
ctest --preset msvc-debug -R auto_satin
cmake --build --preset msvc-debug --target openstitch_satin_planning test_satin_planning
ctest --preset msvc-debug -R satin_planning
cmake --build --preset msvc-debug --target openstitch_autodigitize test_autodigitize
ctest --preset msvc-debug -R autodigitize
cmake --build --preset msvc-debug --target openstitch_stitch_generation test_stitch   # Parametric regression
git ls-files '*.cpp' '*.hpp' | xargs clang-format --dry-run --Werror
```
Plus `linux-core` preset sanity build each phase (all new files stay in `libs/auto_satin`/`libs/satin_planning`, already Qt-free). Re-run `docs/performance-audit.md`'s benchmark before Phase F cutover (Tier 2 perf guardrail).

## 7. Explicitly out of scope

- `libs/satin_planning`'s decomposition/oracle/beam-search machinery (phases 1-9 of SGSD) — sound, untouched, only its junction-info dependency is cleaned up (§2.4).
- `RailConstructionMethod::ContourCorrespondence` (DTW-style contour alignment for `comb`/`star5`/residual-junction cases) — remains reserved and unimplemented; the `E` investigation suggests (to be confirmed empirically in Phase B) that at least the `E` case does **not** need it, since its root cause was the corner-measurement defect, not a junction-correspondence problem.
- Parametric mode's own junction strategy (`extend_into_confluence`/`junction_overlap_target`) — untouched, benefits from the shared station-layer fix for free (§2.6).
- Synthesis of a fill object for `JunctionCore::requires_fill` — still diagnostic-only, unchanged from today.
- Manual/interactive turning-satin editing in `apps/desktop` — out of scope.
- `docs/source/satin.md`'s ~50 historical subsections documenting the old pile — kept as historical record per the file's own stated convention ("root-cause writeups for every non-trivial bug fixed"); a new top-level section marks the architecture superseded rather than deleting the history (subject to the open question in §8.5).

## 8. Decisions from human go/no-go gate (resolved)

1. **Rollback at cutover: net deletion.** Phase F deletes `cross_section`/`trim_unstable_junction_tail`/the retraction loop outright, no hidden escape hatch. Git history is the only rollback path. Consistent with the clean-slate mandate.
2. **`SatinColumnsResult::junction_separators`: removed, full migration.** Every consumer — `debug_export.cpp`'s `columns_to_svg`, any `apps/desktop` diagnostic surface, `satin_plan.cpp:249-253` — migrates to `describe_junctions`/`JunctionDescription` in the same chantier. Phase E's scope grows accordingly: it must now also update `debug_export.cpp` and audit `apps/desktop` for any reference to the removed field (grep for `junction_separators` across `apps/desktop` before Phase E is declared done — not audited line-by-line by this plan, must be swept exhaustively during Phase E, not left as a stray compile error for Phase F to discover).
3. **`docs/source/satin.md` editorial policy: purge.** At Phase F, physically remove the subsections documenting the superseded architecture (`StableBranchEnd`, `JunctionSeparator`-by-amputation, the bridge-combinatorics history, `trim_unstable_junction_tail`'s width-drift heuristic, `cross_section`'s own section) rather than keeping them as marked-superseded history. Replace with one new dated section documenting the `nearest_boundary_feet`/`trace_corridor`/`find_stable_corridor_end` architecture, following the file's existing root-cause-writeup style. Cross-check `docs/source/moteur-de-points.md` and `docs/source/module-reference.md` for any reference to removed symbols while purging.

## 9. Still open — needs Phase B/C empirical results, not a decision now

4. **`trace_corridor`'s defensive retraction fallback (§2.2).** Keep a small bounded retraction as a safety net for extreme acute confluences, or rely purely on the multiplicity criterion? Decide after Phase C's results on the full corpus.
5. **HP-STI-018's unresolved parameters** (`turning_satin_ring_width`=4mm, `has_wide_area` trigger scope). Decide after eyeballing regenerated golden SVGs in Phase D, per the earlier design's own recommendation.

---

## 10. Pivot: abandon region decomposition (SGSD), handle complexity within one column (post Phase E)

### 10.0 Grounding: why Phase E's failure is evidence against the strategy, not just the implementation

Phase E's regression (§4 above) was not a bug in `describe_junctions`'s separator math — it was `build_separator` anchoring on a *different, also-valid* reflex vertex than the old harvest, which steered `generate_cut_candidates` toward a worse cut on one real fixture. The lesson generalizes: SGSD's whole mechanism (`libs/satin_planning`'s `branch_pairing`/`region_split`/`concavity_cuts`/`beam_search`/`merge_pass`/`region_oracle`/`overlap`/`region_routing`, orchestrated by `create_satin_plan` in `satin_plan.cpp`) decides a *topological cut* from upstream signals (reflex vertices, junction separators, continuity heuristics) and only *afterwards* discovers, via `satin_coverage`, whether that cut was any good — and when it wasn't, it recurses and tries another cut. Every one of this chantier's own torture-corpus findings (`star5`/`comb`/`E`/`two_holes` never reaching `Complete`; `trident` never reaching `resolve_junction`; oracle-evaluation budgets needed because a single high-degree junction can cost seconds-to-minutes per candidate) is a symptom of the same thing: cutting a region into pieces *before* knowing if the pieces will satin well is a bet that frequently loses, and recovering from a bad bet is expensive and fragile. This is the human's own documented read of the situation (§4, Phase E writeup) — confirmed independently here by re-reading the evidence, not re-derived from scratch.

The replacement strategy does not try to guess a good topological cut at all. It builds **one column across the whole region** (reusing exactly what Phases A0–D already built: `trace_corridor`, `find_stable_corridor_end`, `resolve_junction`/`JunctionCore`, `build_turning_satin_sections`), and where the column would otherwise be geometrically abusive (a throw across a hub far wider than a sane single stitch), it inserts points **locally, at the moment of generating that one stitch** — never deciding to cut the region, never re-running the whole pipeline on a sub-piece, never needing a coverage-driven recursion loop.

### 10.1 Decision

1. **Delete** `libs/satin_planning`'s decomposition machinery outright: `branch_pairing`, `region_split`, `concavity_cuts`, `beam_search`, `merge_pass`, `region_oracle`, `overlap`, `region_routing`, `decomposition_cost`, `topology`, `region_satinability`, and `satin_plan.{hpp,cpp}`'s `create_satin_plan`/`SatinPlan`/`SatinPlanConfig`. Confirmed safe to delete as a block: `topology.hpp`/`region_satinability.hpp` have **zero includers outside `libs/satin_planning` itself** (grepped — only `topology.cpp`, `region_satinability.cpp`, and `decomposition_cost.hpp` touch them), so nothing external depends on SGSD's internals.
2. **Keep** `satin_sections.hpp`'s adapter shape (`satin_params_from_column`, `BuiltSatinSection`, `SatinBuildReport`) — it already has zero conceptual dependency on decomposition (its own header comment is explicit: it was relocated out of `autodigitize` specifically so generic satin workflows don't need SGSD or image classification) — and **rewrite** `build_satin_sections`'s body to call `auto_satin::build_satin_columns` directly (one call, no recursion), convert each resulting column/branch via the existing `satin_params_from_column`, measure via `satin_coverage::analyze_satin_coverage` (unconditionally kept — it has zero dependency on `satin_planning`/`auto_satin`, confirmed via its `CMakeLists.txt`: links only `core`/`geometry`/`stitch_generation`), and report the result honestly through the *same* `SatinBuildReport` shape callers already consume (`sections`, `unresolved_residual`, `aggregate_coverage`, `status`, `warnings`) — so `libs/autodigitize` and `apps/desktop`'s four call sites (`main_window.cpp:3641`, `:3931`, `:4105`, `:4323`) need **zero signature changes**, only the implementation underneath changes. `libs/satin_planning` as a CMake target survives, shrunk to this adapter plus `satin_coverage`'s existing role — whether to keep the name `satin_planning` for this much smaller job, or fold it into `auto_satin`/rename it, is an open question (§10.9).
3. **Raise the ceiling** on what a *single* column (per branch, per Phase A0–D's existing per-edge-then-resolve-junction machinery) is allowed to build through, by relaxing two specific, concrete thresholds in `auto_satin` that today force a refusal or reroute exactly where SGSD used to be reached for: the `has_wide_area`-driven `Unsuitable` early return in `evaluate_satinability` (`satinability.cpp:125-130`), and `cross_section`'s `TooWide` failure (`satin_column.cpp:91,130-132`), both currently keyed off the same `max_satin_width` (12mm default). `max_junctions` (`satin_column.hpp:57`, default 2 — SGSD's other historical reason to exist, "trop de jonctions pour une décomposition fiable," `satin_column.cpp:3293-3296`) is raised/reworked into a linear budget guard instead of a hard topology cap.
4. **Generalize `split_stitch`** (`libs/stitch_generation/src/satin.cpp`, already-existing, already-tested, `docs/source/satin.md:1697` "Lot 3") — not `short_stitch` — into the actual "insert intermediate points when a throw is too wide" mechanism, and make its length bound **always enforced** (structural), not opt-in cosmetic. A new, distinct lateral perpendicular-offset behavior is added for genuinely wide throws, which is the part that earns the "mix between tatami and satin" description; this part does not exist today and is new work, precisely scoped below (§10.3.3).
5. **`libs/stitch_generation`'s `directional_fill.cpp` is explicitly NOT reused** for this (§10.7) — it is a structurally different generator (guide-driven Jobard & Lefer streamlines over a free scalar field, no rails/rungs/caps/locks/compensation concept at all) and bridging to it would mean synthesizing guides automatically from the medial axis, a materially larger, separate piece of work this plan does not take on.

### 10.2 Why `split_stitch`, not `short_stitch` — grounded in what each mechanism actually measures

Re-reading `docs/source/moteur-de-points.md` §6.3 steps 2 and 6 precisely, because the two existing "a satin rung is locally aggressive, do something" mechanisms measure **different axes** and only one of them is the mirror image of this problem:

- **`short_stitch`** (§6.3 step 2, `docs/source/satin.md:1695-1708`) measures **advance along each rail between two consecutive threads** (`advA`, `advB` — i.e. motion *along the column's length*, projected per rail) and acts when the ratio is too skewed (a tight turn: the inner rail barely advances while the outer rail advances a lot). It *removes* (`RemoveAndRedistribute`) or *insets toward the axis* (`SingleInset`/`MultiLevelInset`) points — it is about too much density crowding on the inside of a turn. This has nothing to do with the across-column distance; it is already resolved along-axis spacing that's become too tight.
- **`split_stitch`** (§6.3 step 6, `satin.cpp:660-709`) measures `len = dist(pa, pb)` — the **across-rail throw itself**, i.e. exactly "two points [the two ends of one stitch] too far apart" in the literal, most direct sense — and inserts collinear intermediate penetrations when `len > max_stitch_length` (7mm default). This is already, today, the real mirror image of the human's ask, not a mechanism that needs inventing from nothing.

What `split_stitch` does **not** yet do, confirmed by reading the exact arithmetic (`satin.cpp:690-704`): every inserted point is `lerpP(pa, pb, frac)` — a point **strictly on the straight line between the two rails**. `Staggered`/`DeterministicJitter` only perturb `frac` (*where along that one straight line* the intermediate penetration lands, to avoid a repeating visible sub-line across many consecutive wide throws) — they never move a point *off* that line. So today's split_stitch, even at its most elaborate setting, produces a bundle of straight, purely collinear stitches re-covering the same chord — mechanically safe (every single needle penetration is short) but visually and texturally still "one long bar of parallel straight throws," not interlocking fabric coverage. That gap — no lateral/perpendicular component at all — is the real, specific, and previously-unaddressed piece of "mix between tatami and satin": tatami's row interlocking (Phase 2, `docs/source/tatami.md`) gets its fabric-like coverage specifically from offsetting rows/stitches *perpendicular* to the row direction, and that perpendicular component is exactly what's missing from `split_stitch` today.

**Conclusion, stated as the task asked**: this is a generalization of an *existing* mechanism (`split_stitch`), not an architecturally new category of thing — but it requires one genuinely new piece of geometry (perpendicular/lateral offset of intermediate points, bounded, bidirectional/zigzag, phase-alternated between neighboring wide throws) that does not exist anywhere in the codebase today and must be built, scoped precisely in §10.3.3.

### 10.3 Concrete mechanism

Three coordinated changes, each at the layer where the corresponding refusal/limitation actually lives today — traced precisely against the real dispatch code, not assumed:

#### 10.3.1 `auto_satin` eligibility (satinability.cpp) — stop refusing a region that has a usable axis just because it's locally wide somewhere

Read precisely: `evaluate_satinability` (`satinability.cpp:84-139`) checks, in this exact order: invalid/zero-area → holes → too-narrow → **ambiguous direction** (`is_elongated` false → `Ambiguous`, routes to ring-peeling, **unaffected by this pivot**) → **branched** (`junction_count>0 || endpoint_count>2` → `RequiresDecomposition`, **also unaffected here** — a branched region already bypasses the width check entirely today, confirmed by code order) → **`has_wide_area`** (→ `Unsuitable`, line 125-130) → width-variation warning → `Suitable`.

Only the `has_wide_area` branch is wrong for the shapes this pivot cares about: a region that is **elongated** (already passed the ambiguous check — `is_elongated` true, so a real 1D axis exists) and **not branched** (already passed the branch check) but has one locally wide spot (`multi_neck`'s hub: Phase D's own diagnostic — `sgsd-debug --shape multi_neck` — found "0 junctions/1 arc," i.e. a single continuous skeleton edge end-to-end, hub included) is refused outright today, and the only fallback attempted (`build_turning_satin_sections`, dispatched at `satin_column.cpp:3069-3086` precisely because `r.status==Unsuitable && r.report.has_wide_area`) is Phase D's own documented non-fix for this exact shape ("the surviving test proves the ring-peeling mechanism itself works correctly... it does **not** prove `multi_neck` itself is fixed" — Phase D results, §4 above). Ring-peeling is the right recipe for a disc/petal with *no* usable axis; `multi_neck` *has* a usable axis (one arc), it's just locally wide along it — a structurally different case that the current dispatcher has no third option for.

**Change**: demote the `has_wide_area` branch from `Unsuitable` to `SuitableWithWarnings` when `is_elongated` is true (it is, by construction, at this point in the function — we already returned for the `Ambiguous` case otherwise). Do **not** touch the `Ambiguous` path or its ring-peeling dispatch. This single change means: `build_satin_columns`'s `switch (r.status)` (`satin_column.cpp:3285-3353`) now falls into the existing `Suitable`/`SuitableWithWarnings` case for `multi_neck`-like shapes — "axe principal = arête la plus longue," `try_edge(*longest)` — i.e., exactly the ordinary single-column Corridor path, which is where §10.3.2's width handling actually does the work. `has_wide_area` remains reported (unchanged field, still visible in `SatinabilityReport` for UI/diagnostics) — it stops being a blocking verdict, same spirit as `width_variation > 0.8` already being `SuitableWithWarnings` rather than `Unsuitable` two lines below it.

Ring-peeling (`build_turning_satin_sections`) is tried **first**, unconditionally unchanged, for its existing trigger (`Ambiguous`, or `Unsuitable && has_wide_area` — which after this change can now only be reached by a region that's *both* elongated-disqualified in some other, unrelated way *and* wide, a narrower residual case than before, or literally never in practice since `has_wide_area` alone no longer produces `Unsuitable` when elongated). This keeps Phase D's recipe exactly where it already works (discs/petals with no real axis) and lets the new per-station wide-throw handling be the recipe for "has an axis, locally wide" — the two recipes stay non-overlapping by construction, not by a new priority rule that could silently regress one or the other.

#### 10.3.2 `auto_satin` corridor measurement (`cross_section`/`corridor.cpp`) — stop treating "locally wide" as a measurement failure

Read precisely: `cross_section` (`satin_column.cpp:90-137`) takes a single `max_width` parameter (always `params.analysis.thresholds.max_satin_width.value`, confirmed at `satin_column.cpp:421-422,949,2147,2251`) and returns `CrossSectionFailure::TooWide` (line 130-131) the instant one station's measured interval exceeds it. Too many `TooWide`/other-failure stations in one branch trip `max_station_gap_ratio`/`min_axis_coverage_ratio` (`satin_column.hpp:110-117`) and the **whole branch is refused**, not partially built — this is the actual mechanical block on a hub-within-a-branch case (e.g. a fat lobe off a Y, or a wide bridge between two junctions in an "H"), independent of §10.3.1's classification-level fix.

**Change**: split the one `max_width` into two params in `SatinColumnsParameters` (`satin_column.hpp`):

```cpp
// Renamed role, same field: now purely a SOFT/informational threshold (feeds
// SatinabilityReport::has_wide_area, and the new per-station `wide` flag
// below) -- no longer a hard refusal boundary on its own.
// analysis.thresholds.max_satin_width stays as-is (12mm default) for this role.

// NEW. The actual TooWide/corridor-sanity refusal boundary. Must stay a real
// ceiling -- it is what still catches a genuinely wrong measurement (a ray/
// foot query that landed on an unrelated, distant contour feature), not just
// "this hub is wide." Proposed default 4x max_satin_width (48mm) -- a
// starting point for Phase-by-phase calibration against the corpus, not a
// value to treat as final (same epistemic discipline as turning_satin_ring_width
// and junction_stability_margin_stations elsewhere in this plan).
Micrometers corridor_max_width_hard{48'000};
```

`cross_section` (and `corridor.cpp`'s equivalent `nearest_boundary_feet`-based width check) now passes `corridor_max_width_hard` as the `max_width` bound that actually fails the station; a station between `max_satin_width` (soft) and `corridor_max_width_hard` is **accepted**, and both `Station` (the local struct in `satin_column.cpp`) and `CorridorStation` (`corridor.hpp`) gain:

```cpp
bool wide{false}; // width_um > params.analysis.thresholds.max_satin_width.value
```

This flag is **diagnostic only** inside `auto_satin` — it does not need to reach `document::SatinParams`/`SatinRung` (no `.osp` schema change, no new persisted field; keeps this a small, low-risk surface) — but should be threaded into `SatinColumnsResult::warnings` (one aggregated message per column, "N stations wide: max X mm") and into `debug_export.cpp`'s SVG (color-code wide stations, same diagnostic culture as the rest of this file) so Phase-by-phase side-by-side review (§10.6) can actually see what changed. `fill_satin_columns` (§10.3.3) recomputes "is this throw wide" independently and cheaply from the already-known `dist(a,b)` — the two checks are intentionally redundant rather than threading a new field through `document::SatinParams`.

`max_adjacent_width_jump_ratio` (0.75, `satin_column.hpp:120`) is **unchanged** — it still catches a genuinely implausible *sudden* jump between adjacent stations (still routed into `adjacent_width_jump_indices`/`select_structural_indices`'s existing "treat as a structural corner" handling, not a refusal) — a gradual hub widening over many stations does not trip it, which is correct: that's exactly the case this pivot wants to let through.

`max_junctions` (`satin_column.hpp:57`, default 2): the refusal at `satin_column.cpp:3293-3296` ("trop de jonctions pour une décomposition fiable") is SGSD's *other* historical reason to exist — a region with 3+ junctions was never attempted by `auto_satin` alone, only by SGSD cutting it first. Without SGSD, this cap must move or the pivot loses exactly the branched shapes (comb-like, multi-way stars) it's supposed to cover. **Change**: replace the topology cap with a linear, deterministic budget — `max_corridor_build_budget` (new field, e.g. a cap on total stations traced across all edges+junctions in one `build_satin_columns` call, default calibrated empirically in Phase against `comb`/`star5`) — so an *ordinary* multi-junction letter/branch shape (comb's 6 junctions) is attempted, while a genuinely pathological case (`star5`'s 5-way single node, already documented as expensive even for SGSD's own oracle-guided search) still gets an honest, bounded refusal rather than hanging. This is a real, open calibration question, not a decision this document makes for the human (§10.9).

#### 10.3.3 `stitch_generation` wide-throw bridging (`fill_satin_columns`, `satin.cpp` step 6) — the actual new geometry

This is the piece that earns "mix between tatami and satin." Precisely scoped against the existing code (`satin.cpp:660-709`, reproduced/analyzed in §10.2):

**New `SatinConfig` fields** (`libs/stitch_generation/include/openstitch/stitch_generation/satin.hpp`):

```cpp
// Structural safety net, ALWAYS enforced from now on -- see the breaking-
// change note below. max_stitch_length itself is unchanged (7mm default,
// still the bound for the existing colinear subdivision).

// NEW. Above this width, a throw additionally gets a perpendicular nudge on
// its interior subdivision points (not just more colinear points on the same
// line) -- the actual "mix with tatami" behavior. Proposed default 10mm
// (intentionally above max_stitch_length's 7mm default, so an ordinary long-
// but-not-hub throw keeps today's pure colinear split unchanged; this is a
// starting point for Phase calibration, not final).
Micrometers wide_throw_width{10'000};

// NEW. Lateral offset amplitude, as a fraction of LOCAL HALF-WIDTH at that
// thread -- same convention and magnitude as short_stitch_inset (0.35
// default) for consistency with the existing param family.
double wide_throw_zigzag_amplitude{0.35};
```

**Breaking change, flagged explicitly**: today, `split_stitch == SplitStitchMode::Disabled` means *no bound is enforced at all* on `len` (satin.cpp:690's `config.split_stitch != SplitStitchMode::Disabled && len > maxLen` gate) — a column that reaches `fill_satin_columns` with an enormous throw (which, after §10.3.1/§10.3.2, is now a reachable case that used to be an upstream refusal) would today silently emit one arbitrarily long stitch. This pivot requires reinterpreting `SplitStitchMode::Disabled` as "simple colinear subdivision, no cosmetic staggering" rather than "no bound" — i.e. the `len > maxLen` bound becomes unconditional, and the three-way enum value only selects the cosmetic *style* of the colinear subdivision. This is a real, user-visible behavior change for anyone who had deliberately left `split_stitch` at its default `Disabled` while also deliberately using very wide manual columns — must be called out in release notes / `docs/source/satin.md`'s dated writeup, not landed silently. Flagged as open-question-adjacent in §10.9 (is this acceptable, or does it need a separate opt-out).

**Geometry, inserted into the existing step-6 loop** (`satin.cpp:665-709`), reusing state the function already computes rather than adding new passes:

1. `w = dist(pa, pb)` is already computed (line 675/689 as `len`).
2. The existing colinear-subdivision block (lines 690-706) runs unconditionally now (bound to `max_stitch_length`, per the breaking change above) — unchanged arithmetic otherwise.
3. **New**: if `w > config.wide_throw_width`, for each interior subdivision point `s` (the same `1..nsplit` loop already iterating), additionally displace it perpendicular to the `pa→pb` axis, along the **local tangent direction** — not literally sideways out of the rail-to-rail quad (which would risk exiting the region or crossing a neighboring rail, the exact self-crossing bug class flagged in §3's table) but *along the column's direction of travel*, which both `short_stitch`'s inset and `push`'s extend/retract already treat as the safe axis to move along. The tangent at thread `i` is already available for free: `mids`/`cumMid` (§6.3 step 4, computed *before* this loop runs, confirmed by the doc's own ordering) give `tangent_i ≈ unit(mids[i+1] - mids[i-1])` (central difference, same convention as every other tangent computation in this codebase — `satin_column.cpp`'s axis tangent, `corridor.cpp`'s `CorridorStation::tangent`). Displacement:

```
offset = min(wide_throw_zigzag_amplitude * (w * 0.5),
             0.4 * min(advance_to_prev, advance_to_next))
point_s = lerpP(pa, pb, frac) + tangent_i * offset * sign(s)
```

where `advance_to_prev`/`advance_to_next` are the existing `cumMid`-derived arc-length gaps to the neighboring threads (already computed, step 4) and `sign(s)` alternates per interior point within one throw (a genuine zigzag: point 1 forward, point 2 back, point 3 forward...) **and** the overall parity alternates per thread too, reusing the exact `emitted % 2` trick `Staggered` already uses (line 696) — so neighboring wide throws' zigzags are out of phase with each other, which is what actually produces fabric-like interlocking across a hub rather than a repeating in-phase ladder pattern. The `0.4 * min(advance_to_prev, advance_to_next)` term is the load-bearing safety bound, directly modeled on `push`'s existing `-(n-1)` retraction bound (§6.3 step 3) and `short_stitch`'s inset-toward-midpoint bound: it makes it geometrically impossible for a nudged point to reach as far as the *next* thread's own rail-to-rail segment, so two consecutive wide throws' zigzags cannot cross each other by construction, not by luck.

4. Rail attachment points themselves (`pa`, `pb` — the actual stitch that starts/ends on rail A/rail B) are **never** displaced — only the interior split points move. This preserves every existing guarantee about rail integrity, routing (`route_columns` reads `column_endpoints` off `pa`/`pb`-derived positions, untouched), and `satin_coverage`'s structural-geometry measurement (§6.3's doc note that `satin_coverage` deliberately uses *structural* rails/rungs, never the finished stitch points — unaffected, since it never looks at this layer at all).

This is deliberately the smallest possible addition that produces the texture being asked for: it reuses `mids`/`cumMid` (already computed), reuses the `emitted`-parity phase trick (already used by `Staggered`), reuses the existing bounded-displacement pattern (already used by `push`/`short_stitch`), and touches nothing upstream of the one loop that already does "turn a Thread into needle penetrations."

### 10.4 Interaction with Phases A0–D (all kept, confirmed against the actual code)

- **`trace_corridor`/`CorridorStation.width_um`** (Phase A/B, `corridor.hpp`): directly feeds the new `wide` flag and the relaxed `TooWide` ceiling in §10.3.2 — no new primitive needed, this is exactly the signal Phase A/B already produce per station.
- **Junction resolution (`resolve_junction`/`JunctionCore`/`JunctionSeparatorInfo`)** (Phase C, kept verbatim): remains the meeting point *between* branches. Intermediate/wide-throw points (§10.3.3) only ever apply to stations strictly inside one branch's stable span (between its `StableCorridorEnd` and the next branch's), **never** inside the junction core region itself. A wide *confluence* (several branches meeting at a fat hub — e.g. a thick "X") is a different, not-yet-covered case: `JunctionCore::requires_fill` already flags this today (area over `junction_core_significant_area_um2`) as "needs a separate fill object, not synthesized here" — unchanged, still out of scope (§10.8). This pivot does not extend wide-throw handling into the junction core; stated honestly as a limitation, not hidden.
- **Ring-peeling (Phase D, `IsoOffsetRing`/`build_turning_satin_sections`)**: untouched for its real territory (no usable axis at all — discs, petals). §10.3.1's reclassification is designed to be non-overlapping with it (elongated-with-an-axis vs. ambiguous-with-no-axis), confirmed by re-reading the exact `evaluate_satinability` branch order — the one narrow overlap is `multi_neck`-shaped cases, where ring-peeling is tried first (unchanged priority) and the new path only engages if ring-peeling's own fallback already failed, which per Phase D's own finding, it does for `multi_neck`.
- **Parametric mode** (§2.6): `compute_column_stations`/`SatinControlPair`/`SatinAngleGuide` share the same station layer, so §10.3.1/§10.3.2's relaxed thresholds benefit Parametric mode's corridor tracing for free, same as Phase C's corner fix did. §10.3.3's wide-throw bridging is a `stitch_generation::fill_satin_columns`-level change and applies identically regardless of which `auto_satin` mode produced the rails (both modes flatten to the same `Thread{a,b}` representation before this loop runs, per `moteur-de-points.md` §6.1's documented shared entry point) — no Parametric-specific work needed here, but Phase-level testing (§10.6) should still include a Parametric fixture explicitly, same discipline as §2.6's correction demanded for the corner fix.

### 10.5 Bug classes this must not reintroduce

| Bug class (from this plan's own §3) | Could this pivot reintroduce it? | How this design avoids it |
|---|---|---|
| Self-crossing rails | Not directly — this pivot never moves a rail point, only interior split points of an already-finished top-layer throw. The new risk is a nudged split point crossing into a *neighboring thread's* territory. | §10.3.3's bound (`0.4 * min(advance_to_prev, advance_to_next)`) makes this geometrically impossible by construction — same pattern as `push`'s existing `-(n-1)` bound. New test: assert no two consecutive wide-throw zigzags' point sets intersect, on a synthetic hub fixture with `wide_throw_width` deliberately small to force many consecutive wide throws. |
| Junction core wrong size/shape | No — §10.3's changes never touch `resolve_junction`/`JunctionCore` construction, only what feeds it (relaxed width ceilings upstream, in a different code path). | Re-run existing `JunctionCore` structural invariants (non-self-intersecting, bounded by `local_radius_um`) unchanged on the existing corpus — if §10.3.1/§10.3.2 somehow changed which stations reach `resolve_junction`, this would be the test that would catch it. |
| Bridge on wrong side of neighbor branch | No — `find_stable_corridor_end`'s multiplicity criterion (Phase C, kept) is entirely upstream of and independent from this pivot's changes. | Unchanged; re-run the existing "réseau en T" real-image fixture (`test_autodigitize.cpp:173`, `leftoverAreaMm2 < 0.5` assertion) as the acceptance gate (§10.6) — this is the fixture Phase E's regression was actually caught on, so it's the right bar here too. |
| Open tip retracting short | No — `extend_tip` untouched by this pivot. | Unchanged; existing `capsule`/`rectangle` length regressions re-run as-is. |
| Corner/bend false refusal (`E`) | No — Phase B/B.5's corner fix is orthogonal to width handling. | Unchanged; `e_trunk_isolated`/full `E` fixture re-run as-is. |
| **New risk**: a wide station that is accepted (§10.3.2) but whose `IntervalOutsideRegion`/foot-selection is actually wrong (e.g. the "direction-blind foot gathering" failure mode from Phase B/B.5, which is about *selection*, not width) gets silently treated as a legitimate wide hub instead of a measurement error. | Yes, if `corridor_max_width_hard` is set too generously relative to the region's real scale, a genuinely broken measurement (ray/foot landed on a distant, unrelated contour feature) could be misread as "wide but valid" instead of refused. | `corridor_max_width_hard` is a **ceiling relative to the region's own scale** worth reconsidering at calibration time (§10.9 open question) — and `IntervalOutsideRegion`'s existing midpoint-must-be-interior check (`satin_column.cpp:133-135`) is unchanged and still catches the specific failure mode Phase B/B.5 found (a foot landing on the wrong lobe produces a midpoint outside the region, not just "wide") — this guard already exists and already covers the actual historical failure signature, independent of the width ceiling. New test: a synthetic "wide hub with a nearby unrelated lobe" fixture, assert `IntervalOutsideRegion` still fires correctly (not absorbed as a false "wide" success) at the new, larger ceiling. |
| **New risk**: a wide throw's zigzag point exits the region polygon (e.g. near a concave boundary where the "safe" tangent-direction offset isn't actually safe because the column is curving sharply at that exact point). | Yes, in principle, at a sharply curving wide hub. | A cheap `in_region`-style sanity check is **not** available at the `stitch_generation` layer (no region polygon there, by design — `fill_satin_columns` only sees rails/rungs, never the source region) — so this cannot be checked geometrically at generation time. Mitigation: rely on the existing bound being conservative (0.4× the *shorter* of the two neighbor gaps is already tight relative to column curvature in practice, since `rung_angle_threshold_deg`/`max_adjacent_width_jump_ratio` upstream already force extra structural rungs at sharp turns, meaning `advance_to_prev`/`advance_to_next` are already small exactly where curvature is high) — but flagged honestly as a **residual risk, not eliminated**, to be caught empirically in Phase testing (§10.6) via `satin_coverage`'s `outside_regions`/`outside_ratio` (already measures exactly "stitch geometry outside the target region," unchanged) rather than assumed safe. |

### 10.6 Test plan

- `tests/unit/auto_satin/test_satinability.cpp` (existing — grep for it; if absent, add alongside `test_corridor.cpp`): assert `multi_neck`-shaped regions now classify `SuitableWithWarnings` (not `Unsuitable`), `disc_15mm`/`petal`'s `Ambiguous` classification is unchanged (ring-peeling territory untouched).
- `tests/unit/auto_satin/test_columns.cpp`: new cases for `corridor_max_width_hard`/`wide` flag — a station between the soft and hard ceilings is accepted and flagged; a station beyond the hard ceiling still refuses (`TooWide` still exists, just at the new ceiling); `IntervalOutsideRegion` still fires on the synthetic "wide hub near an unrelated lobe" fixture (§10.5's new-risk row).
- `tests/unit/auto_satin/test_columns.cpp` or a new `test_multi_junction_budget.cpp`: `comb`-like (many junctions, bounded) now builds (previously refused at `max_junctions=2`); a deliberately pathological high-degree fixture (star-shaped, 5+ branches at one node) still refuses honestly within the new linear budget, never hangs — two-run byte-identical determinism check per Tier 1.
- `tests/unit/stitch/test_satin.cpp` (or wherever `fill_satin_columns` is tested today): new cases for `wide_throw_width`/`wide_throw_zigzag_amplitude` — a throw just above `wide_throw_width` gets a visible, bounded, alternating-side perpendicular offset on interior points; rail attachment points (`pa`/`pb`) never move; two adjacent wide throws' point sets never intersect (self-crossing guard, §10.5); the breaking change (split_stitch's bound is now unconditional) gets an explicit regression test asserting a very wide throw with `split_stitch == Disabled` is still subdivided (today it would not be).
- `tests/unit/satin_planning/*`: delete the decomposition-specific test files (`test_region_split.cpp`, `test_branch_pairing.cpp`, `test_beam_search.cpp`, `test_merge_pass.cpp`, `test_region_oracle.cpp`, `test_overlap.cpp`, `test_region_routing.cpp`, `test_decomposition_cost.cpp`, `test_topology.cpp`, `test_region_satinability.cpp`, `test_satin_plan.cpp`'s recursive-planning cases, `test_torture_corpus.cpp`'s coverage-table-vs-decomposition framing) — **rewrite**, not silently drop, `test_satin_plan.cpp`'s remaining relevant cases (the adapter itself, `build_satin_sections`'s new direct-call behavior) and `test_torture_corpus.cpp` into a new acceptance table: for each of `star5`/`comb`/`E`/`two_holes`/`multi_neck`, re-measure coverage via `satin_coverage` under the new single-column-with-wide-throws path and record it honestly (some will legitimately be worse than SGSD's best recursive attempt on a given shape — e.g. `two_holes` has holes, which `evaluate_satinability` still refuses outright regardless of this pivot, since the hole-count check is untouched and `IsoOffsetRing`'s single-ring case only covers exactly one hole; this must be stated as a known, accepted scope reduction, not silently dropped from the corpus).
- **Primary acceptance gate, same bar as Phase E's own regression**: `tests/unit/autodigitize/test_autodigitize.cpp`'s "réseau en T" fixture (line 173, real segmented-raster image, `leftoverAreaMm2 < 0.5` assertion) must still pass under the new `build_satin_sections` implementation — this is the exact fixture that caught Phase E's regression, so it is the correct acceptance bar for this pivot too, not a fixture to skip because "it's not about width."
- `tests/unit/autodigitize/test_autodigitize.cpp`'s "anneau fin" fixture (line 250): unaffected by this pivot (single-hole ring case, `build_annular_sections`, dispatched before any of §10.3's changes are reached) — re-run unchanged as a negative control that nothing in this pivot touches that path.
- Golden SVGs (`tests/golden/`): regenerate for the full shape gallery per `openstitch-cli-debug`, with explicit visual review of `multi_neck` (does the hub now look like reasonable fabric coverage, not a bundle of straight bars?) and a deliberately constructed "wide hub next to a thin neck" synthetic shape, mandatory before any cutover.
- Determinism (Tier 1): two-run byte-identical on every new/changed test module, same discipline as the rest of this plan — `wide_throw_zigzag_amplitude`'s phase alternation uses `emitted % 2`, already deterministic by construction (no new RNG beyond the existing `DeterministicJitter`/`split_seed` path, untouched).

### 10.7 Why not bridge to `directional_fill` — stated, not just asserted

`directional_fill.cpp`'s mechanism (`docs/directional-fill.md` §2-3, confirmed by reading the file): a direction field built from **user-placed guide curves** (or, absent guides, the sector's principal axis — a single global direction, not locally width-aware), then Jobard & Lefer evenly-spaced streamline tracing over that field, with its own independent penetration/underlay model. To use this for "bridge a wide satin hub" would require synthesizing guides automatically from the corridor's medial axis/tangent field at generation time — a real, separate algorithmic problem (how do you turn a `CorridorStation` sequence into a `DirectionalFillParams::guides` set that produces sane streamlines specifically *in the hub region only*, without guides elsewhere in the column fighting the rail/rung model?) — not a reuse, a new integration. Rejected for this plan as disproportionate to the problem (a local per-throw fix, §10.3.3, solves the documented failure cases without it) and because mixing two independent generators (satin's rails+rungs+caps+locks+compensation pipeline, and directional_fill's own field+streamline+underlay pipeline) inside one embroidery object has no existing precedent in `document::StitchParams`'s variant model and would be a materially larger data-model change.

### 10.8 Explicitly out of scope

- Synthesizing a fill object for `JunctionCore::requires_fill` (a wide *junction confluence*, as opposed to a wide *span within one branch*) — still diagnostic-only, unchanged from today, unchanged by this pivot.
- `RailConstructionMethod::ContourCorrespondence` — still reserved/unimplemented; this pivot does not revisit whether it's needed (SGSD's departure removes the `comb`/`star5` decomposition path that might have wanted it, but the single-column linear-budget path proposed here doesn't need it either).
- Region topologies with holes beyond the single-hole ring case (`two_holes`) — `evaluate_satinability`'s hole-count refusal is untouched; this pivot does not attempt to generalize ring-peeling or corridor tracing to multi-hole regions.
- Manual/interactive wide-throw editing in `apps/desktop` (e.g. letting a user manually tune `wide_throw_zigzag_amplitude` per-object in the inspector) — ship with the global default only; exposing it as a per-object `document::SatinParams` field is future work, not blocking this pivot (and deliberately keeps the `.osp` schema untouched, §10.3.2/§10.3.3).
- Bridging to `directional_fill` (§10.7).
- Re-litigating Phases A0–D's own open items (`junction_stability_margin_stations` distance-scaling, `turning_satin_ring_width` recalibration) — those remain tracked exactly as §9 already states, independent of this pivot.

### 10.9 Open questions for human go/no-go

1. **`corridor_max_width_hard`'s nature**: a fixed absolute ceiling (proposed default 48mm) as drafted above, or scaled relative to the region's own `max_satin_width`/overall size (e.g. `k × max_satin_width`)? A fixed absolute value is simpler and more predictable but could be wrong by an order of magnitude for a very large or very small embroidery design; a relative value tracks better but reintroduces a second knob's worth of calibration risk. This directly trades off against §10.5's "new risk" row (a too-generous ceiling absorbing a real measurement error as a false "wide success").
2. **`max_corridor_build_budget`'s shape**: a station-count linear budget as proposed, or something closer to SGSD's own `max_oracle_evaluations`-style per-junction cost accounting (more precise but reintroduces some of the complexity this pivot is trying to shed)? Needs empirical calibration against `comb`/`star5` specifically, which this document cannot do without running the actual corpus.
3. **The `split_stitch`/`Disabled` breaking change (§10.3.3)**: is making the length bound unconditional (removing the ability to opt out of it entirely) acceptable, or does some real existing use case deliberately rely on unbounded throws today (e.g. a deliberately very wide manual satin column someone is already using, that this change would now silently start subdividing)? This is a user-facing behavior change this document flags but cannot resolve — needs either an explicit migration note or a genuine opt-out field.
4. **`libs/satin_planning`'s fate as a CMake target**: keep the name for the shrunk adapter (minimal churn, `autodigitize`/`apps/desktop`'s `CMakeLists.txt` already link `openstitch::satin_planning`), or rename/relocate the adapter into `auto_satin` or a new small lib, now that "planning" (recursive decomposition) is gone and the remaining job is just "build one column's worth of `document::SatinParams` + measure coverage"? Pure naming/organization call, no functional stakes — left to the human's preference for repo clarity vs. minimizing diff noise.
5. **Default values for `wide_throw_width` (10mm), `wide_throw_zigzag_amplitude` (0.35), `corridor_max_width_hard` (48mm), `max_corridor_build_budget`**: every one of these is a first-draft proposal justified by convention-matching with existing params (`short_stitch_inset`, `max_stitch_length`, `max_satin_width`), not by having run the corpus — same epistemic status as Phase D's `turning_satin_ring_width` before its own empirical correction (4mm → 3mm). This pivot's implementation phase must calibrate these against `multi_neck`, `comb`, and a deliberately constructed wide-hub synthetic fixture before any of them are treated as settled, exactly as this plan's own established discipline requires (§21/§22 of `docs/source/satin.md`: never a value pinned under the pressure of one fixture).
6. **Acceptance for `two_holes`/multi-hole shapes**: explicitly accepted as a permanent scope reduction relative to what SGSD *attempted* (even if it rarely succeeded well, per the torture-corpus history) — does the human want this stated plainly in `docs/roadmap-parite-hatch.md` as a known gap, or does it warrant its own follow-up chantier?

### Critical Files for Implementation
- libs/auto_satin/src/satinability.cpp
- libs/auto_satin/src/satin_column.cpp
- libs/auto_satin/include/openstitch/auto_satin/satin_column.hpp
- libs/auto_satin/src/corridor.hpp
- libs/stitch_generation/src/satin.cpp
- libs/stitch_generation/include/openstitch/stitch_generation/satin.hpp
- libs/satin_planning/include/openstitch/satin_planning/satin_sections.hpp
- libs/satin_planning/src/satin_plan.cpp (rewrite target; most of libs/satin_planning's other files are deletion targets — branch_pairing, region_split, concavity_cuts, beam_search, merge_pass, region_oracle, overlap, region_routing, decomposition_cost, topology, region_satinability)
- tests/unit/autodigitize/test_autodigitize.cpp (the "réseau en T" fixture at line 173 is the primary acceptance gate)
