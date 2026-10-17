#!/usr/bin/env python3
"""Prepare admitted Unicode interval values and authored class expressions.

There is no host Unicode/locale authority and no network access here. The
generated catalog contains code-point properties; consumers choose a domain.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path

from gslt2parse_schema_v1 import StringLiteral, render
from unicode_presentation_v1 import DOMAINS, read_catalog, read_lexical_classes


ROOT = Path(__file__).resolve().parents[1]
VERSION = "16.0.0"
MAXIMUM = 0x10FFFF
Range = tuple[int, int]


def loose(text: str) -> str:
    if not text.isascii():
        raise ValueError("UCD property aliases must be ASCII")
    return "".join(c.lower() for c in text if c not in "_- \t\r\n\v\f")


def normalized(ranges: list[Range]) -> tuple[Range, ...]:
    answer: list[Range] = []
    for low, high in sorted(ranges):
        if not (0 <= low <= high <= MAXIMUM):
            raise ValueError("Range is outside the code-point domain")
        if answer and low <= answer[-1][1] + 1:
            answer[-1] = (answer[-1][0], max(answer[-1][1], high))
        else:
            answer.append((low, high))
    return tuple(answer)


def subtract(left: tuple[Range, ...], right: tuple[Range, ...]) -> tuple[Range, ...]:
    result: list[Range] = []
    j = 0
    for low, high in left:
        while j < len(right) and right[j][1] < low:
            j += 1
        k = j
        position = low
        while k < len(right) and right[k][0] <= high:
            rlow, rhigh = right[k]
            if position < rlow:
                result.append((position, rlow - 1))
            position = max(position, rhigh + 1)
            if position > high:
                break
            k += 1
        if position <= high:
            result.append((position, high))
    return tuple(result)


def code_range(text: str) -> Range:
    fields = text.strip().split("..")
    return int(fields[0], 16), int(fields[-1], 16)


def lines(path: Path) -> list[list[str]]:
    return [
        [field.strip() for field in row.split("#", 1)[0].split(";")]
        for row in path.read_text(encoding="utf-8").splitlines()
        if row.split("#", 1)[0].strip()
    ]


def missing(path: Path) -> list[list[str]]:
    return [
        [field.strip() for field in row.split("@missing:", 1)[1].split("#", 1)[0].split(";")]
        for row in path.read_text(encoding="utf-8").splitlines()
        if "@missing:" in row
    ]


def compile_catalog(component: Path) -> dict:
    manifest = json.loads((component / "sources.v1.json").read_text())
    if manifest["unicode_version"] != VERSION:
        raise ValueError("Wrong input-data version")
    for source in manifest["sources"]:
        actual = hashlib.sha256((component / source["path"]).read_bytes()).hexdigest()
        if actual != source["sha256"]:
            raise ValueError(f"Unpinned input: {source['path']}")
    ucd = component / "ucd" / VERSION
    properties: dict[str, str] = {}
    for fields in lines(ucd / "PropertyAliases.txt"):
        for alias in fields:
            previous = properties.setdefault(loose(alias), fields[0])
            if previous != fields[0]:
                raise ValueError("Ambiguous property alias")
    values: dict[tuple[str, str], str] = {}
    for fields in lines(ucd / "PropertyValueAliases.txt"):
        # CCC's first value is its numeric spelling; every spelling maps to it.
        canonical = fields[1]
        for alias in fields[1:]:
            values[(fields[0], loose(alias))] = canonical

    def prop(name: str) -> str:
        return properties[loose(name)]

    def val(property_name: str, name: str) -> str:
        return values.get((property_name, loose(name)), name)

    sets: dict[tuple[str, str], list[Range]] = defaultdict(list)

    def enum_file(file_name: str, property_name: str) -> None:
        p = prop(property_name)
        rows = lines(ucd / file_name)
        for fields in rows:
            sets[(p, val(p, fields[1]))].append(code_range(fields[0]))
        occupied = normalized([code_range(row[0]) for row in rows])
        defaults = missing(ucd / file_name)
        if not defaults and occupied != ((0, MAXIMUM),):
            raise ValueError(f"Neither full coverage nor a default rule in {file_name}")
        for default in defaults:
            value = val(p, default[-1])
            sets[(p, value)].extend(subtract(normalized([code_range(default[0])]), occupied))

    enum_file("extracted/DerivedGeneralCategory.txt", "gc")
    enum_file("Scripts.txt", "sc")
    enum_file("DerivedAge.txt", "age")
    enum_file("auxiliary/GraphemeBreakProperty.txt", "GCB")
    enum_file("auxiliary/WordBreakProperty.txt", "WB")
    enum_file("auxiliary/SentenceBreakProperty.txt", "SB")

    for file_name in ("PropList.txt", "DerivedCoreProperties.txt", "extracted/DerivedBinaryProperties.txt", "emoji/emoji-data.txt"):
        for fields in lines(ucd / file_name):
            p = prop(fields[1])
            value = val(p, fields[2]) if len(fields) >= 3 else "Y"
            sets[(p, value)].append(code_range(fields[0]))
        for default in missing(ucd / file_name):
            if len(default) != 3:
                continue
            p = prop(default[1])
            explicit = normalized([r for (key, _), rs in sets.items() if key == p for r in rs])
            sets[(p, val(p, default[2]))].extend(subtract(normalized([code_range(default[0])]), explicit))

    scx_rows = lines(ucd / "ScriptExtensions.txt")
    overrides = normalized([code_range(fields[0]) for fields in scx_rows])
    for (p, v), ranges in list(sets.items()):
        if p == "sc":
            sets[("scx", v)] = list(subtract(normalized(ranges), overrides))
    for fields in scx_rows:
        for script in fields[1].split():
            sets[("scx", val("sc", script))].append(code_range(fields[0]))
    for (p, alias), value in list(values.items()):
        if p == "sc":
            values[("scx", alias)] = value

    for category in "LMNPSZC":
        sets[("gc", category)] = [
            r for (p, v), rs in list(sets.items())
            if p == "gc" and len(v) == 2 and v.startswith(category) for r in rs
        ]
    sets[("gc", "LC")] = [r for v in ("Lu", "Ll", "Lt") for r in sets[("gc", v)]]
    for p, v in list(sets):
        if v == "Y":
            sets[(p, "N")] = list(subtract(((0, MAXIMUM),), normalized(sets[(p, "Y")])))
    # Alias rows can describe classes with no assigned member in this version.
    for p, _, value in ((p, a, v) for (p, a), v in list(values.items())):
        if p in ("gc", "sc", "scx", "age", "GCB", "WB", "SB", "InCB"):
            sets.setdefault((p, value), [])

    decimal_points = {}
    for fields in lines(ucd / "UnicodeData.txt"):
        if fields[6]:
            decimal_points[int(fields[0], 16)] = int(fields[6])
    zeroes = sorted(cp for cp, value in decimal_points.items() if value == 0)
    for zero in zeroes:
        if any(decimal_points.get(zero + digit) != digit for digit in range(10)):
            raise ValueError("Decimal digits do not form a ten-value block")
    if len(zeroes) * 10 != len(decimal_points):
        raise ValueError("Decimal blocks do not cover every decimal digit")
    if normalized([(cp, cp) for cp in decimal_points]) != normalized(sets[("gc", "Nd")]):
        raise ValueError("UnicodeData decimal values disagree with category Nd")

    parent: dict[int, int] = {}

    def root(cp: int) -> int:
        parent.setdefault(cp, cp)
        while parent[cp] != cp:
            parent[cp] = parent[parent[cp]]
            cp = parent[cp]
        return cp

    for fields in lines(ucd / "CaseFolding.txt"):
        if fields[1] not in ("C", "S"):
            continue
        targets = fields[2].split()
        if len(targets) != 1:
            raise ValueError("Simple case folding has a multi-scalar mapping")
        source, target = root(int(fields[0], 16)), root(int(targets[0], 16))
        parent[max(source, target)] = min(source, target)
    groups: dict[int, list[int]] = defaultdict(list)
    for cp in sorted(parent):
        groups[root(cp)].append(cp)

    return {
        "sets": {key: normalized(rs) for key, rs in sorted(sets.items())},
        "properties": sorted(properties.items()),
        "values": sorted((p, alias, value) for (p, alias), value in values.items()),
        "decimal_zeroes": zeroes,
        "fold_groups": list(groups.values()),
    }


def generate_presentation(catalog: dict) -> str:
    """Serialize raw property data with shared interval and list identities."""
    intervals = sorted({r for rs in catalog["sets"].values() for r in rs})
    interval_id = {r: i for i, r in enumerate(intervals)}
    lists = sorted(set(catalog["sets"].values()))
    list_id = {rs: i for i, rs in enumerate(lists)}
    def text(value):
        return StringLiteral(value)
    sections = [
        ("version", text(VERSION)),
        ("domains", *((text(k), tuple(v)) for k, v in DOMAINS.items())),
        ("intervals", *intervals),
        ("range-lists", *(tuple(interval_id[r] for r in rs) for rs in lists)),
        ("classes", *((text(p), text(v), list_id[rs])
                      for (p, v), rs in catalog["sets"].items())),
        ("property-aliases", *((text(a), text(b)) for a, b in catalog["properties"])),
        ("value-aliases", *((text(p), text(a), text(v)) for p, a, v in catalog["values"])),
        ("decimal-zeroes", *catalog["decimal_zeroes"]),
        ("fold-groups", *(tuple(xs) for xs in catalog["fold_groups"])),
    ]
    output = ["; Unicode 16.0.0 property data; see LICENSE.txt and sources.v1.json.",
              "; Class meanings use shared canonical intervals and range-list identities.",
              "(unicode-interval-catalog-v1"]
    for tag, *rows in sections:
        output.append("  (" + tag)
        output.extend("    " + render(row) for row in rows)
        output.append("  )")
    output.append(")")
    return "\n".join(output) + "\n"


def generate(component: Path) -> dict[str, str]:
    catalog = read_catalog(component / "unicode_codepoint_classes16_v1.metta")
    if catalog["version"] != VERSION:
        raise ValueError("Unicode property provider requires the pinned Unicode version")
    lexical = read_lexical_classes(component / "lexical_classes_v1.metta", catalog)
    pool: list[Range] = []
    slices = {}
    rows = []
    for (property_name, value), ranges in catalog["sets"].items():
        if ranges not in slices:
            slices[ranges] = (len(pool), len(ranges))
            pool.extend(ranges)
        begin, count = slices[ranges]
        rows.append((property_name, value, begin, count))
    fold_pool: list[int] = []
    fold_index = []
    for members in catalog["fold_groups"]:
        begin = len(fold_pool)
        fold_pool.extend(members)
        fold_index.extend((cp, begin, len(members)) for cp in members)
    fold_index.sort()
    header = """/* Generated from unicode_codepoint_classes16_v1.metta.
 * Unicode-derived data are covered by ../LICENSE.txt (Unicode-3.0). */
