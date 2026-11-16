#!/bin/sh
# run_fixtures.sh - the fixture harness of the rho and tcalc lanes.
#
#   sh tests/run_fixtures.sh LABEL RUNNER [SETTING ...] -- FILE ...
#
# Each FILE runs through RUNNER, a command that takes [PROFILE] FILE, and its
# output (standard output and error together) must equal its .expected file:
# FILE with its suffix replaced.  A fixture may name its profile in a comment
# on its first line, `; profile: SETTINGS` (`// profile:` in a .rho or .mrho
# fixture, `% profile:` in a .chr one); RUNNER then gets the profile before the
# file.  Each SETTING, an environment assignment NAME=VALUE, is one more run of
# every fixture with it set; with none, each fixture runs once.
#
# Prints each failing run with the head of its difference, then a summary.
# Exits 1 when a run failed.
set -u
label=$1
runner=$2
shift 2
settings=""
while [ $# -gt 0 ] && [ "$1" != "--" ]; do
    settings="$settings $1"
    shift
done
[ $# -gt 0 ] && shift

failures=0
count=0
for f in "$@"; do
    [ -f "$f" ] || continue
    count=$((count + 1))
    profile=$(sed -n '1s/^\(;\|\/\/\|%\) profile: //p' "$f")
    expected="${f%.*}.expected"
    for setting in ${settings:-unset}; do
        [ "$setting" = unset ] && setting=""
        if [ -n "$profile" ]; then
            out=$(env $setting $runner "$profile" "$f" 2>&1)
        else
            out=$(env $setting $runner "$f" 2>&1)
        fi
        if [ "$out" != "$(cat "$expected")" ]; then
            echo "FAIL: $f ($label${setting:+, $setting})"
            printf '%s\n' "$out" | diff "$expected" - | head -20
            failures=$((failures + 1))
        fi
    done
done
if [ $failures -ne 0 ]; then
    echo "FAIL: $failures runs of $count fixtures ($label)"
    exit 1
fi
echo "PASS: $count fixtures ($label${settings:+;$settings})"
