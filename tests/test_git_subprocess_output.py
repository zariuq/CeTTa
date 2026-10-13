"""Exercise native Git import with child output larger than its diagnostics."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class GitSubprocessOutputTests(unittest.TestCase):
    def check_import(self, noisy_clone=False, noisy_build=False):
        binary = Path(os.environ.get("CETTA_BIN", ""))
        if not binary.is_file():
            self.skipTest("provide CETTA_BIN")
        git = shutil.which("git")
        if not git:
            self.skipTest("provide Git")
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / "noisy_fixture"
            source.mkdir()
            noise = "head -c 1048576 /dev/zero | tr '\\000' x\n"
            (source / "lib.metta").write_text("(fixture imported)\n")
            (source / "build.sh").write_text(
                "set -e\n" + (noise if noisy_build else "") +
                "printf verified > completed\n")
            for args in (["init", "-q", str(source)],
                         ["-C", str(source), "add", "."],
                         ["-C", str(source), "-c", "user.name=CeTTa Test",
                          "-c", "user.email=cetta@example.invalid", "commit", "-qm", "fixture"]):
                subprocess.run([git, *args], check=True, capture_output=True)
            tools = work / "bin"
            tools.mkdir()
            wrapper = tools / "git"
            wrapper.write_text("#!/bin/sh\nset -e\n" +
                               (noise if noisy_clone else "") +
                               "exec " + git + " \"$@\"\n")
            wrapper.chmod(0o755)
            destination = work / "imports"
            program = work / "probe.metta"
            q = json.dumps
            program.write_text(
                '!(import! &self (library lib_import))\n'
                '!(git-import! ' + q(source.as_uri()) + ' "build.sh" ' +
                q(str(destination)) + ')\n')
            env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ["PATH"])
            result = subprocess.run([str(binary.resolve()), "--lang", "petta", "--quiet", str(program)],
                                    env=env, cwd=work, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertNotIn("Error", result.stdout + result.stderr)
            self.assertEqual((destination / source.name / "completed").read_text(), "verified")

    def test_noisy_clone_finishes_before_wait(self):
        self.check_import(noisy_clone=True)

    def test_noisy_build_finishes_before_wait(self):
        self.check_import(noisy_build=True)


if __name__ == "__main__":
    unittest.main()
