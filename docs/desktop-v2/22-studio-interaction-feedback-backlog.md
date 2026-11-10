# Studio — Interaction Feedback & Juice (PR #317)

This tracks code present on PR #317 as of the latest branch head. **Checked = implementation observed in source, not CI/QA verified or shipped.** Unchecked = remaining limitation or unvalidated behavior. Do not mark the PR complete before tests and real UI screenshots.
Scope: Qt Quick UI only; no engine, stitch geometry, embroidery format or workflow changes. Respect `Theme.reducedMotion`; never display synthetic progress or success without an actual controller event. Avoid overlap with V02 and UX automation.

## Canvas and objects (21–30)
- [x] 21. Preselection highlight on hover, only if hover target is exposed.
- [x] 22. Short selection confirmation on click.
- [x] 23. Distinguish active object from other selected objects.
- [x] 24. Contextual move/rotate/resize/node cursors.
- [x] 25. Visible locked-object feedback.
- [x] 26. Show rotation pivot during rotation.
- [x] 27. Live width/height readout while resizing.
- [x] 28. Show constrained aspect-ratio state.
- [ ] 29. Distinguish duplication preview from movement.
- [ ] 30. Indicate real snap target (grid/guide/object) when exposed.

## Digitizing stages (31–40)
- [x] 31. Confirm successful image import.
- [x] 32. Segmentation activity indicator.
- [x] 33. Segmentation completion feedback.
- [x] 34. Linked region highlight in canvas and list.
- [ ] 35. Vectorization activity indicator.
- [x] 36. Vector result available feedback.
- [x] 37. Stitch generation activity indicator.
- [x] 38. Stitch result available feedback.
- [x] 39. Visually distinguish stale results from valid ones.
- [x] 40. Subtle preview-ready indication.

## Parameters, threads, panels (41–50)
- [x] 41. Distinguish default vs customized values.
- [x] 42. Confirm successful reset.
- [ ] 43. Explain disabled/incompatible settings.
- [x] 44. Animate thread swatch color change subtly.
- [x] 45. Strong persistent active-thread marker.
- [x] 46. Consistent hidden-thread state across list and canvas.
- [x] 47. Highlight thread position after reorder.
- [x] 48. Clear collapsible-panel chevrons and state.
- [x] 49. Section-modified indicator.
- [x] 50. Rich but concise tooltips with shortcut and availability.

## Navigation, productivity, finish (51–60)
- [x] 51. Active-workspace transition cue.
- [x] 52. Clear keyboard focus in lists and menus.
- [x] 53. Search active/results/empty feedback.
- [x] 54. Explain disabled actions via accessible help.
- [x] 55. Stronger unsaved-changes indicator, no permanent motion.
- [ ] 56. Distinct saving state driven by real events.
- [x] 57. Contextual validation errors near the control.
- [x] 58. Keyboard shortcuts produce the same UI feedback as clicks.
- [x] 59. Group repeated notifications rather than stacking them.
- [ ] 60. Audit timing, easing, contrast and reduced-motion consistency.

## Definition of done
For each item: identify the real Qt/QML controller signal, avoid speculative status; add focused tests, check keyboard and reduced-motion behavior, run formatting/lint and CI, and attach actual CPU/GPU screenshots where applicable. Keep PR #317 draft until reviewed.

## Remaining constraints and validation (not silently claimed as complete)
- **29 — Drag duplication preview**: successful duplication has a real confirmation toast and selection feedback; a distinct *in-drag* duplicate preview cannot be delivered without adding a new drag-duplicate gesture (workflow change, prohibited by this PR's scope).
- **30 — Snap target kind**: underlying snap guides expose real position/orientation but not a source category. UI displays actual horizontal/vertical alignment, not a guessed grid/guide/object label.
- **35 — Vectorization running indicator**: the current vector refresh/convert API is synchronous and has no real busy/progress signal; introducing a fake spinner is deliberately avoided.
- **43 — Incompatible parameter explanations**: `StudioButton.disabledReason` and representative disabled actions are wired, but full disabled-state coverage across all panels needs an exhaustive UI audit.
- **56 — Saving-in-progress state**: `saveProject[As]` is synchronous and returns a bool; true save confirmation is implemented but an observable intermediate saving state would require an async lifecycle.
- **60 — QA/sign-off**: source-level tests cover reduced-motion tokens, save result, stage signals, preview readiness, selection ID and hover side effects. Qt runtime tests, Windows/GPU screenshots, keyboard and light/dark accessibility, and the latest CI are pending.

### Source evidence (not a test result)
- Controller-based feedback: `qml/Main.qml`, `qml/spaces/SpaceHost.qml`, `segmentation_space_controller.hpp`, `qml/spaces/SegmentationSpace.qml`.
- Canvas feedback: `gpu_canvas.cpp`, `gpu_canvas.hpp`, `qml/TransformHandles.qml`.
- Inputs/navigation: `qml/Studio*.qml`, `qml/ToolRail.qml`, `qml/ParamField.qml`, `qml/ParamSection.qml`, `qml/ThreadPanel.qml`, `qml/HistoryPanel.qml`.
- Tests: `tests/unit/desktop_v2/test_studio_design.cpp`, `test_studio_shell.cpp`, `test_studio_space_segmentation.cpp`.
