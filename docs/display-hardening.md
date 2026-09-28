# Display hardening and Direct sleep covers

The display integration follows the RC02 FreeInk SDK sources at
`2cca22fe44862215e029a416d5ff6fddcb3e593e`, retaining the local FULL cleanup,
regional refresh, and UC8179 text-AA safeguards. A source handoff must include
the nested SDK changes, not only the root submodule reference. Direct grayscale
and `UltraChipDirectGrayLuts.h` are included in this upstream baseline.

## Behavior

- SSD1677 always honors the first-paint cleanup after boot/wake, including when
  the sunlight fading fix requests panel power-off after each refresh. Only
  that first FAST is promoted; ordinary FAST turns keep their existing waveform.
- Sleep covers prefer Direct grayscale on UC8179 and UC8279 when the SDK reports
  support, otherwise retaining Absolute/Overlay fallbacks. Direct sends both
  complete planes before one display activation. White pixels in a transparent
  cover overlay preserve the existing background in both grayscale planes.
- An explicit or queued FULL cleanup is painted before a Direct pass. This adds
  a B/W cleanup activation for that request; ordinary HALF/FAST Direct passes
  still defer activation until both grayscale planes are complete. The cleanup
  preserves the host canvas pointer and content in single and dual buffering.
- Direct mode cancellation rejects incomplete planes and requests a coherent
  B/W resync. Returning from a completed Direct image uses the upstream explicit
  destination redraw and cleanup sequence; it is not treated as a fast text turn.
- Existing ordinary Overlay text waveforms remain unchanged. The new UC8179
  dark-gray split and UC8279 quality bank are not forced onto sparse text AA.
  The reader's periodic refresh cadence and manual FULL promotion are retained.
- UC8279 keeps the upstream variant-specific AA tables and image-pass resets.
  Explicit Absolute and Direct image planes use the four-tone image bank;
  ordinary Overlay masks retain the text bank below the existing image threshold.
- UC8279 regional B/W refresh checks alignment, bounds, the old-plane baseline,
  pending cleanup, grayscale state, and every pixel outside the window. Any
  uncertainty falls back to the existing full-screen path. It reuses the existing
  PSRAM B/W snapshot; if that snapshot is unavailable it also falls back.
  Optional low-level row reversal/mirroring builds fall back until separately
  supported. No UI is routed through this new driver capability yet.

No additional framebuffer or heap allocation is introduced. The Direct paths add
small driver state fields and constant waveform data. UC8279's existing quality
bank is now calculated at compile time instead of retaining a mutable runtime
bank. Regional validation scans the existing frame only when a window is
explicitly requested. Full-screen reading adds no regional scan.

## Verification

- `python3 freeink-sdk/libs/display/FreeInkDisplay/test/host/run_pro.py`: actual
  SSD1677/UC8179/UC8279 facade and drivers, single/dual buffering, wake cleanup
  with power-off, plane encoding, Direct activation/cancellation and B/W recovery.
- `python3 test/refresh_sequences/run.py --report build/refresh-sequences.json`:
  11,000 display submissions in 74 workloads. The existing 8,000 reading/overlay
  submissions remain. New workloads cover Direct image/abort/B/W transitions and
  regional requests with invalid coordinates, changed outside pixels, pending
  cleanup, post-grayscale redrive, and all three UC8279 variants. Assertions check
  full plane payloads, gate padding, window registers, and activation counts.
- Direct FULL requests are checked through the real display facade and drivers:
  the cleanup uses the full-screen B/W sequence and full-plane payloads before
  the Direct activation, including panel power-off and repeated Direct images.
- `GfxRefreshTest`: manual FULL promotion works in Direct as well as existing
  grayscale modes; the actual bitmap compositor preserves transparent white
  pixels in both Direct planes in all four orientations. The compositor test
  supplies a known decoded four-tone row; it does not test BMP decoding.
- SDK UC8253 and UC8279 X3 host runners protect the shared Direct mode additions.
- The upstream UC8279 waveform-selection test checks all three variants, sparse
  and dense masks, Absolute/Direct image banks, and image-to-text transitions.

These are software/controller invariants. They do not measure pigment motion,
physical ghosting, or whole-page latency. The stock fast-DU shortcut stays off.
On a device, normal reading with anti-aliasing off, sleep/wake with the sunlight
fading fix, and a book-cover sleep screen are useful checks; no recording is
required. The final flashable filename is provided by the release handoff.

## Upstream sources and attribution

- [SDK #97](https://github.com/Free-Ink/freeink-sdk/pull/97), merged September 13,
  2026: Eszter `<hello@eszter.xyz>`, commit
  `6644bf2fbf7525c36d5f86b7d0e880bd3a009b23`.
- [SDK #98](https://github.com/Free-Ink/freeink-sdk/pull/98), merged September 14,
  2026: Justin Mitchell `<justin@jmitch.com>`, commit
  `5916724f23f9392a1d75bc2f632780f54b0735fa`.
- [CrossPoint #3541](https://github.com/crosspoint-reader/crosspoint-reader/pull/3541),
  merged September 14, 2026: Justin Mitchell `<justin@jmitch.com>`, commit
  `f64a6b2506d3e1bec2a58256e39e3e131e73f1b8`. The RC02 sleep-cover implementation
  includes its Direct-cover, transparent-white, and PNG quantization changes.
- [UC8279 variant AA restoration](https://github.com/Free-Ink/freeink-sdk/commit/2cca22fe44862215e029a416d5ff6fddcb3e593e):
  Justin Mitchell `<justin@jmitch.com>`, commit
  `2cca22fe44862215e029a416d5ff6fddcb3e593e`. Its Absolute image-bank selection,
  image-pass state resets, and waveform-selection tests are retained.
- [SDK #91](https://github.com/Free-Ink/freeink-sdk/pull/91), open when reviewed:
  Mathias Karstaedt `<mathias.karstaedt@gmail.com>`, commit
  `adc731043b7cb8209c283f630edb861debf53305`. Adapted to the new Direct-mode driver
  structure and strengthened with post-grayscale and outside-window validation.

These human authors were verified from the respective GitHub commit records.
Retain their `Co-Authored-By` attribution if this adaptation is later committed.
