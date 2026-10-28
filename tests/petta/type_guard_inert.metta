; Typed calls whose values no declaration can type take the inert guard;
; the observations below are those of the corrected F5 type relation.
; A later type variable bound to %Undefined% is checked, not a literal wildcard.

; A requirement aliased by an earlier guard: ($p $q) meets ($a $a), so
; the guard on $p decides $q for the last guard.
(: mk (-> $a ($a $a)))
(: aliased (-> ($p $q) $p $q Bool))
(= (aliased $pair $left $right) True)
!(collapse (aliased (mk $v) 1 2))
!(collapse (aliased (mk $v) 1 "s"))
; The same calls from a compiled body run the typed protocol's guards.
(= (via-aliased $w $l $r) (aliased (mk $w) $l $r))
!(collapse (via-aliased $v 1 2))
!(collapse (via-aliased $v 1 "s"))
(= (via-same $x $y) (same $x $y))
!(collapse (via-same 1 2))
!(collapse (via-same 1 "s"))
!(collapse (via-same (a b) (c d e)))
!(collapse (via-same a (c d)))

; A polymorphic requirement read by a later guard.
(: same (-> $t $t Bool))
(= (same $x $y) True)
!(collapse (same 1 2))
!(collapse (same 1 "s"))
!(collapse (same (a b) (c d)))
!(collapse (same (a b) (c d e)))
!(collapse (same (a $x) (c d)))
!(collapse (same a (c d)))

; Rigid and %Undefined% requirements, literal and structural values.
(: rigid (-> Number Bool))
(= (rigid $x) True)
!(collapse (rigid 3))
!(collapse (rigid (a b)))
(: unknown (-> %Undefined% Bool))
(= (unknown $x) True)
!(collapse (unknown (a (b c))))
!(collapse (unknown 4))
(: shaped (-> (Number $u) Bool))
(= (shaped $x) True)
!(collapse (shaped (1 foo)))
!(collapse (shaped (foo 1)))
!(collapse (shaped (1 foo bar)))

; A value mentioning a declared symbol keeps the relational query, and a
; declaration added later changes the next answer.
(: z Number)
!(collapse (same z 1))
!(collapse (same q "s"))
!(add-atom &self (: q Number))
!(collapse (same q "s"))

; Equal replies stay distinct occurrences; a failing result guard filters.
(: twice (-> $a $a))
(= (twice $x) $x)
(= (twice $x) $x)
!(collapse (twice (k 1)))
(: bad (-> $a Number))
(= (bad $x) (word a number))
!(collapse (bad 1))

; A recursive chainer-shaped call: an inert query and result around a
; Peano depth.
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: walk (-> $kb Nat $q $q))
(= (walk $kb Z $q) $q)
(= (walk $kb (S $n) (: $p $t)) (walk $kb $n (: (step $p) $t)))
!(collapse (walk kb (S (S Z)) (: start (goal x))))
