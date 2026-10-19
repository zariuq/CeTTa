#!/usr/bin/env python3
"""Check the native Unicode provider independently against pinned source data.

This checker does not import the generator. General categories come from
UnicodeData rather than DerivedGeneralCategory, enumerations use a code-point
array, and simple folding uses graph traversal rather than union-find.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
LIMIT = 0x110000
UNIVERSE = (1 << LIMIT) - 1


def normalized_alias(text: str) -> str:
    return text.translate(str.maketrans("", "", "_- \t\r\n\v\f")).lower()


def fields(path: Path, defaults: bool = False) -> list[list[str]]:
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if defaults:
            if "@missing:" not in line:
                continue
            line = line.partition("@missing:")[2]
        line = line.partition("#")[0].strip()
        if line:
            rows.append([part.strip() for part in line.split(";")])
    return rows


def bounds(text: str) -> tuple[int, int]:
    first, separator, last = text.partition("..")
    return int(first, 16), int(last if separator else first, 16)


def interval(first: int, last: int) -> int:
    assert 0 <= first <= last < LIMIT
    return ((1 << (last - first + 1)) - 1) << first


def array_sets(assignments: bytearray, names: list[str]) -> dict[str, int]:
    result = {name: 0 for name in names}
    start = 0
    while start < LIMIT:
        value = assignments[start]
        end = start + 1
        while end < LIMIT and assignments[end] == value:
            end += 1
        result[names[value]] |= interval(start, end - 1)
        start = end
    return result


def expected_properties(ucd: Path) -> tuple[dict, dict]:
    aliases = {}
    for row in fields(ucd / "PropertyAliases.txt"):
        for name in row:
            aliases[normalized_alias(name)] = row[0]
    value_aliases = {}
    for row in fields(ucd / "PropertyValueAliases.txt"):
        for name in row[1:]:
            value_aliases[row[0], normalized_alias(name)] = row[1]

    def canonical(p: str, value: str) -> str:
        return value_aliases.get((p, normalized_alias(value)), value)

    result = {}
    category_names = sorted({row[2] for row in fields(ucd / "UnicodeData.txt")} | {"Cn"})
    category_ids = {name: i for i, name in enumerate(category_names)}
    categories = bytearray([category_ids["Cn"]]) * LIMIT
    decimal = {}
    first = None
    for row in fields(ucd / "UnicodeData.txt"):
        cp = int(row[0], 16)
        if row[1].endswith(", First>"):
            assert first is None
            first = (cp, row[2])
        elif row[1].endswith(", Last>"):
            assert first is not None and first[1] == row[2]
            low, _ = first
            categories[low:cp + 1] = bytes([category_ids[row[2]]]) * (cp - low + 1)
            first = None
        else:
            assert first is None
            categories[cp] = category_ids[row[2]]
        if row[6]:
            decimal[cp] = int(row[6])
    assert first is None
    for value, members in array_sets(categories, category_names).items():
        result["gc", value] = members
    for major in "LMNPSZC":
        members = 0
        for value in category_names:
            if value.startswith(major):
                members |= result["gc", value]
        result["gc", major] = members
    result["gc", "LC"] = result["gc", "Lu"] | result["gc", "Ll"] | result["gc", "Lt"]

    def enumeration(p: str, file_name: str, initial: str) -> None:
        path = ucd / file_name
        rows, missing = fields(path), fields(path, True)
        names = sorted({canonical(p, row[1]) for row in rows} |
                       {canonical(p, row[-1]) for row in missing} | {initial})
        ids = {name: i for i, name in enumerate(names)}
        assert len(names) < 256
        assignments = bytearray([ids[initial]]) * LIMIT
        # Defaults assign an initial map; explicit records overwrite it.
        for row in missing:
            low, high = bounds(row[0])
            assignments[low:high + 1] = bytes([ids[canonical(p, row[-1])]]) * (high - low + 1)
        for row in rows:
            low, high = bounds(row[0])
            assignments[low:high + 1] = bytes([ids[canonical(p, row[1])]]) * (high - low + 1)
        for value, members in array_sets(assignments, names).items():
            result[p, value] = members

    for p, path, initial in (
        ("sc", "Scripts.txt", "Zzzz"), ("age", "DerivedAge.txt", "NA"),
        ("GCB", "auxiliary/GraphemeBreakProperty.txt", "XX"),
        ("WB", "auxiliary/WordBreakProperty.txt", "XX"),
        ("SB", "auxiliary/SentenceBreakProperty.txt", "XX"),
    ):
        enumeration(p, path, initial)

    binary_names = set()
    incb_rows = []
    for file_name in ("PropList.txt", "DerivedCoreProperties.txt",
                      "extracted/DerivedBinaryProperties.txt", "emoji/emoji-data.txt"):
        for row in fields(ucd / file_name):
            p = aliases[normalized_alias(row[1])]
            low, high = bounds(row[0])
            if len(row) == 3:
                assert p == "InCB"
                incb_rows.append((low, high, canonical(p, row[2])))
            else:
                binary_names.add(p)
                result[p, "Y"] = result.get((p, "Y"), 0) | interval(low, high)
    incb_names = sorted({row[2] for row in incb_rows} | {"None"})
    incb_ids = {name: i for i, name in enumerate(incb_names)}
    incb = bytearray([incb_ids["None"]]) * LIMIT
    for low, high, value in incb_rows:
        incb[low:high + 1] = bytes([incb_ids[value]]) * (high - low + 1)
    for value, members in array_sets(incb, incb_names).items():
        result["InCB", value] = members
    for p in binary_names:
        result[p, "N"] = UNIVERSE ^ result[p, "Y"]

    scx = fields(ucd / "ScriptExtensions.txt")
    overridden = 0
    for row in scx:
        overridden |= interval(*bounds(row[0]))
    for (p, v), members in list(result.items()):
        if p == "sc":
            result["scx", v] = members & (UNIVERSE ^ overridden)
    for row in scx:
        members = interval(*bounds(row[0]))
        for name in row[1].split():
            value = canonical("sc", name)
            result["scx", value] = result.get(("scx", value), 0) | members
    for (p, _), value in value_aliases.items():
        if p in {"gc", "sc", "age", "GCB", "WB", "SB", "InCB"}:
            result.setdefault((p, value), 0)
        if p == "sc":
            result.setdefault(("scx", value), 0)
    assert result["gc", "Nd"] == sum(1 << cp for cp in decimal)
    return result, decimal


def native_properties(binary: Path) -> dict:
    output = subprocess.check_output([str(binary), "--dump-properties"], text=True)
    result = {}
    current = None
    previous = -2
    for line in output.splitlines():
        row = line.split("\t")
        if row[0] == "property":
            assert current is None
            current = (row[1], row[2])
            assert current not in result
            result[current] = 0
            previous = -2
        elif row[0] == "range":
            assert current is not None
            low, high = int(row[1]), int(row[2])
            assert low > previous + 1, f"Noncanonical native ranges: {current}"
            result[current] |= interval(low, high)
            previous = high
        elif row[0] == "end":
            assert current is not None
            current = None
        else:
            raise AssertionError(f"Unknown native output: {line}")
    assert current is None
    return result


def compare_properties(actual: dict, expected: dict) -> None:
    assert actual.keys() == expected.keys(), (actual.keys() - expected.keys(), expected.keys() - actual.keys())
    for key, members in expected.items():
        difference = actual[key] ^ members
        assert difference == 0, f"{key}: first different code point U+{(difference & -difference).bit_length() - 1:04X}"


def check_folding(ucd: Path, binary: Path) -> int:
    graph = defaultdict(set)
    for row in fields(ucd / "CaseFolding.txt"):
        if row[1] in {"C", "S"}:
            cp, target = int(row[0], 16), int(row[2], 16)
            graph[cp].add(target)
            graph[target].add(cp)
    expected = {}
    remaining = set(graph)
    while remaining:
        start = min(remaining)
        members, pending = {start}, [start]
        while pending:
            cp = pending.pop()
            for following in graph[cp] - members:
                pending.append(following)
                members.add(following)
        remaining -= members
        ordered = tuple(sorted(members))
        for cp in members:
            expected[cp] = ordered
    actual = {}
    for line in subprocess.check_output([str(binary), "--dump-folds"], text=True).splitlines():
        row = line.split("\t")
        assert row[0] == "fold" and int(row[1]) not in actual
        actual[int(row[1])] = tuple(map(int, row[2:]))
    assert actual == expected, "Native simple-fold equivalence differs from the independent graph"
    return len(expected)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--component", type=Path, required=True,
                        help="external pinned UCD source component, including sources.v1.json")
    args = parser.parse_args()
    manifest = json.loads((args.component / "sources.v1.json").read_text())
    assert manifest["unicode_version"] == "16.0.0"
    for source in manifest["sources"]:
        assert hashlib.sha256((args.component / source["path"]).read_bytes()).hexdigest() == source["sha256"]
    expected, decimal = expected_properties(args.component / "ucd" / "16.0.0")
    actual = native_properties(args.binary.resolve())
    compare_properties(actual, expected)
    # This must detect a plausible erroneous provider, not merely parse output.
    broken = actual.copy()
    broken["gc", "Nd"] ^= 1 << 0x663
    try:
        compare_properties(broken, expected)
    except AssertionError:
        pass
    else:
        raise AssertionError("The independent checker accepted a wrong digit class")
    reported = {}
    for line in subprocess.check_output([str(args.binary.resolve()), "--dump-digits"], text=True).splitlines():
        row = line.split("\t")
        assert row[0] == "digit" and int(row[1]) not in reported
        cp, value, zero = map(int, row[1:])
        assert zero == cp - value and decimal.get(zero) == 0
        reported[cp] = value
    assert reported == decimal, "Native decimal values differ from UnicodeData"
    folds = check_folding(args.component / "ucd" / "16.0.0", args.binary.resolve())
    print(f"PASS: {len(actual)} Unicode classes over all {LIMIT} code points; "
          f"{len(decimal)} decimal digits; {folds} simple-fold members; discriminating failure control")


if __name__ == "__main__":
    main()
