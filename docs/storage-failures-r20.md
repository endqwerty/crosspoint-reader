# Storage failure handling (EPUB r20)

Base: CrossPoint `develop` `1d61100f90d2e7e32965c14301320d9989a7ab71`, SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`. Upstream was checked before this
change. The local reader scheduling, Library and EPUB features from r19 remain.
No cache, settings, bookmark or reading-position format changes are introduced.

## Upstream alignment

This adapts [PR #3419](https://github.com/crosspoint-reader/crosspoint-reader/pull/3419)
at `f2faf88b77b0a3e4b5154b5090b1f6a059347d5c`. It is an open proposal, not an
accepted upstream decision. The change stays within the existing HalStorage
ownership and recursive storage-lock boundary. If upstream chooses a different
API, migrate the callers and preserve the failure tests.

Original contribution: Daviex (`david.iuffri94@hotmail.it`), verified against the
PR commit records. Any future commit incorporating this work must include:

`Co-Authored-By: Daviex <david.iuffri94@hotmail.it>`

## Behavior and deliberate adaptations

- Replace the four throwing file-wrapper allocations with checked
  `makeUniqueNoThrow`. Missing read opens and ordinary end-of-directory return
  an empty handle without allocating a wrapper. Empty/moved-from handles can be
  closed safely, as in the upstream proposal.
- Reserve the wrapper **before** a writable/create/truncate/append open. Wrapper
  OOM therefore cannot itself truncate or create the destination. This differs
  from the proposal's open-first write path; failed write opens can allocate and
  then free one wrapper. Other filesystem failures are not transactional writes.
- Add `HalFile::hasError()` for SdFat-reported errors and a latched failure to
  allocate a directory-entry wrapper. The latch moves with the handle and is
  retained across rewind; reopening starts a new scan. Check it before closing.
- Library rebuilding rejects an interrupted directory scan and keeps the
  committed index. FolderSearch labels retained results incomplete. FileBrowser
  discards an incomplete ordinary folder listing and marks the existing search
  row incomplete; a reload retries. The firmware picker also discards incomplete
  lists, without adding an unrelated UI string.

The wrapper is already heap-owned because HalFile hides SDK types and transfers
ownership across scopes. No new allocation is introduced; one boolean is added
per wrapper (alignment may add padding). Temporary file destruction remains
inside the recursive storage lock. This makes wrapper OOM recoverable; it is not
proof that every SDK or standard-library allocation is recoverable.

`hasError()` exposes the errors SdFat reports. It does not detect every corrupt
FAT entry or all media failures: some SdFat openNext failure paths do not set a
parent error. Other existing iterator clients have not all been converted to
inspect this API. This release specifically protects Library publication,
FolderSearch and FileBrowser listings.

## Validation

The final package's `FLASH.md` and `verification/` contain exact results. The
native tests compile production HalStorage against fixed-size SDK/mutex stubs
and use the real Memory helper with exceptions disabled. They inject wrapper
OOM, verify ownership and serialized cleanup, distinguish ordinary directory
end from reported errors, and check write-open side effects and retries.
Builder tests inject each directory read failure across root and nested scans,
check the previous index byte-for-byte and staged-file cleanup, then retry.
Browser/search tests cover failed count/fill passes and incomplete results.
All previous r19 test names must remain in native and sanitizer registries.

These are host tests, not physical SD latency, panel, power-loss or peak-heap
measurements. To verify on X4 Pro, use anti-aliasing off initially; refresh the
Library, browse large/nested folders, search and reopen books, then read across
chapter boundaries and sleep/wake. Confirm Authors/Series drilldown and saved
positions remain correct. No cache clearing is needed for this change.
