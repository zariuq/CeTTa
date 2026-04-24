#!/usr/bin/env python3
"""Synchronize and validate the top-level CeTTa MeTTa test manifest."""

from __future__ import annotations

import argparse
import dataclasses
import difflib
import glob
import re
import sys
from collections import defaultdict
from pathlib import Path


HEADER = [
    "path",
    "lang",
    "syntax",
    "profile",
    "build",
    "space_engine",
    "lane",
    "expect",
    "notes",
]

INVENTORY_PATTERNS = (
    "tests/test_*.metta",
    "tests/spec_*.metta",
    "tests/he_*.metta",
)

MAKEFILE_LISTS = (
    "PYTHON_TESTS",
    "PATHMAP_REQUIRED_TESTS",
    "PATHMAP_PROBE_TESTS",
    "CORE_PROBE_TESTS",
    "CORE_XFAIL_TESTS",
    "RUNTIME_STATS_METTA_TESTS",
    "BACKEND_DEDICATED_TESTS",
    "BACKEND_HEAVY_TESTS",
    "BACKEND_DIAGNOSTIC_TESTS",
    "BACKEND_PENDING_CORRECTNESS_TESTS",
)

VALID_BUILDS = {
    "any",
    "core",
    "main",
    "mork",
    "pathmap",
    "python",
    "full",
    "runtime-stats",
}

VALID_LANES = {
    "test",
    "test-backend-dedicated",
    "test-fallback-eval-session",
    "test-heavy",
    "test-import-modes",
    "test-pathmap-lane",
    "test-profiles",
    "test-python",
    "test-runtime-stats-lane",
    "probe-core-lane",
    "probe-pathmap-lane",
    "xfail-core-lane",
}

VALID_EXPECTS = {
    "binary",
    "diagnostic",
    "golden",
    "probe",
    "property",
    "xfail",
}

LANE_ORDER = {
    "test": 10,
    "test-profiles": 20,
    "test-python": 30,
    "test-runtime-stats-lane": 40,
    "test-heavy": 50,
    "test-backend-dedicated": 60,
    "test-pathmap-lane": 70,
    "probe-core-lane": 80,
    "probe-pathmap-lane": 90,
    "xfail-core-lane": 100,
    "test-fallback-eval-session": 110,
    "test-import-modes": 120,
}

NO_EXPECT_CLASSIFICATION = {
    "tests/test_bio_wmpln_checkpoint_petta_flat.metta": (
        "diagnostic",
        "heavy WM-PLN PeTTa flat-loader checkpoint count witness",
    ),
    "tests/test_bio_wmpln_checkpoint_petta_top.metta": (
        "diagnostic",
        "heavy WM-PLN PeTTa top-level checkpoint count witness",
    ),
    "tests/test_bio_wmpln_pathway_route_regression.metta": (
        "property",
        "heavy WM-PLN pathway route assertion property",
    ),
    "tests/test_checkpoint_disease_route_probe.metta": (
        "probe",
        "heavy WM-PLN disease route probe",
    ),
    "tests/test_cverify_apply_subst_probe.metta": (
        "probe",
        "cverify apply_subst diagnostic witness",
    ),
    "tests/test_cverify_apply_subst_with_unify_probe.metta": (
        "probe",
        "cverify apply_subst with local unify diagnostic witness",
    ),
    "tests/test_mm2_match_order_fragile.metta": (
        "probe",
        "MORK match-order diagnostic",
    ),
    "tests/test_mork_nil_parity_regression.metta": (
        "probe",
        "MORK nil parity diagnostic",
    ),
    "tests/test_pathmap_backend_primary_destructive_regression.metta": (
        "probe",
        "pathmap destructive surface diagnostic",
    ),
    "tests/test_print_nondet_probe.metta": (
        "probe",
        "print nondeterminism diagnostic witness",
    ),
    "tests/test_tilepuzzle.metta": (
        "diagnostic",
        "heavy 8-puzzle BFS native witness without golden oracle",
    ),
    "tests/test_tilepuzzle_pathmap.metta": (
        "diagnostic",
        "heavy 8-puzzle BFS pathmap witness without golden oracle",
    ),
}

