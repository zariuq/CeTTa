; A lazy accumulator graph shares earlier additions.  The generated Need
; machine must update a forced suspension once; re-evaluating immutable syntax
; turns this linear recurrence into exponential work.
(= (sharing:fib-tr $n $a $b)
   (if (== $n 0)
       $a
       (sharing:fib-tr (- $n 1) $b (+ $a $b))))

!(sharing:fib-tr 1000 0 1)
; The comparison keeps the observable compact while forcing enough shared
; recurrence work to cross the one-megabyte nursery gate.
!(< (sharing:fib-tr 20000 0 1) 0)

; Two positional bindings retain the same suspension.  Updating the first
; demand must make the second demand a memo hit rather than a resample.
(= (sharing:inc $x) (+ $x 1))
(= (sharing:use-twice $x $y) (+ $x $y))
(= (sharing:duplicate $x) (sharing:use-twice $x $x))
!(sharing:duplicate (sharing:inc 10))

; User-defined suspension wrappers are deliberately outside the generated
; total-integer normalization fragment.  This ladder therefore exercises
; ephemeron collection and update-path compression rather than eager GMP
; normalization, while keeping the observable result compact.
(= (sharing:delay-loop $n $value)
   (if (== $n 0)
       $value
       (sharing:delay-loop (- $n 1) (sharing:inc $value))))
!(< (sharing:delay-loop 20000 0) 0)
