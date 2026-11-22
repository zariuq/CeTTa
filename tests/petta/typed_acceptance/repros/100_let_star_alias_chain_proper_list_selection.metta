; A straight-line let* alias chain retains established proper-list evidence.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-list
   (-[$effect]-> (List Number) (-[$effect]-> Number Number) Number))
(= (select-list () $continuation) ($continuation 0))
(= (select-list (cons $head $tail) $continuation) ($continuation $head))

(: let-star-list-alias-chain-caller (-[det]-> Number))
(= (let-star-list-alias-chain-caller)
   (select-list
      (let* (($source (cons 1 ())) ($items $source)) $items)
      keep-number))

!(test (let-star-list-alias-chain-caller) 1)