@dataclasses.dataclass(frozen=True)
class ManifestRow:
    path: str
    lang: str
    syntax: str
    profile: str
    build: str
    space_engine: str
    lane: str
    expect: str
    notes: str

    @classmethod
    def from_fields(cls, fields: list[str]) -> "ManifestRow":
        return cls(*fields)

    def fields(self) -> list[str]:
        return [
            self.path,
            self.lang,
            self.syntax,
            self.profile,
            self.build,
            self.space_engine,
            self.lane,
            self.expect,
            self.notes,
        ]

    def render(self) -> str:
        return "\t".join(self.fields())


SPECIAL_INVENTORY_ROWS = {
    "tests/spec_module_inventory.metta": [
        ManifestRow(
            "tests/spec_module_inventory.metta",
            "he",
            "metta",
            "he_extended",
            "main",
            "native",
            "test-profiles",
            "golden",
            "module inventory reports language profile and import mode",
        ),
    ],
    "tests/spec_profile_once_alias_extension.metta": [
        ManifestRow(
            "tests/spec_profile_once_alias_extension.metta",
            "he",
            "metta",
            "",
            "main",
            "native",
            "test-profiles",
            "diagnostic",
            "base HE rejects an extended surface without a profile",
        ),
    ],
}


def strip_make_comment(line: str) -> str:
    in_single = False
    in_double = False
    for idx, ch in enumerate(line):
        if ch == "'" and not in_double:
            in_single = not in_single
        elif ch == '"' and not in_single:
            in_double = not in_double
        elif ch == "#" and not in_single and not in_double:
            return line[:idx]
    return line


def make_tokens(value: str) -> list[str]:
    value = strip_make_comment(value).strip()
    if not value:
        return []
    return value.split()


def parse_makefile(path: Path) -> dict[str, list[str]]:
    variables: dict[str, list[str]] = defaultdict(list)
    current: str | None = None
    assignment_re = re.compile(r"^([A-Z0-9_]+)\s*(\+=|:=|\?=|=)\s*(.*)$")

    for raw in path.read_text().splitlines():
        if current is None:
            match = assignment_re.match(raw)
            if not match:
                continue
            name, op, rest = match.groups()
            if op != "+=":
                variables[name] = []
            continued = rest.rstrip().endswith("\\")
            value = rest.rstrip()[:-1] if continued else rest
            variables[name].extend(make_tokens(value))
            current = name if continued else None
            continue

        continued = raw.rstrip().endswith("\\")
        value = raw.rstrip()[:-1] if continued else raw
        variables[current].extend(make_tokens(value))
        if not continued:
            current = None

    return dict(variables)


def expand_make_tokens(
    variables: dict[str, list[str]],
    name: str,
    stack: tuple[str, ...] = (),
) -> list[str]:
    if name in stack:
        return []
    expanded: list[str] = []
    for token in variables.get(name, []):
        ref = re.fullmatch(r"\$\(([^)]+)\)", token)
        if ref:
            expanded.extend(expand_make_tokens(variables, ref.group(1), stack + (name,)))
        elif token.startswith("tests/"):
            expanded.append(token)
    return expanded


def makefile_sets(repo: Path) -> dict[str, set[str]]:
    variables = parse_makefile(repo / "Makefile")
    return {name: set(expand_make_tokens(variables, name)) for name in MAKEFILE_LISTS}


def inventory_paths(repo: Path) -> list[str]:
    paths: list[str] = []
    for pattern in INVENTORY_PATTERNS:
        paths.extend(glob.glob(str(repo / pattern)))
    return sorted(str(Path(path).relative_to(repo)) for path in paths if Path(path).is_file())


