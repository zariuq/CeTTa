; Prime Need heap lifetime across evaluator collection.
;
; Algebraic contract:
;   force(delay(e)) after unrelated strict work has the same branch-local
;   observations as force(delay(e)) immediately.
;
; Ownership contract:
;   the persistent Need heap belongs to the top-level evaluation episode,
;   never to the resettable evaluator scratch arena.  The non-tail work below
;   is the same generated-frame shape used by the explicit-stack GC gate; a
;   tiny budget forces collections while $promise remains a live outer root.

(= (prime-need:gc-work $n)
   (if (< $n 1)
       0
       (+ (superpose (1))
          (prime-need:gc-work (- $n 1)))))

!(assertEqualToResult
  (let $promise (delay (superpose (A B)))
    (let $work (prime-need:gc-work (force (delay 1600)))
      (if (== $work 1600)
          (force $promise)
          impossible)){$work}){$promise}
  (A B))

; Select a function before the strict work, then reuse that same function
; afterwards. The first application forces the shared head; collection must
; retain its selected world rather than choosing again at the second use.
(= (prime-need:gc-head) (lam x (+ x 1)))
(= (prime-need:gc-head) (lam x (+ x 2)))
(= (prime-need:gc-use $function)
   (let $first ($function 1)
     (if (< $first 10)
         (let $work (prime-need:gc-work (force (delay 1600)))
           (if (== $work 1600)
               (Pair $first ($function 2))
               impossible)){$work}
         impossible)){$first})

!(prime-need:gc-use (prime-need:gc-head))
