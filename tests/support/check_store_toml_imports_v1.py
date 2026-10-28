#!/usr/bin/env python3
"""Native store and TOML dispatch stay independent in either import order."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    binary = parser.parse_args().binary.resolve()
    store = '''!(println! (case (store:key-i64 -7)
      (((StoreKeyBytes $bytes) (store:key->i64 $bytes)))))'''
    toml = r'''!(println! (== (toml:to-json "value=7") "{\"value\":7}"))'''
    checks = 0
    with tempfile.TemporaryDirectory(prefix="store-toml-imports-") as directory:
        source = Path(directory) / "case.metta"
        for language in ("petta", "he", "prime"):
            def imports(names):
                return "\n".join("!(import! &self %s)" %
                                 ("(library %s)" % name if language == "petta" else name)
                                 for name in names)

            def run(names, body, expected):
                nonlocal checks
                source.write_text(imports(names) + "\n" + body + "\n")
                result = subprocess.run([str(binary), "--lang", language, "--no-results", str(source)],
                                        capture_output=True, text=True, timeout=20)
                lines = result.stdout.splitlines()
                lines = ["true" if line == "True" else "false" if line == "False" else line for line in lines]
                if result.returncode or lines != expected:
                    raise AssertionError((language, names, result.returncode, lines, expected, result.stderr))
                checks += 1

            run(("store",), store + '\n!(println! (__cetta_lib_toml to-json ("value=7")))',
                ['(StoreKeyInteger -7)', '(__cetta_lib_toml to-json ("value=7"))'])
            run(("toml",), toml + '\n!(println! (__cetta_lib_store_key_i64 -7))',
                ['true', '(__cetta_lib_store_key_i64 -7)'])
            for order in (("store", "toml"), ("toml", "store")):
                run(order, store + "\n" + toml, ['(StoreKeyInteger -7)', 'true'])
    print("store/TOML import isolation: %d cases, 0 failures" % checks)


if __name__ == "__main__":
    main()
