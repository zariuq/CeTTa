; Chain uses value-first binding syntax. This checks whether an identity
; binding carries a deterministic proper-list result to a total selector.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-list
   (-[$effect]-> (List Number) (-[$effect]-> Number Number) Number))
(= (select-list () $continuation) ($continuation 0))
(= (select-list (cons $head $tail) $continuation) ($continuation $head))

(: chain-list-caller (-[det]-> Number))
(= (chain-list-caller)
   (select-list
      (chain (cons 1 ()) $items $items)
      keep-number))

!(test (chain-list-caller) 1)
