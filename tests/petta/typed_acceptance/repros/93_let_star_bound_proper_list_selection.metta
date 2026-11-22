; Sequential binding has the same list-evidence obligation as a simple let.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-list
   (-[$effect]-> (List Number) (-[$effect]-> Number Number) Number))
(= (select-list () $continuation) ($continuation 0))
(= (select-list (cons $head $tail) $continuation) ($continuation $head))

(: let-star-list-caller (-[det]-> Number))
(= (let-star-list-caller)
   (select-list
      (let* (($items (cons 1 ()))) $items)
      keep-number))

!(test (let-star-list-caller) 1)
