# Bookmark action tests

This target compiles the production reader toggle and bookmark-list delete,
rename, and open methods. CMake extracts their bodies and rejects moved markers.
Bookmark matching also uses the production progress-range helpers.

The storage spy implements the real `BookmarkFile::SaveOptions` interface and
evaluates its predicate on the original entries. Tests observe live cache and UI
state at save time, inject save failure, and compare the proposed persisted list
with the final committed list. They cover failed add/removal/delete/rename,
selection bounds, stale keyboard callbacks, translated error/success feedback,
undisplayed pages, and pending navigation guards. They also distinguish a missing
bookmark file from an unreadable existing file, refuse to replace unknown contents,
and retry a transient read failure before applying the next edit.
Persistence itself is covered by the storage tests;
these fixtures do not simulate SD power loss or physical display updates.
