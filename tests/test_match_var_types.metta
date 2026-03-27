; Test: match with variable-containing types in facts
; This is where CeTTa and HE diverge

!(bind! &rules (new-space))
!(add-atom &rules (: mp (-> (→ $p $q) $p $q)))

!(bind! &facts (new-space))
!(add-atom &facts (: f1 (→ $a (→ $b $a))))

; f1 has type (→ $a (→ $b $a)) which is (→ X Y) form
; Rule mp expects (-> (→ $p $q) $p $q)
; So $a1 = (→ $a (→ $b $a)), matching (→ $p $q) gives $p=$a, $q=(→ $b $a)
; Then $a2 = $p = $a (a variable!)
; We need a fact with type $a — any fact matches since $a is free

!(assertAlphaEqualToResult
  (let (: $x1 $a1) (match &facts (: $x $t) (: $x $t))
    (match &rules (: $f (-> $a1 $a2 $b)) (result $f $x1 $a2 $b)))
  ((result mp f1 $x (→ $y $x))))
