# Unknown Author read contract r35

Based on develop `93e98bb78702e29868a16a13b80c40e6b36ccdff` and SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`, retaining r34 behavior and tests.
The latest fetch resolves to this same develop revision.

## Confirmed failure

LibraryListActivity::buildGroupStarts rejects a failed readAuthor result. The
real index reader returned false for both a valid empty author and an unreadable
field. A library containing a missing author could therefore fail to build its
Authors directory rather than offering the existing Unknown Author group.
The existing UI test missed this: its mock returned true for a valid empty author.

readAuthor now returns the underlying field-read status. Empty is a valid value;
malformed records, closed handles and reported I/O failures still return false.
The public signature and binary layout are unchanged, but the success semantics
for an empty field are intentionally clarified. This matches readSourceAuthor
and the paired metadata readers. All production call sites were inspected:
only grouping branches on this return value, and grouping needs read success
rather than text presence. Title reads retain their false-on-empty convention,
which callers use for filename fallback.

## Test boundary repair

The UI suite now extracts the production readAuthor method instead of duplicating
its return logic. Only the class qualifier is adapted to the UI fixture; its blob
storage remains simulated. Changes to the index source trigger test regeneration.
This makes the existing UnknownAuthorBooksHaveOneReachableGroup test sensitive
to the actual return contract. That existing test and two new empty-field tests
fail on r34 with the repaired test boundary.

Four new real-index tests cover valid empty fields, their distinction from absent
titles, malformed/missing records and every read/short-read/seek failure point
for a nonempty author. Reopen/retry must recover. The empty-author case still
uses one one-byte HAL read; no extra title read is introduced. All existing UI,
index and builder tests remain required, including missing-author navigation.

## Resources and compatibility

The production change removes one emptiness predicate and documents the return
contract. No allocation, class field, cache, background work or format change is
added. Existing grouping buffers and author reads remain unchanged. This fixes
reachability; it does not claim a measured library speedup or device heap gain.
Maintainer sorting and author-normalization policies are untouched.

Independent clean-upstream evidence remains the r29 run at ef08c3a. The retained
and rechecked equivalence record proves only the upstream release workflow changed
through 93e98bb; reader/test trees and SDK pin are identical. This is not represented
as a newly executed clean-upstream run.

## Device verification

Use the single final X4 Pro image in build/FLASH-LATEST.md, initially with AA off.
Open Authors in a library containing books without author metadata, expand Unknown
Author and open one of its books. Check named and initials-only authors, reverse
order and search. Preserve original reading-position caches; no cache migration
or manual recording is needed. Hardware behavior remains unverified.