#ifndef CETTA_UNICODE_UCD16_DATA_V1_H
#define CETTA_UNICODE_UCD16_DATA_V1_H
#include \"../../../native/unicode_class_v1.h\"
typedef struct { const char *name; const char *canonical; } UnicodeV1PropertyAlias;
typedef struct { const char *property; const char *name; const char *canonical; } UnicodeV1ValueAlias;
typedef struct { const char *property; const char *value; uint32_t begin; uint32_t len; } UnicodeV1PropertyRow;
typedef struct { uint32_t scalar; uint32_t begin; uint32_t len; } UnicodeV1FoldRow;
typedef struct { const char *name; UnicodeV1Ranges ranges; } UnicodeV1LexicalRow;
typedef struct { const char *name; UnicodeV1DigitClass digits; } UnicodeV1DigitRow;
extern const CettaLpNativeUnicodeRange unicode_v1_ucd_ranges[];
extern const UnicodeV1PropertyRow unicode_v1_ucd_properties[];
extern const size_t unicode_v1_ucd_property_len;
extern const UnicodeV1PropertyAlias unicode_v1_ucd_property_aliases[];
extern const size_t unicode_v1_ucd_property_alias_len;
extern const UnicodeV1ValueAlias unicode_v1_ucd_value_aliases[];
extern const size_t unicode_v1_ucd_value_alias_len;
extern const uint32_t unicode_v1_ucd_decimal_zeroes[];
extern const size_t unicode_v1_ucd_decimal_len;
extern const UnicodeV1FoldRow unicode_v1_ucd_fold_index[];
extern const size_t unicode_v1_ucd_fold_index_len;
extern const uint32_t unicode_v1_ucd_fold_members[];
extern const UnicodeV1Ranges unicode_v1_ucd_domains[5];
extern const char unicode_v1_ucd_version[];
extern const UnicodeV1LexicalRow unicode_v1_ucd_lexical_classes[];
extern const size_t unicode_v1_ucd_lexical_class_len;
extern const UnicodeV1DigitRow unicode_v1_ucd_digit_classes[];
extern const size_t unicode_v1_ucd_digit_class_len;
#endif
"""
    source = ["/* Generated Unicode-derived data; see ../LICENSE.txt and sources.v1.json. */",
              '#include "unicode_ucd16_v1.generated.h"', ""]

    def array(kind: str, name: str, entries: list[str], count_name: str | None = None) -> None:
        source.append(f"const {kind} {name}[] = {{")
        source.extend("    " + entry + "," for entry in entries)
        source.append("};")
        if count_name:
            source.append(f"const size_t {count_name} = sizeof({name}) / sizeof({name}[0]);")
        source.append("")

    def string(text: str) -> str:
        return json.dumps(text, ensure_ascii=True)

    source.append(f"const char unicode_v1_ucd_version[] = {string(catalog['version'])};")
    domain_rows = []
    for index, domain in enumerate(DOMAINS):
        ranges = catalog["domains"][domain]
        name = f"unicode_v1_ucd_domain_{index}"
        source.append(f"static const CettaLpNativeUnicodeRange {name}[] = {{")
        source.extend(f"    {{0x{low:x}u, 0x{high:x}u}}," for low, high in ranges)
        source.extend(("};", ""))
        domain_rows.append(f"{{{name}, {len(ranges)}u}}")
    array("UnicodeV1Ranges", "unicode_v1_ucd_domains", domain_rows)
    lexical_rows = []
    for index, (name, (_, ranges)) in enumerate(lexical["classes"].items()):
        array_name = f"unicode_v1_ucd_lexical_{index}"
        source.append(f"static const CettaLpNativeUnicodeRange {array_name}[] = {{")
        source.extend(f"    {{0x{low:x}u, 0x{high:x}u}}," for low, high in ranges)
        if not ranges:
            source.append("    {0u, 0u},")
        source.extend(("};", ""))
        lexical_rows.append(f"{{{string(name)}, {{{array_name}, {len(ranges)}u}}}}")
    array("UnicodeV1LexicalRow", "unicode_v1_ucd_lexical_classes", lexical_rows,
          "unicode_v1_ucd_lexical_class_len")
    digit_rows = []
    for index, (name, values) in enumerate(lexical["digits"].items()):
        low, high = min(values), max(values)
        array_name = f"unicode_v1_ucd_digits_{index}"
        source.append(f"static const uint8_t {array_name}[] = {{")
        for start in range(low, high + 1, 16):
            source.append("    " + ", ".join(str(values[cp] + 1) if cp in values else "0"
                          for cp in range(start, min(start + 16, high + 1))) + ",")
        source.extend(("};", ""))
        digit_rows.append(f"{{{string(name)}, {{{array_name}, {low}u, {high - low + 1}u}}}}")
    array("UnicodeV1DigitRow", "unicode_v1_ucd_digit_classes", digit_rows,
          "unicode_v1_ucd_digit_class_len")
    array("CettaLpNativeUnicodeRange", "unicode_v1_ucd_ranges", [f"{{0x{low:x}u, 0x{high:x}u}}" for low, high in pool])
    array("UnicodeV1PropertyRow", "unicode_v1_ucd_properties", [f"{{{string(p)}, {string(v)}, {begin}u, {count}u}}" for p, v, begin, count in rows], "unicode_v1_ucd_property_len")
    array("UnicodeV1PropertyAlias", "unicode_v1_ucd_property_aliases", [f"{{{string(alias)}, {string(p)}}}" for alias, p in catalog["properties"]], "unicode_v1_ucd_property_alias_len")
    array("UnicodeV1ValueAlias", "unicode_v1_ucd_value_aliases", [f"{{{string(p)}, {string(alias)}, {string(v)}}}" for p, alias, v in catalog["values"]], "unicode_v1_ucd_value_alias_len")
    array("uint32_t", "unicode_v1_ucd_decimal_zeroes", [f"0x{cp:x}u" for cp in catalog["decimal_zeroes"]], "unicode_v1_ucd_decimal_len")
    array("uint32_t", "unicode_v1_ucd_fold_members", [f"0x{cp:x}u" for cp in fold_pool])
    array("UnicodeV1FoldRow", "unicode_v1_ucd_fold_index", [f"{{0x{cp:x}u, {begin}u, {count}u}}" for cp, begin, count in fold_index], "unicode_v1_ucd_fold_index_len")

    return {
        "generated/unicode_ucd16_v1.generated.h": header,
        "generated/unicode_ucd16_v1.generated.c": "\n".join(source) + "\n",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT / "langdef" / "unicode")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--from-presentation", action="store_true",
                        help="compile the bundled interval data (the default)")
    parser.add_argument("--from-ucd", type=Path, metavar="SOURCE_COMPONENT",
                        help="regenerate from an explicit external pinned UCD component")
    args = parser.parse_args()
    presentation_path = args.root / "unicode_codepoint_classes16_v1.metta"
    if args.from_presentation and args.from_ucd:
        parser.error("--from-presentation and --from-ucd are mutually exclusive")
    if args.from_ucd:
        presentation = generate_presentation(compile_catalog(args.from_ucd))
        if args.check:
            if presentation_path.read_text() != presentation:
                raise SystemExit("Generated Unicode presentation differs from the pinned inputs")
        else:
            presentation_path.write_text(presentation)
    outputs = generate(args.root)
    records = []
    for relative, text in outputs.items():
        path = args.root / relative
        content = text.encode("utf-8")
        if args.check:
            if not path.exists() or path.read_bytes() != content:
                raise SystemExit(f"Generated Unicode output differs: {relative}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        records.append({"path": relative, "sha256": hashlib.sha256(content).hexdigest(), "bytes": len(content)})
    presentation_bytes = presentation_path.read_bytes()
    records.append({"path": presentation_path.name,
                    "sha256": hashlib.sha256(presentation_bytes).hexdigest(),
                    "bytes": len(presentation_bytes)})
    lexical_path = args.root / "lexical_classes_v1.metta"
    lexical_bytes = lexical_path.read_bytes()
    records.append({"path": lexical_path.name, "sha256": hashlib.sha256(lexical_bytes).hexdigest(),
                    "bytes": len(lexical_bytes), "authored": True})
    manifest = {
        "format": "cetta-unicode-generated-v1", "unicode_version": VERSION,
        "generator": "tools/compile_unicode_data_v1.py",
        "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "compiler_sources": {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                             for path in (Path(__file__).with_name("unicode_presentation_v1.py"),
                                          Path(__file__).with_name("gslt2parse_schema_v1.py"))},
        "native_input": presentation_path.name,
        "lexical_input": lexical_path.name,
        "outputs": records,
    }
    if (args.root / "sources.v1.json").exists():
        manifest["sources_sha256"] = hashlib.sha256((args.root / "sources.v1.json").read_bytes()).hexdigest()
    text = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    manifest_path = args.root / "generated.v1.json"
    if args.check:
        if not manifest_path.exists() or manifest_path.read_text() != text:
            raise SystemExit("Generated Unicode manifest differs")
    else:
        manifest_path.write_text(text)
    print(f"PASS: deterministic Unicode {VERSION} products ({sum(r['bytes'] for r in records)} bytes)")


if __name__ == "__main__":
    main()
