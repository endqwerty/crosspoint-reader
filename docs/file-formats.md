# File Formats

These formats describe the SD-card cache files under `/.crosspoint/epub_<hash>/`.
All POD fields are written in the ESP32 little-endian representation used by
`Serialization.h`; strings are length-prefixed UTF-8.

## `book.bin`

### Version 10

`book.bin` stores EPUB metadata plus lookup tables for spine and TOC entries.
The current firmware writes this version from `BookMetadataCache`.

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 10
#define MAX_STRING_LENGTH 65535

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

struct Metadata {
    String title [[comment("Book title")]];
    String author [[comment("Book author")]];
    String language [[comment("Book language code")]];
    String coverItemHref [[comment("Path to cover image")]];
    String textReferenceHref [[comment("Path to guided first text reference")]];
};

struct SpineEntry {
    String href [[comment("Resource path")]];
    u32 cumulativeSize [[comment("Cumulative uncompressed spine size through this entry")]];
    s16 tocIndex [[comment("Index into TOC, or inherited/previous TOC index when no direct entry exists")]];
};

struct TocEntry {
    String title [[comment("Chapter/section title")]];
    String href [[comment("Resource path")]];
    String anchor [[comment("Fragment identifier")]];
    u8 level [[comment("Nesting level")]];
    s16 spineIndex [[comment("Index into spine (-1 if none)")]];
};

struct BookBin {
    u8 version;
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    u32 lutOffset [[comment("Offset to lookup tables")]];
    u16 spineCount;
    u16 tocCount;

    Metadata metadata;

    u32 currentOffset = $;
    if (currentOffset != lutOffset) {
        std::warning(std::format("LUT offset mismatch: expected 0x{:X}, got 0x{:X}", lutOffset, currentOffset));
    }

    u32 spineLut[spineCount] [[comment("Spine entry offsets")]];
    u32 tocLut[tocCount] [[comment("TOC entry offsets")]];

    SpineEntry spines[spineCount];
    TocEntry toc[tocCount];
};

BookBin book @ 0x00;

u32 fileSize = std::mem::size();
u32 parsedSize = $;
if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

## `section.bin`

### Version 50

The header adds `paragraphIndentSpaces` after `extraParagraphSpacing`, as in
upstream version 50, and is now 44 bytes. The value participates in cache
validation, so sections with different indentation settings are rebuilt. Current
partial sentinel: 232.

### Version 49

Version 49 adopts upstream version 48 Korean layout: Hangul words wrap at spaces,
justification stretches word gaps, and hyphenation can split a word at a legal
line-end boundary without adding a hyphen. The serialized layout is unchanged.
The local version was already 48 before this change, so both complete version 48
and partial sentinel 234 must rebuild. Its partial sentinel is 233. Reading
positions, bookmarks and Library metadata are preserved.

### Earlier local version 48

The 43-byte section header adds signed `characterSpacing` (pixels) and unsigned
`wordSpacingPercent` after `focusReadingEnabled`; both participate in cache
validation. Each TextBlock's 24-byte serialized BlockStyle stores
`characterSpacing` after `directionDefined`. Word spacing is resolved into
cached word positions during layout.

Version 48 distinguishes this layout from both earlier version 47 formats:
local reliability builds used the version 46 byte layout, while upstream
version 47 introduced the spacing fields. Both complete version 47 caches and
their partial sentinel 235 are rebuilt. Version 48's partial sentinel is 234.

