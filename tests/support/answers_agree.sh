#!/bin/sh
# answers_agree.sh LANG PROGRAM EXPECTED CETTA [ARG...] < OUTPUT
#
# Succeeds when OUTPUT, read from standard input, answers as the file
# EXPECTED does: the same text, or each query's answers equal as a bag, the
# elements of a query's own collapse equal as a bag, and output written while
# the program runs in its order (compare_answers.py).  No order of a query's
# answers is promised, so none is compared.  CETTA [ARG...] is the command
# that ran PROGRAM; a PeTTa program runs once more with it, to frame its
# answers by query.
set -eu
if [ "$#" -lt 4 ]; then
	echo "usage: answers_agree.sh LANG PROGRAM EXPECTED CETTA [ARG...] < OUTPUT" >&2
	exit 2
fi
lang=$1
program=$2
expected=$3
shift 3
actual=$(mktemp "${TMPDIR:-/tmp}/cetta-answers.XXXXXX")
trap 'rm -f "$actual"' EXIT HUP INT TERM
cat >"$actual"
status=0
python3 "$(dirname "$0")/compare_answers.py" --lang "$lang" --program "$program" \
	--expected "$expected" --actual "$actual" --cetta "$@" || status=$?
exit "$status"
