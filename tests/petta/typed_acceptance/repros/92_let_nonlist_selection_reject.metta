; Negative mirror of the bound-list witness: an identity let may preserve
; evidence, but it may not invent a list spine for a scalar producer.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-list
   (-[$effect]-> (List Number) (-[$effect]-> Number Number) Number))
(= (select-list () $continuation) ($continuation 0))
(= (select-list (cons $head $tail) $continuation) ($continuation $head))

(: let-nonlist-caller (-[det]-> Number))
(= (let-nonlist-caller)
   (select-list
      (let $item 1 $item)
      keep-number))

!(test (let-nonlist-caller) 1)
