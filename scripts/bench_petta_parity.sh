#!/bin/bash
set -u

CETTA_DIR=/home/zar/claude/c-projects/CeTTa-lang-petta
PETTA_DIR=/home/zar/claude/hyperon/PeTTa
EXAMPLES_DIR=$PETTA_DIR/examples
REASONABLE=$CETTA_DIR/tests/support/petta_upstream_reasonable_examples.txt
STAMP=$(date +%Y%m%dT%H%M%SZ)
OUT_DIR=$CETTA_DIR/bench_results
mkdir -p "$OUT_DIR"
CSV=$OUT_DIR/petta_parity_$STAMP.csv
LOG=$OUT_DIR/petta_parity_$STAMP.log
TIMEOUT=${TIMEOUT:-20}

cd "$CETTA_DIR"
ulimit -v 10485760

parse_wall() {
  awk '/Elapsed \(wall clock\)/ {
    t=$NF; gsub(/[^0-9:.]/,"",t);
    n=split(t,a,":");
    if (n==3) print a[1]*3600+a[2]*60+a[3];
    else if (n==2) print a[1]*60+a[2];
    else print a[1];
  }' "$1"
}
parse_rss() {
  awk '/Maximum resident set size/ {print $NF}' "$1"
}

echo "file,cetta_wall_s,cetta_rss_kb,cetta_status,petta_wall_s,petta_rss_kb,petta_status" > "$CSV"

while IFS= read -r base; do
  case "$base" in ''|\#*) continue ;; esac
  abs="$EXAMPLES_DIR/$base"
  if [ ! -f "$abs" ]; then
    echo "SKIP missing: $base" | tee -a "$LOG"
    continue
  fi

  cerr=$(mktemp)
  c_t0=$(date +%s.%N)
  /usr/bin/time -v -o "$cerr" timeout "$TIMEOUT" \
    ./cetta --lang petta "$abs" </dev/null >/dev/null 2>&1
  cstatus=$?
  c_t1=$(date +%s.%N)
  cwall=$(awk -v a="$c_t0" -v b="$c_t1" 'BEGIN{printf "%.4f", b-a}')
  crss=$(parse_rss "$cerr")
  rm -f "$cerr"

  perr=$(mktemp)
  p_t0=$(date +%s.%N)
  (cd "$PETTA_DIR" && /usr/bin/time -v -o "$perr" timeout "$TIMEOUT" \
    sh ./run.sh "$abs" --silent </dev/null >/dev/null 2>&1)
  pstatus=$?
  p_t1=$(date +%s.%N)
  pwall=$(awk -v a="$p_t0" -v b="$p_t1" 'BEGIN{printf "%.4f", b-a}')
  prss=$(parse_rss "$perr")
  rm -f "$perr"

  printf "%s,%s,%s,%s,%s,%s,%s\n" \
    "$base" "$cwall" "$crss" "$cstatus" "$pwall" "$prss" "$pstatus" >> "$CSV"
  printf "%-40s cetta=%6ss / %8s KB (exit %d)  petta=%6ss / %8s KB (exit %d)\n" \
    "$base" "$cwall" "$crss" "$cstatus" "$pwall" "$prss" "$pstatus" | tee -a "$LOG"
done < "$REASONABLE"

echo "--- done ---" | tee -a "$LOG"
echo "csv: $CSV" | tee -a "$LOG"
