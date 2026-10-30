#!/usr/bin/env python3
"""Compare a generated runtime project and its C export with direct compilation."""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
FLAGS = ["-std=c11", "-O2", "-Wall", "-Werror"]
SPECIAL = ["-std=c11", "-O2", "-Wall", "-Wno-unused-function", "-DSPECIAL=7"]
SOURCES = ["prefix.c", "special.c", "suffix.c", "uncited.c"]


def run(command, cwd):
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True)
    if result.returncode:
        raise AssertionError((command, result.returncode, result.stdout, result.stderr))
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cesta", required=True, type=Path)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--lto", action="store_true")
    parser.add_argument("--pgo", action="store_true")
    args = parser.parse_args()
    optimize = ['-flto=auto', '-ffat-lto-objects'] if args.lto else []
    flags = FLAGS + optimize
    special = SPECIAL + optimize
    cesta = str(args.cesta.resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="flags-", dir=args.output.resolve()))
    files = {
        "prefix.c": '#include <stdio.h>\n#ifdef SPECIAL\n#error flags escaped their source group\n#endif\n'
            'int special(void); int suffix(void);\n'
            'int main(void) { printf("%d %d\\n", special(), suffix()); return 0; }\n',
        "special.c": '#ifndef SPECIAL\n#error missing source flags\n#endif\n'
            'static int unused(void) { return 0; }\nint special(void) { return SPECIAL; }\n',
        "suffix.c": '#ifdef SPECIAL\n#error flags escaped their source group\n#endif\n'
            'int suffix(void) { return 9; }\n',
        "uncited.c": 'int exported_without_static_call(void) { return 42; }\n',
        "baked.c": 'int baked_data = 11;\n',
    }
    for name, source in files.items():
        (work / name).write_text(source)
    if args.pgo:
        instrumented = []
        for source in SOURCES + ['baked.c']:
            stem = source.removesuffix('.c')
            obj = stem + '.generate.o'
            run([args.cc, *(special if source == 'special.c' else flags), '-fprofile-generate',
                 '-dumpbase', './' + stem + '.generate', '-c', source, '-o', obj], work)
            instrumented.append(obj)
        run([args.cc, *flags, '-fprofile-generate', *instrumented, '-o', 'instrumented'], work)
        run([str(work / 'instrumented')], work)
        for source in SOURCES + ['baked.c']:
            stem = source.removesuffix('.c')
            counts = work / (stem + '.generate.gcda')
            if counts.exists():
                shutil.copy2(counts, work / (stem + '.gcda'))
        flags += ['-fprofile-use', '-Wno-missing-profile']
        special += ['-fprofile-use', '-Wno-missing-profile']
    objects = []
    for source in SOURCES + ["baked.c"]:
        obj = source.removesuffix(".c") + ".o"
        extra = (['-dumpbase', './' + source.removesuffix('.c')] if args.pgo else []) + \
                (['-frandom-seed=' + obj] if args.lto else [])
        run([args.cc, *(special if source == "special.c" else flags), *extra, "-c", source, "-o", obj], work)
        objects.append(obj)
    run([args.cc, *flags, "-Wl,--export-dynamic", "-o", "direct", *objects], work)
    values = {
        "NATIVE_CC": args.cc, "CPPFLAGS": "", "CFLAGS": " ".join(flags), "LDFLAGS": "",
        "SRC": " ".join(SOURCES), "SECP256K1_SRC": "special.c",
        "SECP256K1_CFLAGS": " ".join(special), "PARSER_BAKED_OBJ": "baked.o", "BRIDGE_DEPS": "",
        "SOURCE_OBJECTS": ' '.join(objects[:-1]), 'CETTA_LTO': str(int(args.lto)),
        'CETTA_PGO': 'use' if args.pgo else '',
    }
    manifest = work / "make.vars"
    manifest.write_text("".join(f"{name}={value}\n" for name, value in values.items()))
    run([sys.executable, str(ROOT / "tools/cesta_runtime_project.py"), "--vars", str(manifest),
         "--out", "out", "--executable", "from-project", "--output", "cetta.project"], work)
    run([cesta, "build", "-C", str(work), "-f", "cetta.project", "-j", "2", "cetta"], work)
    reference = (work / "direct").read_bytes()
    assert (work / "from-project").read_bytes() == reference
    assert run([str(work / "from-project")], work) == "7 9\n"

    # Omitting the unit reached only through an exported native symbol leaves
    # runtime output intact; binary identity must detect the lost export.
    original = (work / "cetta.project").read_text()
    mutant = re.sub(r'\(source "uncited.c"\s+\(flags[^)]*\)\)', '', original, count=1)
    (work / "cetta.project").write_text(mutant)
    run([cesta, "build", "-C", str(work), "-f", "cetta.project", "cetta"], work)
    assert run([str(work / "from-project")], work) == "7 9\n"
    assert (work / "from-project").read_bytes() != reference
    (work / "cetta.project").write_text(original)

    run([cesta, "build", "-C", str(work), "-f", "cetta.project", "--export-c", "export", "cetta"], work)
    run(["make", "-j2", f"CC={args.cc}"], work / "export")
    assert (work / "export/from-project").read_bytes() == reference
    assert run([str(work / "export/from-project")], work) == "7 9\n"
    receipt = {"passed": True, "sources": SOURCES, "binary_sha256": hashlib.sha256(reference).hexdigest(),
               "missing_export_mutation_detected": True, "export_matches": True, 'lto': args.lto, 'pgo': args.pgo}
    (work / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("PASS: source flags, object order and unreferenced exports survive project and plain-C builds")


if __name__ == "__main__":
    main()
