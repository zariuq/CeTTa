#!/usr/bin/env sh
# The laws of the compiled presentations, over checked-in programs:
#   - for every presentation P and program F, reading P's printing of F's
#     atoms gives F's atoms back (the canonical view numbers each
#     statement's variables by identity);
#   - the generated S-expression reader reads F as the standard reader does;
#   - a conversion keeps every comment line;
#   - each program runs from its C-, Python- and Rholang-style texts as from
#     its standard text;
#   - the printings of the programs that show the looks are the checked-in
#     ones (a change of a printer's output is a deliberate change), and those
#     programs give their expected output;
#   - every printing is clean under the lint of looks, and the lint reports
#     each look a position's role does not admit;
#   - earlier versions stay readable: the frozen C and Rholang styles 1.0
#     print and read their checked-in 1.0 texts, and the Python style 1.1
#     reads its 1.0 texts as the same atoms;
#   - the run-time route reads soft keywords as the compiled reader does;
#   - the refusals: no leading ! in C and Python style, only the count 0 in
#     a Rholang-style count clause, two stores in one select, versions this
#     cetta does not have.
# usage: check_presentation_laws.sh CETTA
set -eu
cetta=$1
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "$root/runtime/presentation-laws.XXXXXX")
trap 'rm -rf "$work"' EXIT INT TERM
programs="transform_rules transform_aggregates transform_conjunctions transform_once
  transform_store_effects let_star_patterns sort_values lambda_pattern_parameters
  eval_quoted_values superpose_value_items string_layout number_order closure_values
  case_default_retire memoize_register_recursion flow_loops flow_job_runner
  presentation_looks presentation_builtin_positions"
run() { "$cetta" --lang petta --profile extended "$@"; }
failures=0
checks=0
fail() { echo "FAIL $*" >&2; failures=$((failures + 1)); }
for f in $programs; do
  src="$root/tests/petta/$f.metta"
  run --print-syntax atoms "$src" > "$work/$f.native" 2>&1
  run --syntax sexpr --print-syntax atoms "$src" > "$work/$f.generated" 2>&1
  checks=$((checks + 1))
  cmp -s "$work/$f.native" "$work/$f.generated" ||
    fail "$f: the generated S-expression reader differs from the standard reader"
  for p in cstyle rhostyle pystyle sexpr; do
    run --print-syntax "$p" "$src" > "$work/$f.$p" 2>&1
    run --syntax "$p" --print-syntax atoms "$work/$f.$p" > "$work/$f.$p.atoms" 2>&1
    checks=$((checks + 1))
    cmp -s "$work/$f.native" "$work/$f.$p.atoms" || fail "$f: $p does not read back its printing"
  done
  # The printer writes each look where its position's role admits it, so
  # its printing is clean under the lint.
  for p in cstyle rhostyle pystyle; do
    checks=$((checks + 1))
    run --syntax "$p" --check-looks "$work/$f.$p" > "$work/$f.$p.lint" 2>&1 ||
      fail "$f: the lint reports looks in the $p printing: $(head -n 1 "$work/$f.$p.lint")"
  done
  c0=$(run --syntax sexpr --print-syntax metta "$src" | grep -c '^;' || true)
  run --syntax sexpr --print-syntax pystyle "$src" > "$work/$f.kept.py"
  c1=$(grep -c '^ *#' "$work/$f.kept.py" || true)
  checks=$((checks + 1))
  [ "$c0" = "$c1" ] || fail "$f: comments $c0 became $c1"
  (cd "$root" && run "$src" > "$work/$f.out.std" 2>&1 || true)
  for p in cstyle pystyle rhostyle; do
    (cd "$root" && run --syntax "$p" "$work/$f.$p" > "$work/$f.out.$p" 2>&1 || true)
    checks=$((checks + 1))
    cmp -s "$work/$f.out.std" "$work/$f.out.$p" || fail "$f: running the $p text differs"
  done
