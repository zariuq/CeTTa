"""Standalone fs library qualification in both public spellings."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FsToolsLibraryTests(unittest.TestCase):
    def test_streaming_pages_and_atomic_updates(self):
        binary = Path(os.environ.get("CETTA_BIN", ROOT / "cetta"))
        for language in ("he", "petta"):
            with self.subTest(language=language), tempfile.TemporaryDirectory() as directory:
                work = Path(directory)
                target = work / "text"; target.write_bytes("one\r\nλ two\rthree\n".encode()); target.chmod(0o640)
                quote = lambda value: json.dumps(str(value), ensure_ascii=False)
                program = work / "probe.metta"
                program.write_text('!(import! &self fs)\n' +
                    f'!(fs:line-window {quote(target)} 2 1)\n' +
                    f'!(fs:tree-page {quote(work)} 2 1 0 ())\n' +
                    f'!(fs:grep-page "two" {quote(target)} 10 0 ())\n' +
                    f'!(fs:update-text {quote(target)} "edit" "two" "TWO" {quote(work / "backups")})\n' +
                    f'!(fs:is-file {quote(target)})\n', encoding="utf-8")
                result = subprocess.run([str(binary), "--lang", language, str(program)], cwd=ROOT,
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn("Error", result.stdout)
                self.assertIn('"window" 2 1 True', result.stdout)
                self.assertIn('"matches"', result.stdout)
                self.assertIn('"written"', result.stdout)
                self.assertEqual(target.read_bytes(), "one\nλ TWO\nthree\n".encode())
                self.assertEqual(target.stat().st_mode & 0o777, 0o640)
                [backup] = (work / "backups").iterdir()
                self.assertEqual(backup.read_bytes(), "one\r\nλ two\rthree\n".encode())
