; A deterministic identity let returns a manifest boolean to a total Boolean
; selector. This is the Boolean counterpart of bound list evidence.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-bool
   (-[$effect]-> Bool (-[$effect]-> Number Number) Number))
(= (select-bool True $continuation) ($continuation 1))
(= (select-bool False $continuation) ($continuation 0))

(: let-bool-caller (-[det]-> Number))
(= (let-bool-caller)
   (select-bool
      (let $flag True $flag)
      keep-number))

!(test (let-bool-caller) 1)
