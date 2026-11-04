import os
from pathlib import Path
import sys
import tempfile
import unittest

from cetta import Engine, Symbol


class ProcessTests(unittest.TestCase):
    def test_installed_owner_helper_and_child_context(self):
        script = (
            "import os,sys; "
            "sys.stdout.write(os.getcwd()+'|'+os.environ['CETTA_PROCESS_TEST']); "
            "sys.stderr.write('diagnostic'); sys.exit(7)"
        )
        before = os.environ.get("CETTA_PROCESS_TEST")
        with tempfile.TemporaryDirectory(prefix="cetta process λ ") as directory:
            for language in ("petta", "he"):
                with self.subTest(language=language), Engine(language=language) as engine:
                    argv = (sys.executable, "-c", script)
                    engine.import_("proc")
                    options = (
                        (Symbol("cwd"), directory),
                        (Symbol("env"), (("CETTA_PROCESS_TEST", "isolated"),)),
                    )
                    expected = engine.call("proc:run", argv, options).to_python()
                    owned = engine.call(
                        "proc:run", argv,
                        ((Symbol("lifetime"), Symbol("owner")), *options),
                    ).to_python()
                    self.assertEqual(owned, expected)
                    self.assertEqual(owned[0][:5], (
                        Symbol("proc:result"), (Symbol("proc:exited"), 7),
                        Symbol("proc:normal"), directory + "|isolated", "diagnostic",
                    ))
                    self.assertEqual(os.environ.get("CETTA_PROCESS_TEST"), before)
                    self.assertEqual(engine.call("+", 20, 22).to_python(), [42])

    def test_owner_launch_failure_and_timeout_remain_distinct(self):
        owner = (Symbol("lifetime"), Symbol("owner"))
        with tempfile.TemporaryDirectory() as directory:
            for language in ("petta", "he"):
                with self.subTest(language=language), Engine(language=language) as engine:
                    engine.import_("proc")
                    missing = engine.call(
                        "proc:run", (str(Path(directory) / "missing"),), (owner,),
                    ).to_python()[0]
                    self.assertEqual(missing[1][0], Symbol("proc:launch-failed"))
                    self.assertEqual(missing[1][2], Symbol("exec"))
                    timed = engine.call(
                        "proc:run", (sys.executable, "-c", "import time; time.sleep(30)"),
                        (owner, (Symbol("timeout-ms"), 50)),
                    ).to_python()[0]
                    self.assertEqual(timed[1][0], Symbol("proc:signaled"))
                    self.assertEqual(timed[2], Symbol("proc:timed-out"))
                    self.assertEqual(engine.call("+", 20, 22).to_python(), [42])
