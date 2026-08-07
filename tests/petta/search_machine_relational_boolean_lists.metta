; Boolean operators are finite relations.  Open arguments enumerate through
; ordinary machine choice points in the same order as the PeTTa clauses.
!(collapse (if (or $x true) $x))
!(collapse (not $x))

; Relational member constructs an open list spine.  Relational size-atom then
; closes that spine at the demanded length, and the internal representation
; is flattened only at the answer boundary.
(= (two-member-list $items)
   (and (and (member a $items)
             (member b $items))
        (== (size-atom $items) 2)))
!(if (once (two-member-list $items)) $items)

; Source-level cons remains ordinary syntax/value; it is never confused with
; the internal open-list carrier.
!(quote (cons a ()))

; Canonical lambdas are suspended callable values when they cross a relation
; boundary.  Their bodies run only after application, never while the
; callable itself is evaluated as an argument.
(= (apply-once $f $x) ($f $x))
!(apply-once (|-> ($x) (+ $x 1)) 4)
!(collapse
   (apply-once
      (|-> ($x) (if (== $x 0) (empty) (/ 10 $x)))
      0))
!(collapse
   (apply-once
      (|-> ($x) (if (== $x 0) (empty) (/ 10 $x)))
      5))
!(maplist (|-> ($x) (+ $x 1)) (1 2 3))
(= (ignore-callable $f) kept)
!(ignore-callable (|-> ($x) (empty)))

; A false two-branch if contributes no result: Empty is the answer-bag zero.
!(collapse (if False 1))

; Fixed-arity intrinsics curry before the exact-arity host boundary.  Both
; the positive and negative alpha-equivalence cases exercise a partial
; comparator as an ordinary first-class callable.
!((=alpha $x) $y)
!((=alpha 1) 2)

; A known list constructor in an equation result is also an inverse
; relational pattern.  The resulting internal spine is removed recursively,
; including when it is nested in an ordinary tuple answer.
(= (split-last $last)
   (union-atom $prefix ($last)))
(= (cons-view (cons $head $tail))
   ((cons $head $tail) $head $tail))
!(let (split-last $last) (1 2 3) $last)
!(cons-view (1 2 3))
