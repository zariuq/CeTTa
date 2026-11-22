; A later binding cannot rewrite the producer retained by an earlier alias.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-bool
   (-[$effect]-> Bool (-[$effect]-> Number Number) Number))
(= (select-bool True $continuation) ($continuation 1))
(= (select-bool False $continuation) ($continuation 0))

(: let-star-shadowed-alias-caller (-[det]-> Number))
(= (let-star-shadowed-alias-caller)
   (select-bool
      (let* (($source 1) ($flag $source) ($source True)) $flag)
      keep-number))

!(test (let-star-shadowed-alias-caller) 1)
