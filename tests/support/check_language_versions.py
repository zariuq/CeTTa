#!/usr/bin/env python3
"""Check version selection without changing dialect or profile behavior."""

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    binary = Path(sys.argv[1]).resolve()
    checks = 0

    def run(arguments, expression=None):
        command = [str(binary), *arguments]
        if expression is not None:
            command += ["-e", expression]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=60)
        return result.returncode, result.stdout, result.stderr

    # Unversioned spelling remains the default; compatibility is a distinct
    # contract, not a change to ordinary HE's profile or import policy.
    pairs = [
        ([], ["--lang", "he@0.0.1"]),
        (["--lang", "he"], ["--lang", "mettahe@0.0.1"]),
        (["--lang", "he", "--profile", "he"],
         ["--lang", "he@0.0.1", "--profile", "he"]),
        (["--lang", "he", "--profile", "extended"],
         ["--lang", "he@0.0.1", "--profile", "extended"]),
        (["--lang", "he", "--profile", "he-prime"],
         ["--lang", "he@0.0.1", "--profile", "he-prime"]),
        (["--lang", "he", "--profile", "he-compat"],
         ["--lang", "he@0.2.10"]),
        (["--lang", "prime"], ["--lang", "prime@0.0.1"]),
        (["--lang", "prime"], ["--lang", "metta-prime@0.0.1"]),
        (["--lang", "petta"], ["--lang", "petta@1.0.6"]),
        (["--lang", "petta", "--profile", "extended"],
         ["--lang", "petta@1.0.6", "--profile", "extended"]),
        (["--lang", "petta", "--profile", "contract"], ["--lang", "petta@1.1.c"]),
    ]
    for ordinary, versioned in pairs:
        for expression in ("!(+ 1 2)", "!(let $value (+ 1 2) (+ $value 4))"):
            expected = run(ordinary, expression)
            actual = run(versioned, expression)
            assert expected[0] == 0 and actual == expected, (ordinary, versioned, expected, actual)
            checks += 1

    for endpoint, profile, reason in (
        ("he@9", None, b"unknown source version"),
        ("prime@9", None, b"unknown source version"),
        ("petta@9", None, b"unknown source version"),
        ("petta@1.1.c", "extended", b"is not a profile"),
        ("petta@1.0.6", "contract", b"is not a profile"),
        ("he@0.0.1", "he-compat", b"is not a profile"),
        ("he@0.2.10", "extended", b"is not a profile"),
        ("prime@0.0.1", "extended", b"unknown source profile 'extended' for language 'prime'"),
        ("he@", None, b"is not language@version"),
        ("@0.0.1", None, b"is not language@version"),
    ):
        arguments = ["--lang", endpoint]
        if profile:
            arguments += ["--profile", profile]
        actual = run(arguments, "!(+ 1 2)")
        assert actual[0] == 2 and reason in actual[2] and not actual[1], (arguments, actual)
        checks += 1

    expression = "!(+ 1 2)"
    ordinary = run(["--translate", "--lang", "he", "--lang", "petta"], expression)
    versioned = run(["--translate", "--lang", "he@0.0.1", "--lang", "petta@1.0.6"], expression)
    # This build translates only rhocalc. HE/PeTTa endpoint selection must
    # still reach that same boundary; versioning does not add a translator.
    assert ordinary[0] == 2 and b"supports only rhocalc endpoints" in ordinary[2]
    assert versioned == ordinary, (ordinary, versioned)
    checks += 1
    refused = run(["--translate", "--lang", "he@0.0.1", "--lang", "petta@9"], expression)
    assert refused[0] == 2 and b"unknown target version" in refused[2], refused
    checks += 1

    version = run(["--version"])
    expected = ("cetta " + (ROOT / "VERSION").read_text().strip() + " (").encode()
    assert version[0] == 0 and version[1].startswith(expected), version
    assert run(["-v"]) == version
    checks += 2
    print(f"(LanguageVersionSummary {checks} 0)")


if __name__ == "__main__":
    main()
