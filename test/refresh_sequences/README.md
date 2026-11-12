# Page-turn sequence tests

Run from the repository root:

```sh
python3 test/refresh_sequences/run.py --report build/page-turn-analysis/refresh-sequences.json
python3 test/refresh_sequences/run.py --sanitize
```

The runner compiles the **current production** `FreeInkDisplay.cpp`, SSD1677,
UC8179 and UC8279 X4 Pro drivers. It copies those sources into a temporary
directory and substitutes only the SDK's existing host Arduino/board/SPI/heap
and recording-bus stubs, following `freeink-sdk/.../test/host/run_pro.py`.
No driver behavior is reimplemented. The oracle retains written registers/RAM
to inspect the exact OLD/NEW plane at each activation; it does not model ink.
The report hashes every compiled production source, waveform table, test input
and stub so a result can be tied to the code that produced it. Host vectors hold
the captured payloads only; they add no firmware allocation or stack usage.

Each of the three controllers runs five 100-turn workloads plus 100 three-update
overlay cycles in both single- and
dual-buffer builds; UC8279 additionally runs its `0x02`, `0x03`, `0x68` and `0x69`
variants. The core workloads give 9,600 display submissions:

- Blocking B/W, with HALF every 10th turn and FULL every 50th turn.
- Shadowed asynchronous B/W with the live framebuffer overwritten immediately
  after submission. Finishing must retain the submitted frame as OLD and must
  not activate another waveform. Finishing twice must do nothing.
- Repeated overlay AA with the same periodic/manual clean schedule: one base
  activation plus one grayscale activation per page. Plane uploads, legacy
  preconditioning and RAM cleanup must not add an activation.
- Alternating AA and B/W, including leaving AA on a periodic/manual clean.
  The next B/W transition must use the maintained baseline or the explicit
  clean, never stale grayscale selector bits.
- Night mode toggled every five turns. The real SDK capabilities are fed to
  the production `ReaderGrayscalePlan`; all 50 inverted pages disable gray
  work, every toggle uses one cleaning activation, and logical framebuffer
  bytes survive inversion. Legacy gray calls in night mode must write nothing.
- Monochrome page, toolbar sheet, then restored page: each of 100 cycles has
  three submissions. Restoring the snapshot writes no controller RAM, so FAST
  close must erase against the displayed sheet. HALF/FULL closes retain their
  normal cleanup seeds. Night mode toggles every ten cycles. All 300 submissions
  activate once; no grayscale waveform runs. This uses the SDK directly; the
  reader-overlay tests separately exercise the renderer's snapshot restore.

Direct cover/cancellation/B/W recovery adds 3,000 submissions across the UC
controllers in both buffering modes. The full run therefore checks **12,600
submissions in 82 workloads**. UC8279 window requests are not covered here.
Upstream SDK commit `adc73104` makes the X4 driver refresh only the requested
region. The reader never calls `displayWindow()`, and no host test covers that
path yet. The Direct base never runs a B/W activation, even when FULL is
requested; this matches the SDK's `testDirectSleep`. Direct
checks require complete planes before a single activation and a coherent B/W
baseline after success or cancellation. Ordinary Overlay LUT selection remains
protected separately from Direct quality-image waveforms.

The single-buffer AA workloads use `displayGrayscaleBase()`, matching the
deployed EPUB base path. Dual-buffer AA workloads use `displayBuffer()` for
the base, matching the reader's ordinary `displayBaseWithRefreshCycle()`
path, which swaps the active framebuffer. The report includes both configurations
to catch ownership regressions, but their SPI counts are not interchangeable.

The byte oracle checks the full 48,000-byte image, including blank, black,
sparse, dense and shifting patterns. UC drivers additionally check all 600
addressed gates: UC8179 reverses rows, UC8279 has a 120-gate offset, and padding
must stay white. It checks previous-frame differential baselines, complement
seeds for HALF, absolute white seeds for FULL, the mandatory UC8279 partial
window, and AA plane encoding/restoration. Assertions use explicit failures,
so `NDEBUG` cannot silently disable them.

## Evidence and limits

The contracts come from the pinned SDK's `Ssd1677Driver`, `Uc8179Driver`,
`Uc8279X4Driver` and `FreeInkDisplay` implementations. The SDK's `test_pro.cpp`
checks the same Direct-base activation contract in `testDirectSleep`.

`plane_payload_bytes`, command/data bytes, data transactions and activation
counts are measured from the executed production code. `wait_calls` counts
calls only, not elapsed time. These are not panel response times, MCU rendering
times, contrast measurements or predicted ghosting percentages. A passing
baseline test proves the driver supplies the intended transition data; it
does not prove how a physical panel responds to the waveform.

The workload deliberately does not exercise UC8179 AA-to-**async** B/W or
dual-buffer SSD1677 `displayGrayscaleBase()` followed by ordinary B/W. Those
API combinations have different baseline behavior and are outside the deployed
reader path: the repository enables single buffering, and UC8179 advertises no
async grayscale base. They require a separate API-contract decision before
being treated as supported reader optimizations.