The layout-time `WordStore` arena is not serialized. Its words still enter the
same `TextBlock` wire layout, so the arena and reclaim retries do not change
version 48. Vector fonts use the existing `fontId` header field; upstream derives
the ID from the font family and point size with a TTF-specific salt. Switching
font family, size, or between cpfont and TTF therefore retains the existing
font-ID cache validation (subject to the hash's usual collision limit).

### Earlier local version 47

Version 47 keeps the version 46 byte layout. It invalidates section caches that
could have been accepted after a parser allocation failure or unchecked SD write.
Both complete and partial layouts rebuild automatically; progress, bookmarks,
and original EPUB files are separate and remain intact. Fixed-size link/footnote
fields now write zero padding after their terminator. Readers reject incomplete
fields, invalid boolean values, and oversized cached ruby/image-path strings.

Section installation writes `.part`, checks each write/seek and the final close,
renames the prior cache to `.bak`, then installs the new cache. A failed install
restores the backup where possible. A leftover `.bak` at the next open is
conservatively restored, even if a newer live cache exists; this may repeat layout
work but preserves a previously committed copy. It is not a claim of atomic
multi-file transactions or physical SD power-loss durability.

### Version 46

Version 46 keeps the version 45 serialized layout unchanged. It was bumped
because ordered lists now number their items, `list-style-type: none`
suppresses list markers, and `<ul>`/`<ol>` containers contribute their own
margins and padding to child block insets, changing cached word contents and
page layout.

### Version 45

Version 45 keeps the version 44 serialized layout unchanged. It was bumped
because internal EPUB links now preserve CSS superscript and subscript styles,
changing their cached word-style flags and page layout.

### Version 44

Each file in `sections/*.bin` stores one laid-out spine section. The header is
also the cache-busting key: if any layout-affecting setting differs from the
current reader settings, the section is discarded and rebuilt.

Version 44 appends the internal-link rectangles produced during text layout to
each serialized page. The reader uses these rectangles for touch navigation;
older caches are rebuilt because they contain no link geometry.

Version 43 keeps the version 42 serialized layout unchanged. It was bumped
because paragraph base direction now excludes direction changes from inline
elements.

Version 42 keeps the version 41 serialized layout unchanged. It was bumped
because closing a block now strips inherited vertical margins and padding.

Version 41 keeps the version 40 serialized layout unchanged. It was bumped
because simple HTML table rows are now laid out as positioned columns rather
than flattened paragraphs with synthetic row/cell labels.

Version 40 keeps the version 39 serialized layout unchanged. It was bumped
because ruby groups now remain intact when large text blocks are soft-flushed.

Version 39 keeps the version 38 serialized layout unchanged. It was bumped
because image top margins are now clamped to keep full-height images within the
page viewport.

Version 38 keeps the version 37 serialized layout unchanged. It was bumped
because Focus Reading now permits line breaks at visible hyphens and dashes
and hyphenates focus-split words as a whole, changing cached page layout.

Version 37 increases the fixed-size footnote href field from 96 to 256 bytes.
This changes each serialized footnote record from 128 to 288 bytes, so older
section caches must be discarded and rebuilt.

Version 36 keeps the version 35 serialized layout unchanged. It was bumped
because ruby and justified text positioning and CJK line breaking now use
corrected word measurements, so version 35 cached page layouts no longer match.

Version 35 adds a header offset and a `uint32_t` entry per page for the
visible-text offset LUT. The other section LUTs remain unchanged.

Version 34 is binary-identical to version 33. The version was bumped because
word-gap suppression was narrowed to tokens glued together in the source: v33
dropped the gap between any two words meeting at a CJK break opportunity, which
collapsed the spaces between Hangul words, so v33 word positions no longer match
what the layout engine now produces.

Version 30 is binary-identical to version 29. The version was bumped because
Arabic contextual shaping changed text measurement (`getTextAdvanceX` now
measures the shaped visual text), so word positions cached by v29 no longer
match what `drawText` renders.

Version 28 introduced serialized word style bits for underline, strikethrough,
superscript, and subscript. The format also includes:

- cache-busting fields for paragraph alignment, hyphenation, embedded CSS,
  image rendering mode, and Focus Reading
- page offset LUT
- per-page visible-text offset LUT (zero-based Unicode codepoints in `<body>`)
- anchor-to-page map for fragment and footnote navigation
- paragraph and list-item LUTs retained for navigation and legacy sync fallback
- optional per-word Focus Reading split metadata
- per-page footnote entries
- serialized word style bits for underline, strikethrough, superscript, and
  subscript
- flat TextBlock word storage (v29): per-word arrays plus one shared
  NUL-terminated text blob, replacing v28's length-prefixed word strings. The
  on-disk order mirrors the in-RAM arena so the firmware reads a whole block
  payload with a single allocation and a single SD read

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 50
#define MAX_STRING_LENGTH 65535
#define FOOTNOTE_NUMBER_LEN 32
#define FOOTNOTE_HREF_LEN 256

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

enum PageElementTag : u8 {
    TAG_PageLine = 1,
    TAG_PageImage = 2,
    TAG_PageHorizontalRule = 3
};

enum WordStyle : u8 {
    REGULAR = 0,
    BOLD = 1,
    ITALIC = 2,
    BOLD_ITALIC = 3,
    UNDERLINE = 4,
    STRIKETHROUGH = 8,
    SUP = 16,
    SUB = 32
};

enum TextAlign : u8 {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    NONE = 4
};

struct BlockStyle {
    TextAlign alignment;
    bool textAlignDefined;
    s16 marginTop;
    s16 marginBottom;
    s16 marginLeft;
    s16 marginRight;
    s16 paddingTop;
    s16 paddingBottom;
    s16 paddingLeft;
    s16 paddingRight;
    s16 textIndent;
    bool textIndentDefined;
    bool isRtl;
    bool directionDefined;
    s8 characterSpacing;
};

struct TextBlock {
    u16 wordCount;
    u8 hasFocus;
    u16 textBytes [[comment("Total size of text[], including one NUL per word")]];

    if (wordCount > 0) {
        u16 textOff[wordCount] [[comment("Byte offset of word i's text within text[]")]];
        s16 wordXPos[wordCount];
        if (hasFocus != 0) {
            u16 wordFocusSuffixX[wordCount] [[comment("Suffix x offset from word start")]];
        }
        WordStyle wordStyle[wordCount];
        if (hasFocus != 0) {
            u8 wordFocusBoundary[wordCount] [[comment("UTF-8 byte boundary between bold prefix and suffix")]];
        }
        char text[textBytes] [[comment("All words back to back, each NUL-terminated")]];
    }

    BlockStyle blockStyle;
};

struct ImageBlock {
    String imagePath;
    String srcPath;
    s16 width;
    s16 height;
};

struct PageLine {
    s16 xPos;
    s16 yPos;
    TextBlock block;
};

struct PageImage {
    s16 xPos;
    s16 yPos;
    ImageBlock image;
};

struct PageHorizontalRule {
    s16 xPos;
    s16 yPos;
    u16 width;
    u8 thickness;
};

struct PageElement {
    PageElementTag pageElementType;
    if (pageElementType == TAG_PageLine) {
        PageLine pageLine [[inline]];
    } else if (pageElementType == TAG_PageImage) {
        PageImage pageImage [[inline]];
    } else if (pageElementType == TAG_PageHorizontalRule) {
        PageHorizontalRule horizontalRule [[inline]];
    } else {
        std::error(std::format("Unknown page element type: {}", pageElementType));
    }
};

struct FootnoteEntry {
    char number[FOOTNOTE_NUMBER_LEN];
    char href[FOOTNOTE_HREF_LEN];
};

struct Page {
    u16 elementCount;
    PageElement elements[elementCount] [[inline]];

    u16 footnoteCount;
    FootnoteEntry footnotes[footnoteCount];
};

struct AnchorEntry {
    String anchor;
    u16 page;
};

struct AnchorMap {
    u16 count;
    AnchorEntry entries[count];
};

struct ParagraphLut {
    u16 count;
    u16 paragraphIndex[count];
};

struct SectionBin {
    u8 version;
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    s32 fontId;
    float lineCompression;
    bool extraParagraphSpacing;
    u8 paragraphIndentSpaces;
    u8 paragraphAlignment;
    u16 viewportWidth;
    u16 viewportHeight;
    bool hyphenationEnabled;
    bool embeddedStyle;
    u8 imageRendering;
    bool focusReadingEnabled;
    s8 characterSpacing;
    u8 wordSpacingPercent;

    u16 pageCount;
    u32 pageLutOffset;
    u32 anchorMapOffset;
    u32 paragraphLutOffset;
    u32 listItemLutOffset;
    u32 visibleTextLutOffset;

    Page pages[pageCount];

    u32 currentOffset = $;
    if (currentOffset != pageLutOffset) {
        std::warning(std::format("Page LUT offset mismatch: expected 0x{:X}, got 0x{:X}", pageLutOffset, currentOffset));
    }

    u32 pageLut[pageCount] [[comment("Page data offsets")]];

    if (anchorMapOffset != 0) {
        AnchorMap anchorMap @ anchorMapOffset;
    }

    if (paragraphLutOffset != 0) {
        ParagraphLut paragraphLut @ paragraphLutOffset;
    }

    if (listItemLutOffset != 0 && paragraphLutOffset != 0) {
        u16 listItemIndex[paragraphLut.count] @ listItemLutOffset;
    }

    if (visibleTextLutOffset != 0) {
	u32 visibleTextOffset[pageCount] @ visibleTextLutOffset;
    }
};

SectionBin section @ 0x00;

u32 fileSize = std::mem::size();
u32 parsedSize = $;
if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

## CLX1 — library index (`.crosspoint/library.idx`)

Written by `lib/LibraryIndex/LibraryBuilder.cpp`, read by `LibraryIndexFile`. One
file describing every book on the card, so the shelf can sort and search
thousands of titles without opening any of them.

Format version 4. Versions 2 and 3 are read only during reconciliation to preserve
`firstSeen` arrival order and `nextFirstSeen`; they are never exposed directly by
the current Library UI. Their EPUB metadata is parsed again into the new format.
The index migration does not change EPUB reading-position or section-cache files.
Fold/sort revision 6 preserves leading words in title and series sorting and search,
adopting upstream fold revision 4 while retaining the local author and arrival rules.
Reconciliation rebuilds keys and ranks while preserving `firstSeen`.

### Layout

| Section | Offset | Contents |
|---|---|---|
| Header | 0 | 64 bytes, `ClixHeader` |
| Folders | `folderStart` | length-prefixed paths, one per folder |
| Records | `recordStart` | `bookCount` × 128-byte `ClixRecord` |
| Permutations | `permStart` | `bookCount` u16 author, arrival, then series order |
| Series | `seriesStart` | `seriesCount` × 64-byte series entries |
| Series references | `seriesRefStart` | `bookCount` × 4-byte references, parallel to title-order records |
| Name blob | `nameStart` | per record: path hash, name, canonical author, title, source author (see below) |

Sections are 512-byte aligned so each starts on an SD block boundary.

### Records are exactly 128 bytes

A fixed stride is what lets the reader seek straight to record *n* without an
offset table, and read a screenful in one 4 KB block. `static_assert` enforces it.

Each record carries `fold[96]`, the title normalised for search and sorting —
accents stripped, case dropped, leading articles preserved — and `authorKey[12]`.
Since fold/sort revision 4, the author field is binary: four prefix bytes followed
by an eight-byte big-endian FNV-1a fingerprint of the complete normalized author
identity. A zero `authorKeyLen` means no normalized identity. The normalization
cleans the name, ignores initials, and sorts every remaining word, so "Victor
Hugo" and "Hugo Victor" group together without merging unrelated authors whose
names share a short prefix.

The builder verifies the normalized stored source spellings before sharing a
canonical author name; a conflicting fingerprint fails the rebuild and retains
the previous index. Directory grouping uses this identity rather than the
potentially truncated display label. Initials-only and unknown names retain
their separate display-label behavior. The surname sort still uses its own
12-byte prefix, followed by canonical-author identity and title ordinal, keeping
each author's books contiguous. Records stay 128 bytes and the author sort keys
stay 14 bytes per book. Older fold revisions rebuild once, preserving arrival
history and EPUB reading positions.

The byte before the folded title records metadata extraction status: not
attempted, extracted, or failed. The final four bytes contain the packed FAT
modification date and time returned by SdFat. A zero timestamp is not trusted.
These fields occupy the alignment and reserved bytes from version 1, so the
record remains exactly 128 bytes.

Fold/sort revision 5 follows upstream arrival ordering: file modification time
is the primary key, and `firstSeen` breaks equal timestamps (including zero).
The Added shelf uses this order; the separate Recent shelf remains reading
history. An older fold revision rebuilds once while retaining `firstSeen` and
reading positions. The transient timestamp array costs four bytes per book only
during a rebuild; if allocation fails, ordering falls back to `firstSeen`, as in
upstream. `recentRowsFor` resolves up to 16 complete-path identities in two
chunked passes with a fallible temporary 4096-byte buffer; it stores no lookup
cache on the index object.

The header records whether EPUB metadata extraction was enabled for the build.
This prevents a metadata-disabled rebuild from making filename fallbacks look
fresh to a later metadata-enabled build.

### The name blob

Per record, at `nameStart + nameOff`:

```text
[u64 pathHash]    FNV-1a fingerprint of the complete path
[nameLen bytes]  filename, without the directory
[u8][author]     display author, one spelling chosen per authorKey across the library
[u8][title]      the book's own title, or length 0 if it never gave one
[u8][source]     cleaned author spelling before the library-wide spelling vote
```

The filename must stay the first textual field and stay the filename: `readPath`
rebuilds a book's path from it, so writing the display title there makes the book
impossible to open. That was a real defect, and it is why title has its own field.

The source author is separate from the displayed canonical author so a later
rebuild can repeat the spelling vote after books are added or removed. Existing
display reads still stop at the author or title fields and retain their offsets.

### Freshness and unchanged rebuilds

Reconciliation treats the persisted 64-bit complete-path fingerprint as the
book identity. Metadata is reused only when the fingerprint, size, nonzero FAT
timestamp, format version, fold version, metadata mode, and expected extraction status agree.
EPUBs with a zero timestamp or a previous extraction failure are parsed again.

If every current record reuses metadata, the old and new counts agree, and no
unreadable entry or scan limit was seen, the staging files are discarded and the live index is
left byte-for-byte unchanged. A normal rebuild action is therefore a freshness
check, not a forced metadata reread.

### Header flags

`RANKS_DEGRADED` says one or more orders fell back to walk order because a
checked sort allocation failed. Title and author each use a phase-local
`SortKey[bookCount]` allocation (14 bytes per book, 57,344 bytes at the 4,096-book
format ceiling); the first array is released before the second is requested.
Sorting is therefore best effort through the full format limit rather than
being disabled at an arbitrary library size.

`DEDUP_DEGRADED` says a directory exceeded the fixed 1024-entry duplicate-key
buffer, or that its fallible 8 KiB allocation failed. The walk still indexes
every enumerated book; it only stops remembering additional identities for
duplicate-dirent detection, so a damaged FAT may expose duplicates but cannot
make a real book disappear.

`selfSize` is the expected file size. Comparing it against the real one is a free
truncation guard: a build cut short by a power failure cannot pass.


### Series records (version 4)

Each 64-byte series entry contains `bookCount` (u16 at offset 0), `nameLen` (u8
at offset 2), `identity` (u32 at offset 3), and up to 57 UTF-8 name bytes at
offset 7. `identity` is FNV-1a of the full normalized series
name with leading words preserved, computed before display truncation. It is carried forward when metadata
is reused. Version 3 had a 61-byte display name and no persisted identity.

Each 4-byte reference contains `seriesId` and `seriesIndex`, both u16. `0xffff`
means no series or no position, respectively. Positions use hundredths (`250`
means 2.5); zero is valid. The highest finite position is 655.34. Series order
places grouped books before standalone books, orders groups by a 16-byte folded
prefix plus full-name digest, and orders a group's books numerically, with
unnumbered books last. The header's `knownSeriesCount` marks the start of the
standalone block. Prefix/digest ordering is deterministic rather than full
lexicographic ordering for names sharing their first 16 folded bytes.

`LIMITS_REACHED` (header flag bit 2) marks an incomplete scan: another eligible
book was found beyond 4,096, a folder exceeded five levels below the scan root,
or an entry was skipped as unreadable/unrepresentable. Exactly 4,096 books alone
does not set the flag. `DEDUP_DEGRADED` remains separate from scan completeness.

### Library book state (`.crosspoint/library-state/<key>.bin`)

Favorites and reading status live outside the rebuildable index, keyed by the
16-digit hexadecimal FNV-1a 64-bit fingerprint of the raw complete book path.
The record is exactly 16 bytes, explicitly encoded independently of compiler
padding:

| Offset | Size | Value |
|---|---|---|
| 0 | 4 | ASCII `LBS1` |
| 4 | 8 | Book path key, little endian |
| 12 | 1 | Favorite in bit 0; reading status in bits 1–2 (0 unread, 1 reading, 2 finished) |
| 13 | 1 | Flags XOR `0xa5` |
| 14 | 2 | Reserved, zero |

Readers validate magic, key, flags, check byte, reserved bytes and exact size.
A missing record means unread and not favorite. Corrupt records fail instead of
silently replacing saved state with defaults. A valid `.bak` can recover an
unreadable main record. Writes validate a closed `.new` file, restore a verified
backup if necessary, then install by rename with rollback. Unchanged state
performs no write. State currently follows the raw path, so renaming/moving a
book does not automatically migrate its favorite/reading-status sidecar.
