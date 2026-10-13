#!/usr/bin/env python3
"""Compare ordered native joins with the checked LP search observations."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument("binary", type=Path)
parser.add_argument("--corpus", type=Path,
                    default=Path(__file__).resolve().parents[1] / "fixtures" / "pattern_search_model.json")
args = parser.parse_args()
binary = str(args.binary.resolve())
corpus = json.loads(args.corpus.read_text())
assert corpus["schema"] == 1
cases = corpus["cases"]
assert {row["mode"] for row in cases} == {"match%", "%match", "variant"}


def alpha(text):
    names = {}
    return re.sub(r"\$[\w-]+(?:#\d+)?",
                  lambda m: names.setdefault(m[0], f"$v{len(names)}"), text)


def observations(output):
    result = []
    offset = 0
    while (start := output.find("(pat:observed ", offset)) >= 0:
        depth = 0
        for end in range(start, len(output)):
            depth += (output[end] == "(") - (output[end] == ")")
            if depth == 0:
                result.append(output[start:end + 1])
                offset = end + 1
                break
        else:
            raise AssertionError(("unterminated observation", output))
    return result


def canonical_native(text, identities):
    def occurrence(match):
        instance, revision, index = map(int, match.groups())
        assert index > 0, ("anchor selected as a term", text)
        identities.add((instance, revision))
        return f"(pat:occurrence 0 0 {index - 1})"
    return alpha(re.sub(r"\(pat:occurrence (\d+) (\d+) (\d+)\)", occurrence, text))


checked = 0
observed_hits = 0
for language in ("he", "petta"):
    for reference in (False, True):
        env = dict(os.environ, CETTA_OPEN_EQUATIONS_REFERENCE=str(int(reference)),
                   CETTA_HE_SHARED_EXECUTION=str(int(not reference)))
        for index, case in enumerate(cases):
            patterns = " ".join(case["patterns"])
            source = "!(import! &self pat)\n"
            if language == "he":
                source += "!(bind! &oracle (new-space))\n"
            # A nonmatching anchor creates an empty PeTTa named space as well.
            # It occupies position zero in both dialects.
            source += "!(add-atom &oracle pat:oracle-anchor)\n"
            source += "".join(f"!(add-atom &oracle {row})\n" for row in case["rows"])
            source += ("!(collapse (let (pat:hit $rows $w) "
                       f"(pat:query &oracle {case['mode']} (, {patterns})) "
                       f"(pat:observed $rows (pat:apply $w (patterns {patterns})))))")
            result = subprocess.run([binary, "--lang", language, "--profile", "extended", "-e", source],
                                    env=env, text=True, capture_output=True, timeout=45)
            assert result.returncode == 0 and not result.stderr and "Error" not in result.stdout, (
                language, reference, index, result)
            identities = set()
            actual = [canonical_native(value, identities) for value in observations(result.stdout)]
            expected = [alpha(value) for value in case["answers"]]
            assert len(identities) <= 1, ("changed space during one query", identities)
            assert actual == expected, (language, reference, index, case, actual, expected)
            checked += 1
            observed_hits += len(actual)

# An occurrence list and its aliases are part of the observation, not noise.
assert alpha("(p $x $x)") != alpha("(p $x $y)")
assert alpha("(p $x $y $x)") != alpha("(p $x $y $y)")
assert ["(row 1 a)", "(row 2 a)"] != ["(row 1 a)"]
assert ["(row 1 a)", "(row 2 b)"] != ["(row 2 b)", "(row 1 a)"]
print(f"PASS: {checked} native/reference joins and {observed_hits} exact ordered hit observations")
