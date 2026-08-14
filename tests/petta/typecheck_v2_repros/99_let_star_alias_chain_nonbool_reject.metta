; An alias chain preserves a numeric value, not Boolean evidence.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-bool
   (-[$effect]-> Bool (-[$effect]-> Number Number) Number))
(= (select-bool True $continuation) ($continuation 1))
(= (select-bool False $continuation) ($continuation 0))

(: let-star-alias-chain-nonbool-caller (-[det]-> Number))
(= (let-star-alias-chain-nonbool-caller)
   (select-bool
      (let* (($source 1) ($flag $source)) $flag)
      keep-number))

!(test (let-star-alias-chain-nonbool-caller) 1)
