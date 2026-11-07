# Independent visual judging for OpenStitch

The digitizer must not score its own output as **independent**. The existing
`scripts/openstitch_evaluate.py` remains the final deterministic validator.

## Running

1. Produce **real** OSP/DST, source image, semantic map, preview and CLI JSON
   reports. Put them in one local directory; never commit private source images.
2. Write `manifest.json` with `source_id`, `size_mm: [width,height]`,
   `digitizer` (unique session identifier), `artifacts` mapping all nine
   required file keys to relative paths, and `checks` in the format of
   `openstitch-evaluate`. The judge does **not** invent CLI check outcomes.
3. Configure `OPENAI_API_KEY` in a private environment. A key is **not**
   automatically provided by GitHub; never commit it.
4. Run:
   ```sh
   python3 scripts/openstitch_independent_judge.py manifest.json \
      --allow-private-upload -o review.json
   python3 scripts/openstitch_evaluate.py review.json -o score.json
   ```
5. Inspect `score.json`, including blockers. A score is `software_only` until
   physical embroidery is tested.

This uses a separate remote multimodal inference call, not a second pass of
the digitizer's own reasoning. The model sees the original, segmentation,
real render and diagnostics; it does **not** receive the digitizer's hoped-for
score. Model independence is operational, not a guarantee of objectivity.

**Privacy:** explicit opt-in is required because the original may be private.
The image bytes are sent to the configured API; do not run this on images for
which you lack permission. GitHub Actions cannot access the private ChatGPT
Library original. Never upload it to a public workflow artifact.

If credentials, consent, preview or reports are missing, the judge exits
nonzero **without writing a score**. The laboratory can still record a
qualitative self-review, but must label it `self_review`, not independent.

The judge uses OpenAI Responses API and may need a supported vision model.
This integration has not been live-tested without credentials.
