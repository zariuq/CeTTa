from pathlib import Path
import argparse
import json
import subprocess
import sys

parser = argparse.ArgumentParser(description="Check native proof and seal examples in both MeTTa dialects.")
parser.add_argument("output", type=Path)
parser.add_argument("binary", nargs="?", default="cetta", type=Path)
parser.add_argument("--group", choices=("megalodon", "seal", "integration"), action="append")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
tree = root
binary = tree / args.binary
out = root / args.output
out.mkdir(exist_ok=False)
results = []
groups = {"megalodon": "tests/megalodon/metta", "seal": "tests/seal",
          "integration": "tests/megalodon/integration"}
sources = [source for group in (args.group or list(groups))
           for source in sorted((tree / groups[group]).glob("*.metta"))]
for source in sources:
    for lane in ("he", "petta"):
        stem = f"{source.stem}.{lane}"
        case_dir = out / stem
        case_dir.mkdir()
        save_dir = str(case_dir) + "/"
        program = case_dir / source.name
        program.write_text(source.read_text().replace("runtime/megalodon-test/", save_dir)
                           .replace("../../../lib/lib_tptp.metta", str(tree / "lib/lib_tptp.metta")))
        profile = ["--profile", "extended"] if "integration" in source.parts else []
        run = subprocess.run([str(binary), "--lang", lane, *profile, str(program)], cwd=tree,
                             text=True, capture_output=True, timeout=180)
        expected = source.with_suffix(f".{lane}.expected").read_text().replace("runtime/megalodon-test/", save_dir)
        (out / f"{stem}.out").write_text(run.stdout)
        (out / f"{stem}.err").write_text(run.stderr)
        passed = run.returncode == 0 and run.stdout.rstrip() == expected.rstrip() and not run.stderr
        results.append({"case": stem, "returncode": run.returncode, "passed": passed})
        print("PASS" if passed else "FAIL", stem, flush=True)
(out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
sys.exit(0 if all(row["passed"] for row in results) else 1)
