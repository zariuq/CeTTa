; A three-hop administrative alias chain retains Boolean evidence.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-bool
   (-[$effect]-> Bool (-[$effect]-> Number Number) Number))
(= (select-bool True $continuation) ($continuation 1))
(= (select-bool False $continuation) ($continuation 0))

(: let-star-three-hop-caller (-[det]-> Number))
(= (let-star-three-hop-caller)
   (select-bool
      (let* (($source True) ($middle $source) ($flag $middle)) $flag)
      keep-number))

!(test (let-star-three-hop-caller) 1)
