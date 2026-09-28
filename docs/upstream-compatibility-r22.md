# CrossPoint develop integration: X4 Pro r22

## Upstream foundation

The local branch base advances from `1d61100f90d2e7e32965c14301320d9989a7ab71`
to `4a6283db9c9692e059eaf2d57d5b7a36d85a2f4d` on `develop`.
The SDK pin remains `111fdcc7f0176c3ee38391a160ee296bf492dbd8`.
The retained additions remain an uncommitted overlay. No push or PR is created.

Adopted upstream work and human authors:

- #3652: avoid main-loop contention, Sung-jin Brian Hong <serialx@serialx.net>.
  This landed after its r19 adaptation; `src/main.cpp` and `RenderLock.h` now
  match develop exactly, retiring that local scheduling patch.
- #3748: landscape toolbar and vector font sizes, Uri Tauber <uritaube@gmail.com>.
  Toolbar, font-size policy and SD font integration match upstream exactly.
  Vector fonts offer every whole point from 8 through 22; built-in bitmap sizes
  and installed `.cpfont` choices retain their existing restrictions.
- #3724: unreadable-book wake recovery, Ninos Yomo
  <32492083+SurayaAtouraya@users.noreply.github.com>.
  Clear the remembered book before parsing; restore it only after rendering.
- #3749: preserve leading words in Library sorting/search, Uri Tauber.
  The article-stripping API is removed rather than maintained as an option.

## Adaptation of local features

Library search, filters and names-only Authors/Series retain the indexed
upstream design. Both title and series normalization now preserve leading words;
for example, `The Earthsea` and `Earthsea` are distinct series identities.
Local fold revision 6 maps to upstream's new normalization while keeping the
existing full author/series identities and arrival ordering. Old indexes rebuild
once and retain `firstSeen`. EPUB positions, bookmarks and chapter caches are
unchanged. Normalization adds no new runtime allocation.

The EPUB reader retains the existing incremental-pagination, navigation and
refresh-commit safeguards. The new remember-once marker is published only after
our display-commit check; the end-of-book screen applies that check as well.
The upstream atomic marker and remember-once flag are per-reader scalar fields,
with no added heap buffer or task. New coverage checks unsuccessful first refresh,
unreadable books, once-only state writes and end-of-book rendering.

The existing r21 bundled-Expat configuration remains in place. Optional search
still initializes on demand; this integration adds no active-reading index work.

## Validation and limits

The package's `verification/` directory records the final complete native and
LLVM22 ASan/UBSan suites, the pinned upstream suite, SDK runners, static analysis,
firmware build and image validation. The previous article-stripping test is
superseded by upstream's leading-word-preservation test; no test is disabled.
Additional tests cover Library cache migration and vector/built-in size selection.

Host tests establish logical behavior and memory safety within their seams.
They do not establish device timing, physical ghosting, SD durability or peak heap.
After flashing, start with anti-aliasing off, allow Library's one-time reindex,
check Authors/Series drill-in and leading-word searches, then check normal reading,
sleep/wake, landscape controls and installed vector sizes. An unreadable EPUB
should return safely without being repeatedly reopened on wake.

One intended image: `firmware-x4pro-epub-r22-final.bin`, built for
`x4pro-gh_release`. `build/FLASH-LATEST.md` is updated only after all release gates.
