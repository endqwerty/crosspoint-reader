#!/usr/bin/env bash
# Copy a Calibre "Save to disk" export onto the reader's SD card.
#
# Usage: scripts/sync-calibre-library.sh [-n] [--delete] EXPORT_DIR SD_BOOKS_DIR
#   -n, --dry-run  List what would change without writing to the card.
#   --delete       Also remove EPUBs from SD_BOOKS_DIR that are not in EXPORT_DIR.
#
# Example:
#   scripts/sync-calibre-library.sh -n --delete ~/CalibreExport /Volumes/XTEINK/Books
#
# SD_BOOKS_DIR must be a dedicated folder (e.g. /Books), never the card root,
# so --delete cannot touch /.crosspoint or other device data. Only *.epub files
# are copied; Calibre's metadata.opf/cover.jpg sidecars and hidden files are
# skipped.
#
# Files are compared by content (--checksum), not mtime, because every Calibre
# export rewrites all files. Source mtimes are deliberately not copied: an
# unchanged book keeps its SD mtime, so the Library index reuses its metadata,
# while a changed book gets a fresh mtime and is re-parsed.
#
# Reading progress, bookmarks and reading state are keyed by SD path. Keep the
# Calibre save template stable; a book whose folder or filename changes is
# treated as a new book. With --delete, keep "Move finished books to /Read" off,
# otherwise moved books are copied back into SD_BOOKS_DIR on the next sync.

set -euo pipefail

usage() {
  sed -n '4,6p' "$0" | sed 's/^# \{0,1\}//' >&2
  exit 2
}

dry_run=0
delete=0
while [ $# -gt 0 ]; do
  case "$1" in
    -n | --dry-run) dry_run=1 ;;
    --delete) delete=1 ;;
    -h | --help) usage ;;
    -*) echo "Unknown option: $1" >&2; usage ;;
    *) break ;;
  esac
  shift
done
[ $# -eq 2 ] || usage

src="${1%/}"
dest="${2%/}"

[ -d "$src" ] || { echo "Export folder not found: $src" >&2; exit 1; }
dest_parent="$(dirname "$dest")"
[ -d "$dest_parent" ] || { echo "SD card folder not found: $dest_parent (is the card mounted?)" >&2; exit 1; }

src_abs="$(cd "$src" && pwd -P)"
dest_parent_abs="$(cd "$dest_parent" && pwd -P)"
dest_abs="$dest_parent_abs/$(basename "$dest")"

case "$dest_abs/" in
  "$src_abs"/*) echo "SD folder must not be inside the export folder." >&2; exit 1 ;;
esac
case "$src_abs/" in
  "$dest_abs"/*) echo "Export folder must not be inside the SD folder." >&2; exit 1 ;;
esac
case "$dest_abs/" in
  */.crosspoint/*) echo "Refusing to sync into the device cache (.crosspoint)." >&2; exit 1 ;;
esac
if [ -d "$dest_abs/.crosspoint" ]; then
  echo "$dest_abs looks like the SD card root; use a dedicated books folder such as $dest_abs/Books." >&2
  exit 1
fi

# The device path is everything below the card root. The firmware skips books
# whose filename or folder path exceeds 255 bytes (lib/LibraryIndex/LibraryBuilder.cpp).
card_root="$dest_abs"
while [ "$card_root" != "/" ] && [ ! -d "$card_root/.crosspoint" ]; do
  card_root="$(dirname "$card_root")"
done
if [ "$card_root" = "/" ]; then
  echo "Note: no .crosspoint folder found above $dest_abs; assuming $dest_abs is directly below the card root."
  card_root="$dest_parent_abs"
fi
device_prefix="${dest_abs#"$card_root"}"

count=0
too_long=0
while IFS= read -r -d '' file; do
  count=$((count + 1))
  rel="${file#"$src_abs"/}"
  name="$(basename "$rel")"
  case "$rel" in
    */*) folder="$device_prefix/$(dirname "$rel")" ;;
    *) folder="$device_prefix" ;;
  esac
  name_bytes=$(printf '%s' "$name" | wc -c)
  folder_bytes=$(printf '%s' "$folder" | wc -c)
  if [ $((name_bytes)) -gt 255 ] || [ $((folder_bytes)) -gt 255 ]; then
    echo "Warning: path too long for the device Library, will be skipped: $rel" >&2
    too_long=$((too_long + 1))
  fi
done < <(find "$src_abs" \( -name '.*' -prune \) -o -type f \( -name '*.epub' -o -name '*.EPUB' \) -print0)

echo "Export contains $count EPUB(s)."
[ "$too_long" -eq 0 ] || echo "$too_long book(s) exceed the device path limit; shorten the Calibre save template." >&2
if [ "$count" -eq 0 ]; then
  echo "No EPUBs found in $src_abs; nothing to do." >&2
  exit 1
fi

args=(-r --checksum --prune-empty-dirs --itemize-changes
  --exclude='.*'
  --include='*/' --include='*.epub' --include='*.EPUB'
  --exclude='*')
[ "$dry_run" -eq 1 ] && args+=(--dry-run)
[ "$delete" -eq 1 ] && args+=(--delete)

[ "$dry_run" -eq 1 ] || mkdir -p "$dest_abs"
rsync "${args[@]}" "$src_abs/" "$dest_abs/"

if [ "$dry_run" -eq 1 ]; then
  echo "Dry run only; nothing was written."
  exit 0
fi

# Finder and other macOS tools leave AppleDouble files on FAT/exFAT cards.
find "$dest_abs" -name '._*' -type f -delete

echo "Done. Eject the card, insert it into the reader, power on and open Library."
