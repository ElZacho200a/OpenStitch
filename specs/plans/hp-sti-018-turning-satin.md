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
- **Phase E** — `describe_junctions` new API; migrate `satin_plan.cpp:249-253`; re-run `tests/unit/satin_planning` in full, including `test_region_split.cpp`'s JunctionSeparator-reuse-family test.
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

### Critical Files for Implementation
- libs/auto_satin/src/satin_column.cpp
- libs/auto_satin/include/openstitch/auto_satin/satin_column.hpp
- libs/auto_satin/include/openstitch/auto_satin/skeleton_graph.hpp
- libs/satin_planning/src/satin_plan.cpp
- libs/satin_planning/include/openstitch/satin_planning/topology.hpp
- libs/auto_satin/src/shapes.cpp
- tests/unit/auto_satin/test_columns.cpp
