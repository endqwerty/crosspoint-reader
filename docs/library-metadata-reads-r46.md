# Library metadata reads r46

All r45 improvements remain on develop `93e98bb` and SDK `111fdcc7`.
This change reduces storage calls while materializing library rows and reusing
cached metadata during a rebuild.

## Mechanism

LibraryListActivity::rowTextFor uses readTitleAndAuthor, and LibraryBuilder uses
readTitleAndSourceAuthor when reusing an existing record. Both share readMetadata.
Its 64-byte buffer combines nearby short fields and their one-byte lengths.
Previously long output fields were also copied through repeated 64-byte refills.

After consuming any buffered bytes, copyBytes now reads a remaining span of at
least 64 bytes directly into the already-sized output string. Short remainders and
length bytes retain the buffered path. The existing length check still precedes
payload copying; readAt still owns cursor tracking, HAL access and failure status.
A failed read still clears both outputs through the existing return path.

This uses no additional allocation or buffer: the existing output strings and
64-byte stack buffer are reused. No resident cache, metadata/cache format change,
sorting change or extra active-reading work is introduced.

## Synthetic evidence

The same production index reader was measured before and after the change using
729 combinations of field lengths 0, 1, 63, 64, 65, 127, 128, 254 and 255. Each
combination exercises both canonical-author and source-author reads, for 1,458
operations. Every returned title/author is checked against the original string.

| Output | Read calls before | Read calls after | Seeks before/after | Bytes before | Bytes after |
| --- | ---: | ---: | ---: | ---: | ---: |
| Title and canonical author | 2,961 | 2,142 | 729 | 182,360 | 173,944 |
| Title and source author | 3,564 | 2,711 | 1,215 | 193,023 | 193,023 |

Combined read calls fall from 6,525 to 4,853, about 26 percent in this boundary
workload. This is not a claim about a whole Library load or actual SD latency.
Maximum-length fields use four reads instead of eight for title/canonical author,
and five instead of nine for title/source author. The ordinary short-fields test
still uses one read. Existing empty-field and spelling behavior is preserved.

The existing interrupted-read test now covers all five reads in the maximum-field
source-author path, both outright read failures and short reads. It checks cleared
outputs and successful retry, including the direct-to-string path.

## Device check

Flash the single final X4 Pro image linked by build/FLASH-LATEST.md. Browse rows
with long titles/authors and refresh Library; check titles and author spelling.
No recordings or cache deletion are needed. Start with AA off when reading.
Actual device timing, peak heap and panel ghosting remain unmeasured.
No commits, pushes or PRs were created.
