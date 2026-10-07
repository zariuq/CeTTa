#!/usr/bin/env python3
"""Compare native saved substitutions with the checked LP reference corpus."""

import argparse
import json
from pathlib import Path
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument("binary", type=Path)
parser.add_argument("--corpus", type=Path,
                    default=Path(__file__).resolve().parents[1] / "fixtures" / "pattern_model.json")
args = parser.parse_args()
binary = str(args.binary.resolve())
corpus = json.loads(args.corpus.read_text())
assert corpus["schema"] == 1
rows = corpus["cases"]
assert rows and {row["mode"] for row in rows} == {"match%", "%match", "variant"}

checks = 0
for language in ("he", "petta"):
    for start in range(0, len(rows), 40):
        batch = rows[start:start + 40]
        queries = []
        expected = []
        for row in batch:
            solver = f'(pat:solve {row["mode"]} {row["pairs"]})'
            image = row["images"]
            if image is None:
                queries.append(f"!(collapse {solver})")
                expected.append("()")
            else:
                queries.append(f"!(collapse (== (pat:apply {solver} (slots $v0 $v1 $v2)) {image}))")
                expected.append("(True)" if language == "he" else "(true)")
        # Equality must observe variable identity, not merely alpha-equivalence.
        queries.append("!(== (slots $x $y) (slots $y $x))")
        expected.append("False" if language == "he" else "false")
        queries.append("!(== (slots a) (slots b))")
        expected.append("False" if language == "he" else "false")
        source = "!(import! &self pat)\n" + "\n".join(queries)
        result = subprocess.run([binary, "--lang", language, "--profile", "extended", "-e", source],
                                capture_output=True, text=True, timeout=60)
        assert result.returncode == 0 and not result.stderr, (language, start, result)
        lines = result.stdout.splitlines()
        if language == "he":
            assert lines[0] == "[()]", (language, start, lines[:1])
            expected_lines = [f"[{value}]" for value in expected]
        else:
            assert lines[0] == "true", (language, start, lines[:1])
            expected_lines = expected
        observed = lines[1:]
        if observed != expected_lines:
            for offset, (actual, wanted) in enumerate(zip(observed, expected_lines)):
                if actual != wanted:
                    raise AssertionError((language, start + offset, queries[offset], actual, wanted))
            raise AssertionError((language, start, "answer count", observed, expected_lines))
        checks += len(batch) + 2

print(f"PASS: {checks} native/reference witness and variable-identity observations")
