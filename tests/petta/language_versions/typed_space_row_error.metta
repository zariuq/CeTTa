; Adding a row that contradicts a typed space's schema raises
; literal_type_mismatch in context typecheck.
(: &pairs (SpaceOf (Number Number)))
!(bind! &pairs (new-space))
(= (store $a $b) (add-atom &pairs ($a $b)))

!(test (let (Error $detail $context) (catch (store "a" "b")) $context)
       typecheck)
!(test (collapse (match &pairs ($x $y) $x)) ())
