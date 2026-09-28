# Author read failures r38

Based on freshly fetched develop `93e98bb78702e29868a16a13b80c40e6b36ccdff`
and SDK `111fdcc7f0176c3ee38391a160ee296bf492dbd8`. All r37 changes are retained.

## Confirmed defect and change

`LibraryIndexFile::readAuthor` returns true for valid empty authors and false
for unreadable fields. Structural errors can fail without setting `ioFailed`,
which records transport failures. `LibraryListActivity::authorFor` and
`filterBooks` ignored that return value. The UI could label corrupt metadata as
Unknown Author, drill into that invented heading, or return an incomplete search
without its normal failure state.

Both callers now check the return value. Heading failure stops row construction
using the existing unavailable-view handling; drill-down leaves the current scope
unchanged. Search uses its existing failure handling and discards partial results.
Valid empty author metadata still forms the Unknown Author group and permits
series search. Existing sticky transport failures remain checked.

No allocation, buffer, class field, SD request, cache format or public API is added.
The successful path checks an already-returned boolean. No page-reading code changes.
This is a correctness fix, not a measured device-speed or ghosting improvement.

## Regression evidence

Six tests cover real index-reader bounds failures, author label rejection,
collapsed-row rendering, unchanged drill-down scope, partial search rejection and
retry, and valid empty-author series search with unchanged read counts. Four UI
regressions fail on r37; the two storage/valid-empty controls pass. All six pass
with the caller fix. The storage test proves the real reader can return false
without a transport error; the UI fixture models that exact outcome separately
from transport failures. These are host fixtures, not physical SD corruption tests.

## Upstream compatibility

A fresh fetch leaves develop unchanged. Reviewed open PR #3651 (head
`8552ecaaa543dbaa56c83463671b3562a63c846e`) introduces file-as ordering fields;
PR #2878 (head `66137b2bc279e2001c093b970d45aa32ba461beb`) contains an earlier
library implementation. This fix imports neither proposal and retains the current
sort/group and on-disk contracts. It changes only two local UI callers to honor
the read contract established in r35. No commit, push or PR is created.

The retained independent clean-upstream run remains r29 at ef08c3a; the checked
comparison through 93e98bb changes only the release workflow, with identical
reader/test trees and SDK pin. It is not a new clean-upstream test run.

## Device verification

Use the final X4 Pro image named in build/FLASH-LATEST.md, initially with AA off.
Browse Authors, open an author, search author and series names, then clear the
query. Confirm ordinary reading and saved progress. Existing caches are compatible;
no cache deletion or recordings are needed. Do not damage the card to exercise
these error cases. Peak heap, panel ghosting and physical SD behavior remain
unverified by host tests.
