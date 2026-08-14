; A one-binding let* identity returns a manifest Boolean to a total selector.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-bool
   (-[$effect]-> Bool (-[$effect]-> Number Number) Number))
(= (select-bool True $continuation) ($continuation 1))
(= (select-bool False $continuation) ($continuation 0))

(: let-star-bool-caller (-[det]-> Number))
(= (let-star-bool-caller)
   (select-bool
      (let* (($flag True)) $flag)
      keep-number))

!(test (let-star-bool-caller) 1)
