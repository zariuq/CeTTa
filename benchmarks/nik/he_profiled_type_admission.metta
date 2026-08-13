; Repeatedly ask the live HE profiled type authority the same closed judgment.
; A revision-pinned admitted result should make every query after the first a
; direct cache lookup.  CETTA_TYPE_CACHE=0 is the canonical reference path.

(: f (-> Number Number))

(= (type-loop $n)
   (if (== $n 0)
       Done
       (let $type (get-type (f 1))
         (type-loop (- $n 1)))))

!(type-loop 100000)
