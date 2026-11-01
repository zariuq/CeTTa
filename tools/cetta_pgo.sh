#!/bin/sh
# Profile-guided build of the runtime.
#
#   cetta_pgo.sh OUTPUT [MAKE-VARIABLE=VALUE ...]
#
# Builds an instrumented executable (CETTA_PGO=generate), runs it over the
# training programs, moves the counts it wrote next to its objects to the
# names of the profile-using objects, and builds OUTPUT with CETTA_PGO=use.
# Training and held-out inputs are declared in cetta_pgo_workloads.json.
# Every training execution must match a non-instrumented reference. Each run
# has its own object/profile namespace; previous runs are left intact.
set -eu
out=$1; shift
jobs=${PGO_JOBS:-2} limit=${PGO_TIMEOUT:-60}
manifest=${PGO_MANIFEST:-tools/cetta_pgo_workloads.json}
python3 tools/cetta_pgo_train.py --manifest "$manifest"
run=$(python3 -c 'import time; print(format(time.time_ns(),"x"))')
record=runtime/pgo/run-$run
mkdir -p "$record"
tag() {
    text=$(make -s --no-print-directory "$@" --eval 'pgo-print-tag: ; @echo CESTA_PGO_TAG=$(BUILD_OBJ_TAG)' pgo-print-tag) || return
    printf '%s\n' "$text" | sed -n 's/^CESTA_PGO_TAG=//p'
}
gen_tag=$(tag "$@" CETTA_PGO=generate CETTA_PGO_RUN="$run")
use_tag=$(tag "$@" CETTA_PGO=use CETTA_PGO_RUN="$run")
test -n "$gen_tag" && test -n "$use_tag"
gen_bin=$record/instrumented
reference=$record/reference
python3 - "$record/build.json" "$run" "$manifest" "$out" "$gen_tag" "$use_tag" "$@" <<'PY'
import hashlib,json,os,subprocess,sys
from pathlib import Path
output,run,manifest,binary,generated,used,*arguments=sys.argv[1:]
receipt=dict(format='CettaPGOBuildV1',run=run,manifest=manifest,output=binary,
             generated=generated,used=used,make_arguments=arguments,
             make_environment={key:os.environ.get(key) for key in ('MAKEFLAGS','MAKEOVERRIDES','CESTA_NATIVE_CC','CESTA_BUILD','CESTA')})
bundle=Path(os.environ.get('CESTA_SRC_DIR','vendor/cesta'))/'SOURCE.json'
if bundle.exists():receipt['cesta_source']=json.loads(bundle.read_text())['source_sha256']
receipt['build_inputs']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in
    [Path('Makefile'),Path('make/build_object_tag.mk'),*Path('tools').glob('cetta_pgo*'),Path('tools/cesta_cc.sh')]
    if p.is_file()}
try:
    head=subprocess.run(['git','rev-parse','HEAD'],capture_output=True,text=True)
    if head.returncode==0:receipt['base_commit']=head.stdout.strip()
except OSError:pass
Path(output).write_text(json.dumps(receipt,indent=2)+'\n')
PY

make -s --no-print-directory -j"$jobs" "$@" CETTA_PGO= CETTA_PGO_RUN= BIN="$reference" "$reference"

echo "pgo: instrumented build ($gen_tag)" >&2
make -s --no-print-directory -j"$jobs" "$@" CETTA_PGO=generate CETTA_PGO_RUN="$run" BIN="$gen_bin" "$gen_bin"

echo "pgo: training" >&2
python3 tools/cetta_pgo_train.py --manifest "$manifest" --binary "$gen_bin" \
    --reference "$reference" --timeout "$limit" --output "$record/training.json"

echo "pgo: moving counts to $use_tag" >&2
python3 tools/cetta_pgo_profiles.py "$gen_tag" "$use_tag" "$record/profiles.json"

echo "pgo: profile-using build" >&2
make -s --no-print-directory -j"$jobs" "$@" CETTA_PGO=use CETTA_PGO_RUN="$run" BIN="$out" "$out"
printf 'PGO receipt: %s\n' "$record"
