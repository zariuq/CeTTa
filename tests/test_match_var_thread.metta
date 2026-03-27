; Test: variables from one match must survive into a nested match
; This is the core of the forward chaining discrepancy

!(bind! &rules (new-space))
!(add-atom &rules (: mp (-> (→ $p $q) $p $q)))

!(bind! &facts (new-space))
!(add-atom &facts (: f1 (→ A B)))
!(add-atom &facts (: f2 A))

; Get a fact, then match its type against the rule
; f1 has type (→ A B). Rule mp expects (-> (→ $p $q) $p $q).
; So $a1 = (→ A B) should match mp's first arg (→ $p $q).
; Then $a2 = $p = A. We need a fact with type A.
; f2 has type A. So mp(f1, f2) → conclusion B.

!(assertEqualToResult
  (let (: $x1 $a1) (match &facts (: $x $t) (: $x $t))
    (match &rules (: $f (-> $a1 $a2 $b)) (applied $f $x1 $a2 $b)))
  ((applied mp f1 A B)))
