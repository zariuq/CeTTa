; Both branches of the conditional construct a proper list. The
; effect-polymorphic callee is exhaustive for lists, so this probes whether
; that list fact is retained through the conditional at the call boundary.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-list
   (-[$effect]-> (List Number) (-[$effect]-> Number Number) Number))
(= (select-list () $continuation)
   ($continuation 0))
(= (select-list (cons $head $tail) $continuation)
   ($continuation $head))

(: if-list-caller (-[det]-> Bool Number))
(= (if-list-caller $flag)
   (select-list
      (if $flag (cons 1 ()) (cons 2 ()))
      keep-number))

!(test (if-list-caller True) 1)
!(test (if-list-caller False) 2)