done
# The looks, checked in: each position's role decides how an expression is
# written (a call where it runs, data where it is held).
for f in presentation_looks flow_job_runner; do
  for p in cstyle pystyle rhostyle; do
    checks=$((checks + 1))
    cmp -s "$work/$f.$p" "$root/tests/petta/$f.$p.expected" ||
      fail "$f: the $p printing differs from tests/petta/$f.$p.expected"
  done
  # Earlier versions: the frozen 1.0 printers print the checked-in 1.0 texts
  # and read them as the program's atoms; Python style 1.1 reads its 1.0 text.
  for p in cstyle rhostyle; do
    run --print-syntax "$p@1.0" "$root/tests/petta/$f.metta" > "$work/$f.$p-1.0" 2>&1
    checks=$((checks + 1))
    cmp -s "$work/$f.$p-1.0" "$root/tests/petta/$f.$p-1.0.expected" ||
      fail "$f: the $p 1.0 printing differs from tests/petta/$f.$p-1.0.expected"
    run --syntax "$p@1.0" --print-syntax atoms "$root/tests/petta/$f.$p-1.0.expected" > "$work/$f.$p-1.0.atoms" 2>&1
    checks=$((checks + 1))
    cmp -s "$work/$f.native" "$work/$f.$p-1.0.atoms" || fail "$f: $p@1.0 does not read its 1.0 text"
  done
  run --syntax pystyle --print-syntax atoms "$root/tests/petta/$f.pystyle-1.0.expected" > "$work/$f.pystyle-1.0.atoms" 2>&1
  checks=$((checks + 1))
  cmp -s "$work/$f.native" "$work/$f.pystyle-1.0.atoms" || fail "$f: pystyle 1.1 does not read its 1.0 text"
done
# The programs written for the presentations give their expected output.
for f in flow_loops flow_job_runner presentation_looks presentation_builtin_positions; do
  checks=$((checks + 1))
  cmp -s "$work/$f.out.std" "$root/tests/petta/$f.expected" || fail "$f: output differs from its .expected"
done
# Layout in both routes: the run-time GLL route (layout resolved on the text)
# reads Python-style text as the compiled reader (layout resolved on the
# tokens) does; keywords are written backquoted, as the run-time route reads.
for f in transform_rules flow_loops case_default_retire presentation_looks; do
  src="$root/tests/petta/$f.metta"
  run --print-hard-keywords --print-syntax pystyle "$src" > "$work/$f.hard.py" 2>&1
  run --syntax pystyle-gll --print-syntax sexpr "$work/$f.hard.py" > "$work/$f.gll.sx" 2>&1
  run --syntax pystyle --print-syntax sexpr "$work/$f.hard.py" > "$work/$f.compiled.sx" 2>&1
  checks=$((checks + 1))
  cmp -s "$work/$f.gll.sx" "$work/$f.compiled.sx" && [ -s "$work/$f.compiled.sx" ] ||
    fail "$f: the run-time route reads the Python-style text differently"
