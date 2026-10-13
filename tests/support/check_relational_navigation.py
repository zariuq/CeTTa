#!/usr/bin/env python3
"""Compare native navigation with the checked model and independent support."""
import json
from pathlib import Path
import subprocess
import sys


def check(binary: Path) -> None:
    reference = Path(__file__).with_name("ordered_intersection_reference.json")
    document = json.loads(reference.read_text())
    assert document["schemaVersion"] == 1
    assert document["observation"] == "strict-ordered-key-support"
    expected = document["cases"]
    assert len(expected) == 4096
    identities = {(row["left"], row["right"]) for row in expected}
    assert identities == {(left, right) for left in range(64) for right in range(64)}
    for row in expected:
        left = {key for key in range(6) if row["left"] & (1 << key)}
        right = {key for key in range(6) if row["right"] & (1 << key)}
        assert row["keys"] == sorted(left & right)
        assert row["advances"] <= 2 * min(len(left), len(right))
    run = subprocess.run([str(binary.resolve()), "--json"], text=True,
                         capture_output=True, check=True, timeout=60)
    actual = [json.loads(line) for line in run.stdout.splitlines()]
    assert actual == expected, "native progress trace differs from the ordered intersection model"
    print("PASS: 4096 native navigation cases agree with the checked model and independent ordered support")


if __name__ == "__main__":
    check(Path(sys.argv[1]))
