; Every case branch produces a proper list. The effect-polymorphic callee is
; exhaustive for lists, so the caller can retain deterministic cardinality.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-list
   (-[$effect]-> (List Number) (-[$effect]-> Number Number) Number))
(= (select-list () $continuation)
   ($continuation 0))
(= (select-list (cons $head $tail) $continuation)
   ($continuation $head))

(: case-list-caller (-[det]-> Bool Number))
(= (case-list-caller $flag)
   (select-list
      (case $flag ((True (cons 1 ())) (False (cons 2 ()))))
      keep-number))

!(test (case-list-caller True) 1)
!(test (case-list-caller False) 2)
