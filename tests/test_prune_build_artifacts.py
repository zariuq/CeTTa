"""Destructive pruning protects source, evidence, active work and hard links."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'scripts/prune_build_artifacts.py'
SPEC = importlib.util.spec_from_file_location('prune_build_artifacts', SOURCE)
prune = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = prune
SPEC.loader.exec_module(prune)


class Pruning(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        (self.root / 'runtime').mkdir()
        self.binary = Path(shutil.which('true'))

    def artifact(self, name):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(self.binary, path)
        os.utime(path, (1, 1))
        return path

    def select(self, **kwargs):
        return prune.candidates(self.root, 0, kwargs.get('keep', set()),
                                kwargs.get('tags', []), kwargs.get('legacy', False),
                                kwargs.get('held', set()))

    def test_only_generated_products_are_selected(self):
        self.artifact('runtime/old-test')
        pinned = self.artifact('runtime/baseline')
        tracked = self.artifact('runtime/tracked-fixture')
        subprocess.run(['git', '-C', str(self.root), 'add', str(tracked)], check=True)
        (self.root / 'runtime/receipt.json').write_text('{"verified": true}')
        (self.root / 'runtime/source.c').write_text('int main(void) { return 0; }')
        # A program's captured output may contain arbitrary bytes. Its
        # evidence role takes precedence over an executable-looking prefix.
        self.artifact('runtime/captured.stdout')
        self.artifact('runtime/test-current-config')
        live = self.artifact('runtime/running-test')
        info = live.stat()
        actual = self.select(keep={str(pinned.relative_to(self.root))}, tags=['current-config'],
                             held={(info.st_dev, info.st_ino)})
        self.assertEqual([item.relative for item in actual], ['runtime/old-test'])

    def test_legacy_is_explicit_and_preserves_lean_source(self):
        self.artifact('.codex-work/attempt/output')
        source = self.root / '.codex-work/theorem.lean'
        source.write_text('theorem example_identity (n : Nat) : n = n := rfl')
        source.with_suffix('.olean').write_bytes(b'generated module')
        self.assertEqual(self.select(), [])
        selected = self.select(legacy=True)
        self.assertEqual({item.relative for item in selected},
                         {'.codex-work/attempt/output', '.codex-work/theorem.olean'})
        result = prune.prune(self.root, selected, self.root / 'manifest.jsonl', True, set())
        self.assertEqual(result['removed'], 2)
        self.assertTrue(source.exists())

    def test_symlink_and_nested_repository_are_never_walked(self):
        outside = self.root / 'outside'
        outside.mkdir()
        self.artifact('outside/test')
        (self.root / 'runtime/escape').symlink_to(outside, target_is_directory=True)
        (self.root / 'runtime/alias').symlink_to(outside / 'test')
        nested = self.root / 'runtime/nested'
        subprocess.run(['git', 'init', '-q', str(nested)], check=True)
        self.artifact('runtime/nested/build')
        self.assertEqual(self.select(), [])

    def test_dry_run_and_changed_file_are_preserved(self):
        path = self.artifact('runtime/old')
        selected = self.select()
        result = prune.prune(self.root, selected, self.root / 'preview.jsonl', False, set())
        self.assertEqual(result['candidates'], 1)
        self.assertTrue(path.exists())
        path.write_bytes(b'new work')
        result = prune.prune(self.root, selected, self.root / 'apply.jsonl', True, set())
        self.assertEqual(result['removed'], 0)
        self.assertEqual(result['changed_or_busy'], 1)
        self.assertEqual(path.read_bytes(), b'new work')

    def test_hard_link_accounting_and_manifest_non_overwrite(self):
        path = self.artifact('runtime/old')
        kept = self.root / 'runtime/kept'
        os.link(path, kept)
        selected = self.select(keep={'runtime/kept'})
        manifest = self.root / 'removed.jsonl'
        result = prune.prune(self.root, selected, manifest, True, set())
        self.assertEqual(result['removed'], 1)
        self.assertEqual(result['released_file_blocks'], 0)
        self.assertTrue(kept.exists())
        with self.assertRaises(FileExistsError):
            prune.prune(self.root, self.select(), manifest, True, set())
        self.assertTrue(kept.exists())

    def test_active_build_is_refused(self):
        if not Path('/proc').is_dir() or not shutil.which('make'):
            self.skipTest('process build inspection requires procfs and make')
        (self.root / 'Makefile').write_text('hold:\n\t@sleep 10\n')
        process = subprocess.Popen(['make', '-s', 'hold'], cwd=self.root,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(.1)
            with self.assertRaisesRegex(RuntimeError, 'active build'):
                prune.active_files(self.root)
        finally:
            process.terminate()
            process.wait(timeout=3)


if __name__ == '__main__':
    unittest.main()
