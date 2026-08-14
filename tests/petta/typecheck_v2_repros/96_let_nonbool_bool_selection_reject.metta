; An identity binding does not create Boolean evidence from a numeric value.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-bool
   (-[$effect]-> Bool (-[$effect]-> Number Number) Number))
(= (select-bool True $continuation) ($continuation 1))
(= (select-bool False $continuation) ($continuation 0))

(: let-nonbool-caller (-[det]-> Number))
(= (let-nonbool-caller)
   (select-bool
      (let $flag 1 $flag)
      keep-number))

!(test (let-nonbool-caller) 1)
