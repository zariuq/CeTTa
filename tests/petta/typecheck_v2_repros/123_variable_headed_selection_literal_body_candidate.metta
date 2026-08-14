; Candidate census probe: isolate partial selection from the closure body's
; own inferred-value obligation.  The clause still has an open head pattern,
; but its body supplies a fixed Number to the continuation.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-dynamic-head
   (-[$effect]-> Expression (-[$effect]-> Number Number) Number))
(= (select-dynamic-head ($head fixed $value) $continuation)
   ($continuation 1))

(: dynamic-head-caller (-[det]-> Expression Expression Number))
(= (dynamic-head-caller $head $tag)
   (select-dynamic-head ($head $tag 1) keep-number))
