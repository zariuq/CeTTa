; Alias forwarding preserves Boolean shape but cannot make a nondeterministic
; producer acceptable to a deterministic selector.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-bool
   (-[$effect]-> Bool (-[$effect]-> Number Number) Number))
(= (select-bool True $continuation) ($continuation 1))
(= (select-bool False $continuation) ($continuation 0))

(: let-star-alias-chain-nondet-caller (-[det]-> Number))
(= (let-star-alias-chain-nondet-caller)
   (select-bool
      (let* (($source (superpose (True False))) ($flag $source)) $flag)
      keep-number))

!(test (let-star-alias-chain-nondet-caller) 1)
