# Studio — Interaction Feedback & Juice (PR #317)

This is a design/implementation backlog, **not** evidence that a feature is shipped.
Scope: Qt Quick UI only; no engine, stitch geometry, embroidery format or workflow changes. Respect `Theme.reducedMotion`; never display synthetic progress or success without an actual controller event. Avoid overlap with V02 and UX automation.

## Canvas and objects (21–30)
- [ ] 21. Preselection highlight on hover, only if hover target is exposed.
- [ ] 22. Short selection confirmation on click.
- [ ] 23. Distinguish active object from other selected objects.
- [ ] 24. Contextual move/rotate/resize/node cursors.
- [ ] 25. Visible locked-object feedback.
- [ ] 26. Show rotation pivot during rotation.
- [ ] 27. Live width/height readout while resizing.
- [ ] 28. Show constrained aspect-ratio state.
- [ ] 29. Distinguish duplication preview from movement.
- [ ] 30. Indicate real snap target (grid/guide/object) when exposed.

## Digitizing stages (31–40)
- [ ] 31. Confirm successful image import.
- [ ] 32. Segmentation activity indicator.
- [ ] 33. Segmentation completion feedback.
- [ ] 34. Linked region highlight in canvas and list.
- [ ] 35. Vectorization activity indicator.
- [ ] 36. Vector result available feedback.
- [ ] 37. Stitch generation activity indicator.
- [ ] 38. Stitch result available feedback.
- [ ] 39. Visually distinguish stale results from valid ones.
- [ ] 40. Subtle preview-ready indication.

## Parameters, threads, panels (41–50)
- [ ] 41. Distinguish default vs customized values.
- [ ] 42. Confirm successful reset.
- [ ] 43. Explain disabled/incompatible settings.
- [ ] 44. Animate thread swatch color change subtly.
- [ ] 45. Strong persistent active-thread marker.
- [ ] 46. Consistent hidden-thread state across list and canvas.
- [ ] 47. Highlight thread position after reorder.
- [ ] 48. Clear collapsible-panel chevrons and state.
- [ ] 49. Section-modified indicator.
- [ ] 50. Rich but concise tooltips with shortcut and availability.

## Navigation, productivity, finish (51–60)
- [ ] 51. Active-workspace transition cue.
- [ ] 52. Clear keyboard focus in lists and menus.
- [ ] 53. Search active/results/empty feedback.
- [ ] 54. Explain disabled actions via accessible help.
- [ ] 55. Stronger unsaved-changes indicator, no permanent motion.
- [ ] 56. Distinct saving state driven by real events.
- [ ] 57. Contextual validation errors near the control.
- [ ] 58. Keyboard shortcuts produce the same UI feedback as clicks.
- [ ] 59. Group repeated notifications rather than stacking them.
- [ ] 60. Audit timing, easing, contrast and reduced-motion consistency.

## Definition of done
For each item: identify the real Qt/QML controller signal, avoid speculative status; add focused tests, check keyboard and reduced-motion behavior, run formatting/lint and CI, and attach actual CPU/GPU screenshots where applicable. Keep PR #317 draft until reviewed.
