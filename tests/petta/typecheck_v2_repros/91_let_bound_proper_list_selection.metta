; A deterministic let binds a manifest proper list before a total list
; selector consumes it. The binding should retain list evidence for the
; enclosing call-selection proof.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-list
   (-[$effect]-> (List Number) (-[$effect]-> Number Number) Number))
(= (select-list () $continuation) ($continuation 0))
(= (select-list (cons $head $tail) $continuation) ($continuation $head))

(: let-list-caller (-[det]-> Number))
(= (let-list-caller)
   (select-list
      (let $items (cons 1 ()) $items)
      keep-number))

!(test (let-list-caller) 1)