def expected_path(test_path: str) -> str:
    return re.sub(r"\.[^.]+$", ".expected", test_path)


def has_expected(repo: Path, test_path: str) -> bool:
    return (repo / expected_path(test_path)).is_file()


def no_expected_expect_and_note(repo: Path, test_path: str) -> tuple[str, str]:
    if test_path in NO_EXPECT_CLASSIFICATION:
        return NO_EXPECT_CLASSIFICATION[test_path]
    text = (repo / test_path).read_text(errors="replace")
    if "assertEqual" in text or "assertEqualToResult" in text or "assertTrue" in text:
        return (
            "property",
            "assertion-only property without golden oracle",
        )
    if "probe" in Path(test_path).stem:
        return (
            "probe",
            "probe without golden oracle",
        )
    return (
        "diagnostic",
        "diagnostic witness without golden oracle",
    )


def generated_expect_and_note(repo: Path, test_path: str, note: str) -> tuple[str, str]:
    if has_expected(repo, test_path):
        return "golden", note
    return no_expected_expect_and_note(repo, test_path)


def generated_row(repo: Path, test_path: str, sets: dict[str, set[str]]) -> ManifestRow:
    if test_path in sets["PATHMAP_REQUIRED_TESTS"]:
        expect, note = generated_expect_and_note(
            repo,
            test_path,
            "pathmap lane golden regression",
        )
        return ManifestRow(
            test_path, "he", "metta", "he_extended", "pathmap", "pathmap",
            "test-pathmap-lane", expect, note,
        )
    if test_path in sets["PATHMAP_PROBE_TESTS"]:
        expect, note = no_expected_expect_and_note(repo, test_path)
        return ManifestRow(
            test_path, "he", "metta", "he_extended", "pathmap", "pathmap",
            "probe-pathmap-lane", expect, note,
        )
    if test_path in sets["CORE_PROBE_TESTS"]:
        expect, note = no_expected_expect_and_note(repo, test_path)
        return ManifestRow(
            test_path, "he", "metta", "he_extended", "main", "native",
            "probe-core-lane", expect, note,
        )
    if test_path in sets["CORE_XFAIL_TESTS"]:
        return ManifestRow(
            test_path, "he", "metta", "he_extended", "main", "native",
            "xfail-core-lane", "xfail",
            "known failing core regression",
        )
    if test_path in sets["RUNTIME_STATS_METTA_TESTS"]:
        expect, note = generated_expect_and_note(
            repo,
            test_path,
            "runtime-stats lane golden regression",
        )
        return ManifestRow(
            test_path, "he", "metta", "he_extended", "runtime-stats", "native",
            "test-runtime-stats-lane", expect, note,
        )
    if test_path in sets["PYTHON_TESTS"]:
        expect, note = generated_expect_and_note(
            repo,
            test_path,
            "Python-enabled build golden regression",
        )
        return ManifestRow(
            test_path, "he", "metta", "he_extended", "python", "native",
            "test-python", expect, note,
        )
    if test_path in sets["BACKEND_HEAVY_TESTS"]:
        build = "pathmap" if "pathmap" in Path(test_path).stem else "main"
        engine = "pathmap" if build == "pathmap" else "native"
        expect, note = generated_expect_and_note(
            repo,
            test_path,
            "heavy backend golden regression",
        )
        return ManifestRow(
            test_path, "he", "metta", "he_extended", build, engine,
            "test-heavy", expect, note,
        )
    if test_path in sets["BACKEND_DEDICATED_TESTS"]:
        stem = Path(test_path).stem
        bridgeish = "mork" in stem or "mm2" in stem or stem in {
            "test_new_space_mork_surface",
            "test_step_space_surface",
        }
        runtime_stats = "runtime_stats" in stem or "runtime-stats" in stem
        build = "runtime-stats" if runtime_stats else ("mork" if bridgeish else "main")
        engine = "pathmap" if bridgeish else "native"
        expect, note = generated_expect_and_note(
            repo,
            test_path,
            "dedicated backend or bridge golden regression",
        )
        return ManifestRow(
            test_path, "he", "metta", "he_extended", build, engine,
            "test-backend-dedicated", expect, note,
        )

    expect, note = generated_expect_and_note(
        repo,
        test_path,
        "-",
    )
    return ManifestRow(
        test_path, "he", "metta", "he_extended", "main", "native",
        "test", expect, note,
    )


