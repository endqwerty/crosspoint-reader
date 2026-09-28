# ASCII word-direction probe r45

All r44 improvements remain on develop `93e98bb` and SDK `111fdcc7`.
This change removes repeated direction-class lookup work during text rendering.

## Mechanism

TextBlock probes each word with BidiUtils::detectParagraphLevel. Previously, an
ordinary ASCII letter went through utf8NextCodepoint and bidi_class, which searches
the Unicode class table. A-Z and a-z are already class L in that table. The probe
now returns left-to-right directly on those bytes. All other bytes use the existing
decoder and classifier; punctuation and digit prefixes still consume the same
probe budget. The initial null/limit guard and fallback rules remain unchanged.

startsWithRtl delegates to the same probe with a left-to-right fallback, removing
the duplicate loop. This adds no heap allocation, static table or buffer. The
vendored minibidi implementation, visual shaping, glyph rendering, cache format,
public signatures and font settings remain unchanged. Non-ASCII text gains no
lookup reduction and incurs the small ASCII check before its existing path.

## Synthetic evidence

One test compiles the real C classifier under a reference symbol and wraps only
the requests from production BidiUtils. Shaping-internal lookups are deliberately
outside this counter. The unchanged r44 code was measured with the same harness:
2,000 alternating English words through both public helpers made 4,000 classifier
requests. The candidate makes zero. A numeric-prefix control (12a) falls from
three to two, while Hebrew and Arabic controls retain one each.

The baseline passes output parity tests and fails only the new expected operation
budgets. The optimized build must pass them all. Output comparisons cover every
nonzero ASCII prefix, Latin/RTL/CJK suffixes, six probe limits, five fallback levels,
plus Unicode marks, mixed-language text and explicit direction expectations.
The existing Arabic/Farsi/Urdu shaping tests and page-render tests remain required.

These are deterministic operation counts, not measured device page-turn latency.
The change does not shorten the panel waveform or establish better ghosting.
No physical timing or peak heap result is claimed.

## Device check

Flash the single final X4 Pro image linked by build/FLASH-LATEST.md. Read English
prose and, if available, mixed-language text; check word direction and punctuation.
Start with anti-aliasing off. No recordings or cache deletion are required.
No commits, pushes or PRs were created.
