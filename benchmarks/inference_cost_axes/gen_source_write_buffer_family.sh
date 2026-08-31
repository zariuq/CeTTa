#!/usr/bin/env bash
set -euo pipefail

mode=${1:?usage: gen_source_write_buffer_family.sh MODE ITERATIONS}
iterations=${2:?usage: gen_source_write_buffer_family.sh MODE ITERATIONS}

case "$mode" in
  late-rigid)
    printf '%s\n' \
      '(= (buffer-produce) actual)' \
      '(= (buffer-match (Late (Wrap $x) expected)) ok)' \
      '(= (buffer-run $n $open)' \
      '   (if (== $n 0)' \
      '       done' \
      '       (let $late (buffer-produce)' \
      '         (let $_ (collapse (buffer-match (Late $open $late)))' \
      '           (buffer-run (- $n 1) $open)))))'
    ;;
  deep-rigid)
    printf '%s\n' \
      '(= (buffer-produce) actual)' \
      '(= (buffer-match (Deep (Outer (Middle (Inner (Wrap $x)))) expected)) ok)' \
      '(= (buffer-run $n $open)' \
      '   (if (== $n 0)' \
      '       done' \
      '       (let $late (buffer-produce)' \
      '         (let $_ (collapse (buffer-match (Deep $open $late)))' \
      '           (buffer-run (- $n 1) $open)))))'
    ;;
  wide-rigid)
    printf '%s\n' \
      '(= (buffer-produce) actual)' \
      '(= (buffer-match (Wide (Pack $a $b $c $d $e $f $g) expected)) ok)' \
      '(= (buffer-run $n $open)' \
      '   (if (== $n 0)' \
      '       done' \
      '       (let $late (buffer-produce)' \
      '         (let $_ (collapse (buffer-match (Wide $open $late)))' \
      '           (buffer-run (- $n 1) $open)))))'
    ;;
  long-rigid-prefix)
    printf '%s\n' \
      '(= (buffer-produce) actual)' \
      '(= (buffer-match (Long (Wrap $x) a b c d e expected)) ok)' \
      '(= (buffer-run $n $open)' \
      '   (if (== $n 0)' \
      '       done' \
      '       (let $late (buffer-produce)' \
      '         (let $_ (collapse (buffer-match (Long $open a b c d e $late)))' \
      '           (buffer-run (- $n 1) $open)))))'
    ;;
  observer-flush)
    printf '%s\n' \
      '(= (buffer-match (Observe (Wrap $x) marker)) ok)' \
      '(= (buffer-run $n $open)' \
      '   (if (== $n 0)' \
      '       done' \
      '       (let $_ (collapse (buffer-match (Observe $open $open)))' \
      '         (buffer-run (- $n 1) $open))))'
    ;;
  success-flush)
    printf '%s\n' \
      '(= (buffer-match (Success (Wrap $x) marker)) (Result $x))' \
      '(= (buffer-run $n $open)' \
      '   (if (== $n 0)' \
      '       done' \
      '       (let $_ (buffer-match (Success $open marker))' \
      '         (buffer-run (- $n 1) $next))))'
    ;;
  prebound-decline)
    printf '%s\n' \
      '(= (buffer-match (Prebound $x $x marker)) ok)' \
      '(= (buffer-run $n $open)' \
      '   (if (== $n 0)' \
      '       done' \
      '       (let $_ (buffer-match (Prebound known $open marker))' \
      '         (buffer-run (- $n 1) $next))))'
    ;;
  *)
    printf 'unknown mode: %s\n' "$mode" >&2
    exit 2
    ;;
esac

printf '!(buffer-run %s $seed)\n' "$iterations"
