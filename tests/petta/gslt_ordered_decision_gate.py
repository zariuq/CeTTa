#!/usr/bin/env python3
"""Run the ordered-decision controls on the pinned draft binary and reject a mismatch."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CETTA = ROOT / "cetta"
LIBRARY = ROOT / "lib" / "gslt_ordered_decision.metta"
CONTROLS = ROOT / "tests" / "petta" / "gslt_ordered_decision.metta"
IMPORT_ONLY = ROOT / "tests" / "petta" / "gslt_ordered_decision_import_only.metta"
EXPECTED = ROOT / "tests" / "petta" / "gslt_ordered_decision.expected"

CALLS = (
    "earlier-insertion",
    "unrelated-later",
    "later-cardinality",
    "equal-payloads",
    "reversed-order",
    "empty-completion",
    "pause-resume-absence",
    "stale-or-foreign",
    "pause-resume-success",
    "pause-resume-same-payload",
    "changed-selected-row",
    "empty-residual",
)

BARE_ALIASES = (
    "(= (lookup ",
    "(= (concatenate ",
    "(= (concatenate-head ",
    "(= (first-success ",
    "(= (complete-scan ",
    "(= (at-most-one ",
    "(= (plan-id ",
    "(= (pause-after-one ",
    "(= (resume-validated ",
)


class GateFailure(RuntimeError):
    pass


def run_petta(source: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(CETTA), "--lang", "petta", str(source)],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )


def scenario_lines(rendered: str) -> list[str]:
    lines = [line for line in rendered.splitlines() if line != ""]
    if not lines or lines[0] != "true":
        raise GateFailure(f"import did not return true before the controls: {rendered!r}")
    body = lines[1:]
    if any(not line.startswith("(scenario-") for line in body):
        raise GateFailure(f"a control result is not an evaluated scenario: {body!r}")
    return body


def assert_semantics(lines: list[str]) -> None:
    by_name = {}
    for line in lines:
        head = line.split(" ", 1)[0][1:]
        by_name[head] = line
    success = by_name["scenario-pause-resume-success"]
    if "gslt:ordered:residual-done (gslt:ordered:occ id-1 seven)" not in success:
        raise GateFailure("retained success did not close over the first occurrence")
    if "(gslt:ordered:occ id-1 seven) (gslt:ordered:occ id-8 other) false" not in success:
        raise GateFailure("retained success resumed the next candidate")
    same = by_name["scenario-pause-resume-same-payload"]
    if (
        "(gslt:ordered:occ id-1 seven) (gslt:ordered:occ id-2 seven) true false false)"
        not in same
    ):
        raise GateFailure("same payload did not keep the first identity")
    changed = by_name["scenario-changed-selected-row"]
    if " refused " not in f" {changed} ":
        raise GateFailure("changed selected row was not refused")
    if not changed.endswith(" true false)"):
        raise GateFailure("changed selected row did not record the failed read check")
    empty = by_name["scenario-empty-residual"]
    if empty != "(scenario-empty-residual () (gslt:ordered:residual-done none) none none true)":
        raise GateFailure("empty candidate list did not finish as a none residual")


def assert_library_boundary(library: str, controls: str) -> None:
    if "scenario-" in library or "!(" in library:
        raise GateFailure("library still executes or names a control")
    for alias in BARE_ALIASES:
        if alias in library:
            raise GateFailure(f"library keeps a bare alias: {alias}")
    if "!(import! &self ../../lib/gslt_ordered_decision.metta)" not in controls:
        raise GateFailure("controls do not import the library")
    for name in CALLS:
        if f"!({name})" not in controls:
            raise GateFailure(f"controls do not evaluate {name}")


def main() -> int:
    library = LIBRARY.read_text(encoding="utf-8")
    controls = CONTROLS.read_text(encoding="utf-8")
    expected = [line for line in EXPECTED.read_text(encoding="utf-8").splitlines() if line]
    assert_library_boundary(library, controls)
    if len(expected) != len(CALLS):
        raise GateFailure(f"expected {len(CALLS)} results, file has {len(expected)}")

    evaluated = run_petta(CONTROLS)
    if evaluated.returncode != 0:
        raise GateFailure(
            f"controls exited {evaluated.returncode}\n{evaluated.stdout}{evaluated.stderr}"
        )
    actual = scenario_lines(evaluated.stdout)
    if actual != expected:
        raise GateFailure(
            "evaluated controls differ from the recorded results:\n"
            + "\n".join(actual)
        )
    assert_semantics(actual)

    imported = run_petta(IMPORT_ONLY)
    if imported.returncode != 0:
        raise GateFailure(
            f"import-only exited {imported.returncode}\n{imported.stdout}{imported.stderr}"
        )
    import_text = imported.stdout
    if "scenario-" in import_text or import_text.strip() != "true":
        raise GateFailure(f"import-only produced control output: {import_text!r}")

    for name, line in zip(CALLS, actual):
        print(f"assert {name} evaluated {line}")
    print("assert retained-first-identity id-1 rather than id-8")
    print("assert same-payload payloads-equal identities-differ retained id-1")
    print("assert changed-selected-row refused")
    print("assert empty-residual residual-done none")
    print("assert import-only true")
    print("assert import-only scenario-lines 0")
    print(f"ordered-decision-gate controls={len(CALLS)} import-only=silent")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except GateFailure as failure:
        print(f"ordered-decision-gate failed: {failure}", file=sys.stderr)
        raise SystemExit(1)
