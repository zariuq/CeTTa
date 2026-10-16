from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import parser_artifact_publication_v1 as publication


class ArtifactPublicationTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="publication-test-", dir=ROOT / "runtime")
        self.addCleanup(temporary.cleanup)
        self.output = Path(temporary.name)

    def test_identical_publication_preserves_file_identity(self):
        path = self.output / "catalog.h"
        publication.publish_file(path, b"prepared")
        before = path.stat()
        with mock.patch.object(publication.os, "replace", side_effect=AssertionError("unneeded publication")):
            publication.publish_file(path, b"prepared")
        self.assertEqual(path.stat().st_ino, before.st_ino)
        self.assertEqual(path.stat().st_mtime_ns, before.st_mtime_ns)

    def test_preparation_failure_does_not_publish_any_file(self):
        first = {"first.h": b"old first", "second.h": b"old second"}
        publication.publish_files(self.output, first)
        with mock.patch.object(publication.os, "fsync", side_effect=OSError("interrupted preparation")):
            with self.assertRaises(OSError):
                publication.publish_files(self.output, {name: b"new" for name in first})
        self.assertEqual({name: (self.output / name).read_bytes() for name in first}, first)
        self.assertEqual(list(self.output.glob(".publish-*")), [])

    def test_single_catalog_commit_failure_retains_old_catalog(self):
        path = self.output / "catalog.h"
        publication.publish_file(path, b"old catalog")
        with mock.patch.object(publication.os, "replace", side_effect=OSError("interrupted commit")):
            with self.assertRaises(OSError):
                publication.publish_file(path, b"new catalog")
        self.assertEqual(path.read_bytes(), b"old catalog")

    def test_existing_generation_must_match(self):
        staged, generation = self.output / "staged", self.output / "generation"
        staged.mkdir()
        (staged / "parser.tpp").write_bytes(b"parser program")
        publication.publish_generation(staged, generation, {"parser.tpp": b"parser program"})
        publication.publish_generation(staged, generation, {"parser.tpp": b"parser program"})
        with self.assertRaisesRegex(ValueError, "content identity"):
            publication.publish_generation(staged, generation, {"parser.tpp": b"another program"})
        self.assertEqual((generation / "parser.tpp").read_bytes(), b"parser program")

    def test_bad_names_and_content_do_not_change_published_files(self):
        for files in ({"../outside": b"bad"}, {"nested/name": b"bad"}, {"valid": "not bytes"}):
            with self.subTest(files=files):
                with self.assertRaises(ValueError):
                    publication.publish_files(self.output, files)
        self.assertEqual(list(self.output.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
