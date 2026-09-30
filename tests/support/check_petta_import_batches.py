#!/usr/bin/env python3
"""Check owning import batches, solution-reference lifetimes and reloads."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def run(binary, source, expected, route, phase):
    env = dict(os.environ, CETTA_PETTA_SEARCH_MACHINE="1",
               CETTA_OPEN_EQUATIONS_REFERENCE=route)
    result = subprocess.run([str(binary), "--lang", "petta", str(source)],
                            env=env, capture_output=True, text=True, timeout=60)
    if result.returncode or result.stderr or result.stdout != expected:
        raise RuntimeError(f"import batch route={route} phase={phase}: "
                           f"rc={result.returncode}\n{result.stdout}\n{result.stderr}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--artifacts", type=Path, default=Path("runtime"))
    args = parser.parse_args()
    binary = args.binary.resolve()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    # Preserve failed-run evidence and generated loader caches for inspection.
    root = Path(tempfile.mkdtemp(prefix="petta-import-batches.",
                                 dir=args.artifacts)).resolve()
    large = "λ" * 100000
    rows = [f'(row {i} (nested {i} "q Ω"))' for i in range(1000)]
    rows[255:255] = ['(row 17 (nested 17 "q Ω"))'] * 3
    rows.insert(700, f'(huge text "{large}")')
    expected = ("true\ntrue\n1004\n1003\n"
                "((nested 17 \"q Ω\") (nested 17 \"q Ω\") "
                "(nested 17 \"q Ω\") (nested 17 \"q Ω\"))\n"
                "true\ntrue\n1004\ntrue\n1004\ntrue\n1004\n")
    program = f'''!(import! &self (library lib_import))
!(static-import! batch rows)
!(size-atom (collapse (get-atoms batch)))
!(size-atom (collapse (match batch (row $i $n) $i)))
!(collapse (match batch (row 17 $n) $n))
!(match batch (huge text $s) (== $s "{large}"))
!(static-import! batch rows)
!(size-atom (collapse (get-atoms batch)))
!(static-import! batch more)
!(size-atom (collapse (match batch (row $i $n) $i)))
!(static-import! batch rows)
!(size-atom (collapse (match batch (row $i $n) $i)))
'''
    variables = '''!(import! &self (library lib_import))
!(static-import! variables aliases)
!(size-atom (collapse (match variables (alias $x $x) $x)))
!(size-atom (collapse (match variables (alias $x $y)
    (let true (== $x $y) ok))))
!(let $xs (collapse (match variables (alias $x $x) $x))
    (== (car-atom $xs) (car-atom (cdr-atom $xs))))
'''
    for route in ("0", "1"):
        directory = root / route
        directory.mkdir()
        (directory / "rows.metta").write_text("\n".join(rows) + "\n")
        (directory / "more.metta").write_text('(row 1000 (nested 1000 "q Ω"))\n')
        (directory / "import.metta").write_text(program)
        (directory / "aliases.pl").write_text(
            ":- multifile variables/3.\n" +
            "variables(alias, X, X).\n" * 600)
        (directory / "variables.metta").write_text(variables)
        for phase in ("cold", "warm"):
            run(binary, directory / "import.metta", expected, route, phase)
            run(binary, directory / "variables.metta",
                "true\ntrue\n600\n600\nfalse\n", route, phase)
    print("PASS: import batches own nested/oversized rows, retain occurrences "
          "and isolate clause variables on cold and warm routes")


if __name__ == "__main__":
    main()
