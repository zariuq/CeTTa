#!/usr/bin/env bash
set -eu

width=${1:-7}
iterations=${2:-8000}
rounds=${3:-1}

case $width in
    ''|*[!0-9]*)
        echo "support width must be a positive integer" >&2
        exit 2
        ;;
esac
case $iterations in
    ''|*[!0-9]*)
        echo "iteration count must be a nonnegative integer" >&2
        exit 2
        ;;
esac
case $rounds in
    ''|*[!0-9]*)
        echo "round count must be a positive integer" >&2
        exit 2
        ;;
esac
if [ "$width" -eq 0 ]; then
    echo "support width must be a positive integer" >&2
    exit 2
fi
if [ "$rounds" -eq 0 ]; then
    echo "round count must be a positive integer" >&2
    exit 2
fi

support=
index=0
while [ "$index" -lt "$width" ]; do
    support="$support \$support$index"
    index=$((index + 1))
done

printf '%s\n' \
    "; Finite-support reachability axis generated at width $width." \
    "; The live and empty clauses both survive an open-variable query." \
    "; Only the live clause recurs; its next argument is one unbound" \
    "; member of the previous rule frame." \
    "(= (support-walk \$n (live (node$support)))" \
    "   (if (== \$n 0) done" \
    "       (support-walk (- \$n 1) \$support0)))" \
    "(= (support-walk \$n (empty-lane (node$support))) (empty))" \
    ""

round=0
while [ "$round" -lt "$rounds" ]; do
    printf '%s\n' "!(support-walk $iterations \$proof)"
    round=$((round + 1))
done
