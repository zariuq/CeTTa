; A straight-line let* alias chain retains manifest Boolean evidence.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-bool
   (-[$effect]-> Bool (-[$effect]-> Number Number) Number))
(= (select-bool True $continuation) ($continuation 1))
(= (select-bool False $continuation) ($continuation 0))

(: let-star-alias-chain-caller (-[det]-> Number))
(= (let-star-alias-chain-caller)
   (select-bool
      (let* (($source True) ($flag $source)) $flag)
      keep-number))

!(test (let-star-alias-chain-caller) 1)
