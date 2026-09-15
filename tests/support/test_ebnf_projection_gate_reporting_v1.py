#!/usr/bin/env python3
"""Exercise the equivalence driver with failing and contradictory runtimes."""
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
evidence = Path(sys.argv[1]).resolve()
evidence.mkdir(parents=True, exist_ok=False)
fake = evidence / "runtime"
fake.write_text('''#!/usr/bin/env python3
import os, pathlib, sys
mode = os.environ["EBNF_GATE_PROBE"]
name = pathlib.Path(sys.argv[-1]).stem.removeprefix("case-")
verdict = f"(EbnfProjectionNativeEquivalenceV1 {name} true)"
if mode != "missing":
    print(verdict if mode != "false" else verdict.replace(" true)", " false)"))
if mode == "duplicate": print(verdict)
if mode == "contradictory": print(verdict.replace(" true)", " false)"))
if mode == "stderr": print("runtime diagnostic", file=sys.stderr)
if mode == "error": print("(Error unexpected failure)")
sys.exit(3 if mode == "nonzero" else 0)
''')
fake.chmod(0o755)
gate = root / "tests/support/check_ebnf_projection_native_equivalence_v1.sh"
admission = "langdef/petta/generated/plain_bnf_semantic_admission_v1.metta"
checks = 0
for mode in ("missing", "false", "duplicate", "contradictory", "stderr", "error", "nonzero", "success"):
    for size in ("standard", "extended"):
        # A fake success cannot satisfy the standard gate's mutation control.
        if mode == "success" and size == "standard":
            continue
        run = evidence / f"{size}-{mode}"
        env = dict(os.environ, EBNF_GATE_PROBE=mode)
        result = subprocess.run(["bash", str(gate), str(fake), str(run), admission, size],
                                cwd=root, env=env, text=True, capture_output=True)
        (evidence / f"{size}-{mode}.out").write_text(result.stdout)
        (evidence / f"{size}-{mode}.stderr").write_text(result.stderr)
        expected_success = mode == "success"
        if (result.returncode == 0) != expected_success:
            raise SystemExit(f"incorrect gate status for {size}/{mode}: {result.returncode}")
        if size == "extended":
            summary = "4 0" if expected_success else "0 4"
            if result.stdout.strip() != f"(EbnfProjectionNativeEquivalenceV1Summary {summary})":
                raise SystemExit(f"incorrect failure totals for {mode}: {result.stdout!r}")
        checks += 1
print(f"(EbnfProjectionGateReportingV1Summary {checks} 0)")