def parse_manifest(path: Path) -> tuple[list[ManifestRow], list[str]]:
    errors: list[str] = []
    if not path.is_file():
        return [], [f"missing manifest {path}"]

    lines = path.read_text().splitlines()
    if not lines:
        return [], [f"{path}: empty manifest"]
    if lines[0].split("\t") != HEADER:
        errors.append(f"{path}:1: invalid header")

    rows: list[ManifestRow] = []
    for lineno, line in enumerate(lines[1:], start=2):
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) != len(HEADER):
            errors.append(f"{path}:{lineno}: expected {len(HEADER)} TSV columns, got {len(fields)}")
            continue
        rows.append(ManifestRow.from_fields(fields))
    return rows, errors


def row_sort_key(row: ManifestRow) -> tuple[str, int, str, str, str, str]:
    return (
        row.path,
        LANE_ORDER.get(row.lane, 1000),
        row.build,
        row.space_engine,
        row.expect,
        row.notes,
    )


def generate_manifest(repo: Path, existing_rows: list[ManifestRow]) -> list[ManifestRow]:
    sets = makefile_sets(repo)
    inventory = set(inventory_paths(repo))
    rows = [row for row in existing_rows if row.path not in inventory]

    for test_path in sorted(inventory):
        if test_path in SPECIAL_INVENTORY_ROWS:
            rows.extend(SPECIAL_INVENTORY_ROWS[test_path])
        else:
            rows.append(generated_row(repo, test_path, sets))

    return sorted(rows, key=row_sort_key)


def render_manifest(rows: list[ManifestRow]) -> str:
    return "\t".join(HEADER) + "\n" + "\n".join(row.render() for row in rows) + "\n"


def check_exact_lane(
    rows: list[ManifestRow],
    expected: set[str],
    lane: str,
    label: str,
    errors: list[str],
) -> None:
    actual = {row.path for row in rows if row.lane == lane}
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        if missing:
            errors.append(f"{label}: missing from lane {lane}: {', '.join(missing)}")
        if extra:
            errors.append(f"{label}: extra in lane {lane}: {', '.join(extra)}")


def check_paths_present(
    rows: list[ManifestRow],
    expected: set[str],
    label: str,
    errors: list[str],
) -> None:
    actual = {row.path for row in rows}
    missing = sorted(expected - actual)
    if missing:
        errors.append(f"{label}: missing manifest paths: {', '.join(missing)}")