done
# Soft keywords in both routes: a keyword is a name wherever the grammar
# cannot take the keyword (the soft-keyword printings of the programs above,
# and texts that use keywords as names).
printf 'f(x) = 1;\nall(f(x));\nread(f(x));\natom(edge(a, b));\nmatch &self { edge{$x, $y} => once($x) };\ntransform &self { edge{$x, $y}, `read`{$y} => take($x) };\nfor ($i of [all, once]) { println!($i) };\nof(1) = from;\nin(of(2));\ng(*or{a}, 1 - all(b));\n' > "$work/soft.cm"
printf 'def f($x):\n    all(read($x))\n    once($x)\natom(edge(a, b))\nfor $i of [all, once]:\n    println!(take($i))\n    collect($i)\ng(*or{a}, 1 - all(b))\n' > "$work/soft.py"
for t in cstyle:soft.cm cstyle:transform_rules.cstyle cstyle:case_default_retire.cstyle \
         cstyle:presentation_looks.cstyle pystyle:soft.py pystyle:transform_rules.pystyle \
         pystyle:case_default_retire.pystyle pystyle:presentation_looks.pystyle; do
  p=${t%%:*}; f=${t#*:}
  run --syntax "$p" --print-syntax atoms "$work/$f" > "$work/$f.c.atoms" 2>&1
  run --syntax "$p-gll" --print-syntax atoms "$work/$f" > "$work/$f.g.atoms" 2>&1
  checks=$((checks + 1))
  cmp -s "$work/$f.c.atoms" "$work/$f.g.atoms" && [ -s "$work/$f.c.atoms" ] ||
    fail "$f: the run-time route reads soft keywords differently from the compiled reader"
done
# A rejection in a text with layout is reported where it is in the text.
printf 'x(1)\ny(2) )\n' > "$work/reject.py"
checks=$((checks + 1))
# (a pipe: the run-time reader writes its diagnosis to /dev/stderr itself)
{ run --syntax pystyle-gll --print-syntax atoms "$work/reject.py" 2>&1 || true; } | cat > "$work/reject.out"
grep -q 'ParserRejectedV1 10 10 ' "$work/reject.out" ||
  fail "the run-time route reports a rejection away from its place: $(head -c 120 "$work/reject.out")"
# Every look reads anywhere: a call and data denote the same atom.
printf 'f(a, g(b));\nf{a, g{b}};\nf{a, g(b)};\n' > "$work/looks.cm"
run --syntax cstyle --print-syntax atoms "$work/looks.cm" > "$work/looks.atoms" 2>&1
checks=$((checks + 1))
[ "$(sort -u "$work/looks.atoms" | grep -c '')" = 1 ] || fail "the call and data looks read as different atoms"
printf 'f(a, g(b));\nf[a, g[b]];\nf[a, g(b)];\n' > "$work/looks-1.0.cm"
run --syntax cstyle@1.0 --print-syntax atoms "$work/looks-1.0.cm" > "$work/looks-1.0.atoms" 2>&1
checks=$((checks + 1))
cmp -s "$work/looks.atoms" "$work/looks-1.0.atoms" || fail "the 1.0 data look reads as other atoms than 2.0's"
# The explicit read is the quoted receipt; absence is the count 0, attached
# to the receipt before it as a guard is, or on its own.
printf 'select { @{ p[$x] } <<- &s & where count(q[$x] <<- &s) == 0 => r($x) };\nselect { p[$x] <<- &s where count(q[$x] <<- &s) == 0 => r($x) };\nselect { p[$x] <<- &s where count(q[$x] <<- &s) == 0 & t[$x] <- &s => r($x) };\n' > "$work/reads.rhostyle"
run --syntax rhostyle --print-syntax metta "$work/reads.rhostyle" > "$work/reads.metta" 2>&1
checks=$((checks + 1))
grep -q '(transform &s (, (read (p $x)) (absent (q $x))) (r $x))' "$work/reads.metta" &&
  grep -q '(transform &s (, (p $x) (absent (q $x))) (r $x))' "$work/reads.metta" &&
  grep -q '(transform &s (, (p $x) (absent (q $x)) (take (t $x))) (r $x))' "$work/reads.metta" ||
  fail "the quoted receipt or a count clause reads as other atoms"
# Why 2.0 is a new major version: these 1.0 texts read differently or not at all.
printf 'select { p[$x] <<- &s where count($x) == 0 => r($x) };\n' > "$work/count-guard.rhostyle"
run --syntax rhostyle@1.0 --print-syntax metta "$work/count-guard.rhostyle" > "$work/count-guard.metta" 2>&1
checks=$((checks + 1))
grep -q ' (== (count $x) 0))) ' "$work/count-guard.metta" || fail "rhostyle@1.0 does not read its guard calling count"
printf 'if $c { a } else { b };\n' > "$work/if.cm"
run --syntax cstyle@1.0 --print-syntax metta "$work/if.cm" > "$work/if.metta" 2>&1
checks=$((checks + 1))
grep -q '(if $c a b)' "$work/if.metta" || fail "cstyle@1.0 does not read its if"
# Refusals.
refused() {
  checks=$((checks + 1))
  if run --syntax "$1" --print-syntax metta "$2" > /dev/null 2>&1; then fail "$3"; fi
}
refused rhostyle "$work/count-guard.rhostyle" "rhostyle 2.0 read a 1.0 guard calling count"
refused cstyle "$work/if.cm" "cstyle 2.0 read an if without parentheses"
refused cstyle "$work/looks-1.0.cm" "cstyle 2.0 read the 1.0 data look f[a]"
printf 'select { a($x) <- &s & b($y) <<- &t => &s!(c) };\n' > "$work/two_stores.rhostyle"
refused rhostyle "$work/two_stores.rhostyle" "a select over two stores was read"
printf 'select { where count(q[$x] <<- &s) == 1 => r };\n' > "$work/count_one.rhostyle"
refused rhostyle "$work/count_one.rhostyle" "a count other than 0 was read"
printf 'select { p[$x] <<- &s where count(q[$x] <<- &s) == 1 => r };\n' > "$work/count_one_attached.rhostyle"
refused rhostyle "$work/count_one_attached.rhostyle" "an attached count other than 0 was read"
printf '!f(x);\n' > "$work/bang.cm"
refused cstyle "$work/bang.cm" "a C-style statement with a leading ! was read"
printf '!f(x)\n' > "$work/bang.py"
refused pystyle "$work/bang.py" "a Python-style statement with a leading ! was read"
# A fact and the statement that adds the same atom: one space, but the
# statement answers true, which prints.
printf 'atom color{red};\nadd-atom(&self, color{green});\ncollapse(match &self { color{$c} => $c });\n' > "$work/facts.cm"
run --syntax cstyle "$work/facts.cm" > "$work/facts.out" 2>&1
checks=$((checks + 1))
[ "$(cat "$work/facts.out")" = "$(printf 'true\n(red green)')" ] ||
  fail "a fact and add-atom(&self, e) differ otherwise than by the statement's printed answer"
# The lint of looks: a text whose looks mislead, and the same text honest.
printf 'steps($xs) = $xs;\nsteps([build, check, deploy]);\nadd-atom(&self, job(j1, 2));\njob{j2, 0};\ncollapse(superpose(twice(1)(twice(2))));\nshow-held : Atom -> Atom;\nshow-held(twice(3));\nfor ($x in [a, b]) { println!($x) };\n' > "$work/lint-bad.cm"
printf 'steps($xs) = $xs;\nsteps(build(check, deploy));\nadd-atom(&self, job{j1, 2});\njob(j2, 0);\ncollapse(superpose([twice(1), twice(2)]));\nshow-held : Atom -> Atom;\nshow-held(twice{3});\nfor ($x of [a, b]) { println!($x) };\n' > "$work/lint-good.cm"
checks=$((checks + 1))
if run --syntax cstyle --check-looks "$work/lint-bad.cm" > "$work/lint-bad.out" 2>&1; then
  fail "the lint passed a text with misleading looks"
elif [ "$(grep -c 'is written as' "$work/lint-bad.out")" != 6 ]; then
  fail "the lint did not report the six misleading looks: $(cat "$work/lint-bad.out")"
fi
checks=$((checks + 1))
run --syntax cstyle --check-looks "$work/lint-good.cm" > "$work/lint-good.out" 2>&1 ||
  fail "the lint reported honest looks: $(cat "$work/lint-good.out")"
# The lint reads a compiled presentation; asked of anything else it refuses
# (and never runs the program).
checks=$((checks + 1))
if run --check-looks "$root/tests/petta/flow_loops.metta" > /dev/null 2>&1; then
  fail "--check-looks without a compiled presentation was not refused"
fi
# Versions: NAME@MAJOR[.MINOR] reads with the compiled version that reads
# every text of it (the same MAJOR, a MINOR at least as high).
for v in 1 1.0; do
  run --syntax "cstyle@$v" --print-syntax atoms "$work/looks-1.0.cm" > "$work/looks.v.atoms" 2>&1
  checks=$((checks + 1))
  cmp -s "$work/looks.atoms" "$work/looks.v.atoms" || fail "cstyle@$v did not read as cstyle 1.0"
done
for v in 2 2.0; do
  run --syntax "cstyle@$v" --print-syntax atoms "$work/looks.cm" > "$work/looks.v.atoms" 2>&1
  checks=$((checks + 1))
  cmp -s "$work/looks.atoms" "$work/looks.v.atoms" || fail "cstyle@$v did not read as cstyle 2.0"
done
refused cstyle@1.1 "$work/looks-1.0.cm" "a later minor version was accepted"
refused cstyle@3.0 "$work/looks.cm" "another major version was accepted"
printf 'f(a, g{b})\n' > "$work/ok.py"
for v in 1 1.0 1.1; do
  checks=$((checks + 1))
  run --syntax "pystyle@$v" --print-syntax atoms "$work/ok.py" > /dev/null 2>&1 || fail "pystyle@$v was refused"
done
refused pystyle@1.2 "$work/ok.py" "a later Python-style minor version was accepted"
checks=$((checks + 1))
if run --syntax cstyle --print-syntax rhostyle@0.9 "$work/looks.cm" > /dev/null 2>&1; then
  fail "printing in another major version was accepted"
fi
if [ "$failures" -ne 0 ]; then
  echo "presentation laws: $failures of $checks checks failed" >&2
  exit 1
fi
echo "PASS: presentation laws, $checks checks"
