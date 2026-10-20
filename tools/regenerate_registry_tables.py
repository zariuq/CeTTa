"""Regenerate the checked-in counter and profile headers; normal builds need only C."""
import argparse
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cesta", default="cesta")
parser.add_argument("--check", action="store_true",
                    help="check generated headers without replacing them")
args = parser.parse_args()
destination = root / "src/generated"
with tempfile.TemporaryDirectory(prefix="registry-tables-", dir=destination) as temporary:
    generated = []
    for source, header in (("runtime_counters", "cetta_runtime_counters"),
                           ("runtime_counter_names", "cetta_runtime_counter_names"),
                           ("profiles", "cetta_profiles")):
        target = destination / (header + ".generated.h")
        staged = Path(temporary) / target.name
        subprocess.run([args.cesta, "export-header",
                        str(root / "lib" / (source + ".export")),
                        "-o", str(staged)], check=True)
        # Cesta separates declarations with blank lines and currently leaves
        # one additional blank line after the include guard. Keep its content
        # byte-for-byte while giving checked-in headers one final newline.
        staged.write_bytes(staged.read_bytes().rstrip(b"\r\n") + b"\n")
        generated.append((staged, target))
    # Validate every table before replacing any header.
    changed = [(staged, target) for staged, target in generated
               if not target.exists() or staged.read_bytes() != target.read_bytes()]
    for staged, target in changed:
        if args.check:
            print("stale generated header:", target.relative_to(root))
        else:
            staged.replace(target)
    if args.check and changed:
        raise SystemExit(1)
