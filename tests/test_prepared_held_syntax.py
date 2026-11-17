"""Check captured Atom arguments under the existing prepared worker routes."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def run(binary, source, profile, flags, stats=False):
    env = os.environ.copy()
    for key in ("CETTA_PREPARED_PURE_REFERENCE", "CETTA_OPEN_EQUATIONS_REFERENCE",
                "CETTA_PREPARED_PURE_CODE_REFERENCE", "CETTA_GC_BUDGET_MB"):
        env.pop(key, None)
    env.update(flags)
    command = [str(binary), "--lang", "petta"]
    if profile:
        command += ["--profile", profile]
    if stats:
        command.append("--emit-runtime-stats")
    result = subprocess.run(command + [str(source)], env=env,
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, (source, profile, flags, result.stderr)
    counters = {}
    for line in result.stderr.splitlines():
        fields = line.split()
        assert stats and len(fields) == 3 and fields[0] == "runtime-counter", line
        counters[fields[1]] = int(fields[2])
    return result.stdout, counters


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--stats", action="store_true")
    args = parser.parse_args()
    binary = args.binary.resolve()
    checks = 0
    routes = [
        ("registers", {}),
        ("nodes", {"CETTA_PREPARED_PURE_CODE_REFERENCE": "1"}),
        ("canonical", {"CETTA_PREPARED_PURE_REFERENCE": "1",
                       "CETTA_OPEN_EQUATIONS_REFERENCE": "1"}),
        ("collecting", {"CETTA_GC": "1", "CETTA_GC_BUDGET_MB": "1"}),
    ]
    for name in ("prepared_held_syntax", "prepared_held_recursion"):
        source = Path("tests/petta") / (name + ".metta")
        expected = source.with_suffix(".expected").read_text().rstrip()
        for profile in (None, "extended"):
            for route, flags in routes:
                actual, counters = run(binary, source, profile, flags, args.stats)
                assert actual.rstrip() == expected, (name, profile, route, expected, actual)
                if args.stats and name == "prepared_held_recursion":
                    commits = counters["prepared-pure-call-commit"]
                    if route == "canonical":
                        assert commits == 0, (profile, route, counters)
                    else:
                        assert commits > 0, (profile, route, "worker did not commit")
                        assert counters["prepared-pure-call-decline"] == 0, (profile, route)
                checks += 1
    if args.stats:
        # A genuine reader stays canonical. Its source refusal must be
        # shared by repeated calls in the same evaluation episode.
        attempts = []
        with tempfile.TemporaryDirectory(prefix="held-reader-", dir="runtime") as tmp:
            source = Path(tmp) / "reader.metta"
            for count in (4, 400):
                source.write_text("""(held-reader-row)
(= (held-reader $value) (match &self (held-reader-row) $value))
(= (held-repeat $n $total)
   (if (== $n 0) $total
       (held-repeat (- $n 1) (+ $total (held-reader 1)))))
""" + f"!(held-repeat {count} 0)\n")
                actual, counters = run(binary, source, None, {}, True)
                assert actual.strip() == str(count), actual
                attempts.append(counters["prepared-pure-call-compile-attempt"])
                assert counters["prepared-pure-call-negative-cache-hit"] > 0
                checks += 1
        assert attempts[0] > 0 and attempts[0] == attempts[1], attempts
    print(f"PASS: held source preserves data, scope, effects and alternatives ({checks} cases)")


if __name__ == "__main__":
    main()
