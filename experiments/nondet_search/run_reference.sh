#!/usr/bin/env bash
# Run each program on a frozen CeTTa binary with the compiled open tier on
# (default) and off (CETTA_OPEN_EQUATIONS_REFERENCE=1), printing wall time,
# machine transitions and the answer.  Usage: run_reference.sh BIN FILE...
bin=$1; shift
for f in "$@"; do
  for mode in open reference; do
    if [ "$mode" = reference ]; then ref=1; else ref=0; fi
    out=$( (CETTA_OPEN_EQUATIONS_REFERENCE=$ref CETTA_PETTA_MACHINE_STATS=1 \
      /usr/bin/time -f "__TIME__ %e" timeout 120 "$bin" --lang petta "$f") 2>&1 )
    t=$(printf '%s\n' "$out" | grep __TIME__ | awk '{print $2}')
    tr=$(printf '%s\n' "$out" | grep -o 'PETTA_MACHINE_STATS transitions=[0-9]*' | head -1 | sed 's/.*=//')
    ans=$(printf '%s\n' "$out" | grep -v -e __TIME__ -e PETTA_MACHINE_STATS -e '^Command' | tail -1)
    printf '%s\t%s\t%s\t%s\t%s\n' "$(basename "$f" .metta)" "$mode" "$t" "$tr" "$ans"
  done
done
