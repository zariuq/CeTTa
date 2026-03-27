; Test: deeply nested expression matching with mixed types
; Exercises the SubstTree with 3-4 levels of nesting, strings, variables

; Deep nesting: (A (B (C x)))
(Eval (Pred "eats") (List (Con "Sam") (Con "flies")))
(Eval (Pred "eats") (List (Con "Fritz") (Con "flies")))
(Eval (Pred "likes") (List (Con "Sam") (Con "beer")))
(Eval (Pred "hates") (List (Con "Fritz") (Con "beer")))

; Match with variable at different depths
! (match &self (Eval (Pred "eats") (List $x (Con "flies"))) $x)
! (match &self (Eval (Pred $verb) (List (Con "Sam") $obj)) ($verb $obj))
! (match &self (Eval $pred (List $x $y)) ($pred $x $y))

; 4-level nesting
(Deep (L1 (L2 (L3 a))) result-a)
(Deep (L1 (L2 (L3 b))) result-b)
(Deep (L1 (L2 (L3 c))) result-c)

! (match &self (Deep (L1 (L2 (L3 $x))) $r) ($x $r))

; Mixed variable positions at different depths
(: proof1 (-> (-> A B) (-> B C) (-> A C)))
(: proof2 (-> (-> X Y) X Y))
(: proof3 (-> A A))

; Match typed theorems by type structure (user-defined only)
!(bind! &proofs (new-space))
!(add-atom &proofs (: proof1 (-> (-> A B) (-> B C) (-> A C))))
!(add-atom &proofs (: proof2 (-> (-> X Y) X Y)))
!(add-atom &proofs (: proof3 (-> A A)))
! (match &proofs (: $name (-> (-> $a $b) $c $d)) $name)
! (match &proofs (: $name (-> $x $x)) $name)

; Variable head inside nested expression
(= (f (g a)) result-ga)
(= (f (g b)) result-gb)
(= (f (h a)) result-ha)

! (match &self (= (f ($op $arg)) $r) ($op $arg $r))
! (match &self (= (f (g $x)) $r) ($x $r))