def validate_manifest(repo: Path, rows: list[ManifestRow]) -> list[str]:
    errors: list[str] = []
    inventory = set(inventory_paths(repo))
    paths = {row.path for row in rows}
    sets = makefile_sets(repo)

    for test_path in sorted(inventory - paths):
        errors.append(f"missing inventory row for {test_path}")

    seen: set[ManifestRow] = set()
    for row in rows:
        if row in seen:
            errors.append(f"duplicate manifest row: {row.render()}")
        seen.add(row)

        if "\t" in row.notes:
            errors.append(f"{row.path}: notes contain a tab")
        if row.profile == "none":
            errors.append(f"{row.path}: use a blank profile field, not none")
        if row.build not in VALID_BUILDS:
            errors.append(f"{row.path}: unknown build {row.build}")
        if row.lane not in VALID_LANES:
            errors.append(f"{row.path}: unknown lane {row.lane}")
        if row.expect not in VALID_EXPECTS:
            errors.append(f"{row.path}: unknown expect {row.expect}")
        if not (repo / row.path).is_file():
            errors.append(f"{row.path}: missing path")
        if row.expect == "golden" and not has_expected(repo, row.path):
            errors.append(f"{row.path}: golden row missing {expected_path(row.path)}")
        if row.path in inventory and not has_expected(repo, row.path) and row.expect == "golden":
            errors.append(f"{row.path}: top-level no-expected file cannot use expect=golden")

    check_exact_lane(
        rows,
        sets["PATHMAP_REQUIRED_TESTS"],
        "test-pathmap-lane",
        "PATHMAP_REQUIRED_TESTS",
        errors,
    )
    check_exact_lane(
        rows,
        sets["PATHMAP_PROBE_TESTS"],
        "probe-pathmap-lane",
        "PATHMAP_PROBE_TESTS",
        errors,
    )
    check_exact_lane(
        rows,
        sets["CORE_PROBE_TESTS"],
        "probe-core-lane",
        "CORE_PROBE_TESTS",
        errors,
    )
    check_exact_lane(
        rows,
        sets["CORE_XFAIL_TESTS"],
        "xfail-core-lane",
        "CORE_XFAIL_TESTS",
        errors,
    )
    check_exact_lane(
        rows,
        sets["RUNTIME_STATS_METTA_TESTS"],
        "test-runtime-stats-lane",
        "RUNTIME_STATS_METTA_TESTS",
        errors,
    )
    check_exact_lane(
        rows,
        sets["BACKEND_HEAVY_TESTS"],
        "test-heavy",
        "BACKEND_HEAVY_TESTS",
        errors,
    )
    check_exact_lane(
        rows,
        sets["PYTHON_TESTS"],
        "test-python",
        "PYTHON_TESTS",
        errors,
    )
    check_paths_present(
        rows,
        sets["BACKEND_DEDICATED_TESTS"],
        "BACKEND_DEDICATED_TESTS",
        errors,
    )

    diagnostic_paths = sets["BACKEND_DIAGNOSTIC_TESTS"]
    bad_diagnostics = sorted(
        row.path
        for row in rows
        if row.path in diagnostic_paths and row.expect not in {"diagnostic", "probe", "property"}
    )
    if bad_diagnostics:
        errors.append(
            "BACKEND_DIAGNOSTIC_TESTS: diagnostic paths must use diagnostic/probe/property: "
            + ", ".join(bad_diagnostics)
        )

    pending = sets["BACKEND_PENDING_CORRECTNESS_TESTS"]
    if pending:
        missing_pending = sorted(pending - paths)
        if missing_pending:
            errors.append(
                "BACKEND_PENDING_CORRECTNESS_TESTS: missing manifest paths: "
                + ", ".join(missing_pending)
            )

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--check", action="store_true", help="validate and require generated output to match")
    group.add_argument("--write", action="store_true", help="rewrite the manifest deterministically")
    args = parser.parse_args()

    repo = Path.cwd()
    manifest = repo / "tests/test_manifest.tsv"
    existing_rows, parse_errors = parse_manifest(manifest)
    if parse_errors and args.write:
        for error in parse_errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1

    generated_rows = generate_manifest(repo, existing_rows)
    generated_text = render_manifest(generated_rows)

    if args.write:
        manifest.write_text(generated_text)
        print(f"PASS: wrote {manifest} with {len(generated_rows)} rows")
        return 0

    errors = parse_errors + validate_manifest(repo, existing_rows)
    current_text = manifest.read_text()
    if current_text != generated_text:
        diff = difflib.unified_diff(
            current_text.splitlines(),
            generated_text.splitlines(),
            fromfile=str(manifest),
            tofile=f"{manifest} (generated)",
            lineterm="",
        )
        errors.append("manifest is not synchronized; run make test-manifest-sync")
        print("\n".join(diff), file=sys.stderr)

    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print(f"PASS: test manifest covers {len(inventory_paths(repo))} top-level MeTTa tests")
    print(f"PASS: test manifest has {len(existing_rows)} rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
