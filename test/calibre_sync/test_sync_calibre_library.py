"""Exercise the SD sync script using only temporary export/card fixtures."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

try:
    import resource
except ImportError:
    resource = None


SCRIPT = Path(__file__).resolve().parents[2] / "scripts/sync-calibre-library.sh"
UUID = "1731e1ca-38a6-47da-9c5b-6f324ab6a3bf"


@unittest.skipUnless(shutil.which("bash") and shutil.which("rsync") and shutil.which("unzip"),
                     "requires bash, rsync and unzip")
class CalibreSyncTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="crosspoint-calibre-sync-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.export = self.root / "export"
        self.card = self.root / "card"
        self.books = self.card / "Books"
        self.export.mkdir()
        (self.card / ".crosspoint").mkdir(parents=True)
        self.write_epub(self.export / "New.epub")

    @staticmethod
    def write_epub(path, payload=b"book content"):
        path.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED) as book:
            book.writestr("content.opf", '<dc:identifier id="uuid_id">urn:uuid:' + UUID + '</dc:identifier>')
            book.writestr("content.xhtml", payload)

    def sync(self, destination=None, dry_run=False, delete=True, preexec_fn=None):
        command = ["bash", str(SCRIPT)]
        if dry_run:
            command.append("--dry-run")
        if delete:
            command.append("--delete")
        command.extend([str(self.export), str(self.books if destination is None else destination)])
        return subprocess.run(command, text=True, capture_output=True, timeout=30, preexec_fn=preexec_fn)

    def assert_rejected(self, destination, message):
        original = (self.export / "New.epub").read_bytes()
        result = self.sync(destination, dry_run=True)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(message, result.stderr)
        self.assertNotIn("*deleting", result.stdout)
        self.assertEqual((self.export / "New.epub").read_bytes(), original)

    def test_source_and_ancestor_aliases_are_rejected(self):
        for name, target in (("source-link", self.export), ("ancestor-link", self.root)):
            with self.subTest(name=name):
                alias = self.root / name
                alias.symlink_to(target, target_is_directory=True)
                self.assert_rejected(alias, "must not be inside")
        self.assert_rejected(str(self.root) + "/.", "must not be inside")
        self.assert_rejected(str(self.export) + "/.", "must not be inside")

    def test_existing_and_missing_destinations_inside_export_are_rejected(self):
        nested = self.export / "Books"
        self.assert_rejected(nested, "must not be inside")
        nested.mkdir()
        self.assert_rejected(nested, "must not be inside")

    def test_root_export_is_rejected_before_scanning(self):
        result = subprocess.run(
            ["bash", str(SCRIPT), "--dry-run", "--delete", "/", str(self.books)],
            text=True, capture_output=True, timeout=5,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("must not be inside the export folder", result.stderr)
        self.assertNotIn("Export contains", result.stdout)
        self.assertFalse(self.books.exists())

    def test_alias_into_cache_and_card_root_are_rejected(self):
        cache = self.card / ".crosspoint" / "cache"
        cache.mkdir()
        alias = self.root / "cache-link"
        alias.symlink_to(cache, target_is_directory=True)
        self.assert_rejected(alias, "device cache")
        card_alias = self.root / "card-link"
        card_alias.symlink_to(self.card, target_is_directory=True)
        self.assert_rejected(card_alias, "SD card root")
        self.assert_rejected("/", "filesystem root")

    def test_nondirectory_and_dangling_symlink_are_rejected(self):
        regular = self.root / "not-directory"
        regular.write_bytes(b"preserve")
        self.assert_rejected(regular, "not a directory")
        dangling = self.root / "dangling"
        dangling.symlink_to(self.root / "missing", target_is_directory=True)
        self.assert_rejected(dangling, "not a directory")
        self.assertFalse((self.root / "missing").exists())

    def test_missing_books_folder_dry_run_does_not_create_it(self):
        result = self.sync(dry_run=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.books.exists())

    def test_missing_books_folder_is_created_for_copy(self):
        result = self.sync()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.books / "New.epub").read_bytes(), (self.export / "New.epub").read_bytes())

    def test_valid_destination_alias_copies_to_its_physical_folder(self):
        self.books.mkdir()
        alias = self.root / "books-link"
        alias.symlink_to(self.books, target_is_directory=True)
        result = self.sync(alias)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.books / "New.epub").read_bytes(), (self.export / "New.epub").read_bytes())

    def test_success_transfers_before_deletion_and_preserves_excluded_files(self):
        self.write_epub(self.books / "Old.epub")
        (self.books / "notes.txt").write_bytes(b"keep notes")
        cache = self.card / ".crosspoint" / "state.bin"
        cache.write_bytes(b"keep state")
        result = self.sync()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.books / "New.epub").read_bytes(), (self.export / "New.epub").read_bytes())
        self.assertFalse((self.books / "Old.epub").exists())
        lines = result.stdout.splitlines()
        transfer = next(i for i, line in enumerate(lines) if line.startswith(">f") and "New.epub" in line)
        deletion = next(i for i, line in enumerate(lines) if line.startswith("*deleting") and "Old.epub" in line)
        self.assertLess(transfer, deletion, result.stdout)
        self.assertIn("Library refresh can relink", result.stdout)
        self.assertEqual((self.books / "notes.txt").read_bytes(), b"keep notes")
        self.assertEqual(cache.read_bytes(), b"keep state")

    def test_destination_directory_conflict_keeps_old_renamed_book(self):
        self.write_epub(self.books / "Old.epub")
        original = (self.books / "Old.epub").read_bytes()
        conflict = self.books / "New.epub"
        conflict.mkdir()
        (conflict / "notes.txt").write_bytes(b"keep notes")
        result = self.sync()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertEqual((self.books / "Old.epub").read_bytes(), original)
        self.assertEqual((conflict / "notes.txt").read_bytes(), b"keep notes")
        self.assertNotIn("*deleting Old.epub", result.stdout)
        self.assertNotIn("Done.", result.stdout)

    @unittest.skipUnless(resource is not None and hasattr(resource, "RLIMIT_FSIZE"),
                         "requires a receiver file-size limit")
    def test_receiver_write_failure_keeps_old_renamed_book(self):
        self.write_epub(self.export / "New.epub", os.urandom(16 * 1024))
        self.write_epub(self.books / "Old.epub")
        original = (self.books / "Old.epub").read_bytes()

        def limit_receiver_files():
            resource.setrlimit(resource.RLIMIT_FSIZE, (1024, 1024))

        result = self.sync(preexec_fn=limit_receiver_files)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("New.epub", result.stdout + result.stderr)
        self.assertEqual((self.books / "Old.epub").read_bytes(), original)
        self.assertFalse((self.books / "New.epub").exists())
        self.assertNotIn("*deleting", result.stdout)
        self.assertNotIn("Done.", result.stdout)


if __name__ == "__main__":
    unittest.main()
