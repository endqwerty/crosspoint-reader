# SdFat 2.3.1 patches

`patch_sdfat.py` pins and verifies the dependency files before applying this set.
Remove patches when an upstream dependency release supplies equivalent fixes;
review new source bytes before changing the version pin or hashes.

The separate-cache override, failed-fill invalidation, and original patch-hook
implementation are adapted from CrossPoint PR #3685 by Sung-jin Brian Hong
(`serialx`), revision `be6543fd379d14670977b80cf996e3aabc20b623`:
https://github.com/crosspoint-reader/crosspoint-reader/pull/3685

`0003-guard-directory-output-pointer.patch` is a local fix exposed by the real-dependency sanitizer tests:
FatFile::readDirCache supplies a null output pointer because it returns the cache
address through another argument. Advancing that unused pointer is undefined;
the guard leaves it unchanged. This adds no allocation or persisted format change.

The hook additionally passes GIT_OPTIONAL_LOCKS=0 to its temporary patch commands.
Both cache modes and patch application are covered by `test/sdfat_cache`.

`0003-sync-fat-before-data-cache.patch` comes from upstream CrossPoint PR #4150.
With separate caches, it writes both FAT copies before directory data during
sync. An interrupted allocation can leak clusters instead of publishing a
directory entry that points at free clusters. Interrupted frees can still leave
dangling entries, as with the shared cache; this is not a power-loss guarantee.
