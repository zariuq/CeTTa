; Candidate census probe: check whether an open head pattern can carry enough
; evidence for recursive structural selection totality.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-dynamic-head
   (-[$effect]-> Expression (-[$effect]-> Number Number) Number))
(= (select-dynamic-head ($head fixed $value) $continuation)
   ($continuation $value))

(: dynamic-head-caller (-[det]-> Expression Expression Number))
(= (dynamic-head-caller $head $tag)
   (select-dynamic-head ($head $tag 1) keep-number))
