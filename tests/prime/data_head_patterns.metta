; An equation head may deconstruct a Data parameter: the held argument is
; matched as the syntax it holds, without evaluating it, and the parts the
; head binds are held in the right-hand side.
(= (f $y) (g $y))
(= (g $y) (pair $y $y))
(: inspect-pair (-> (Data Atom) (Data Atom)))
(= (inspect-pair (Pair $x $y)) (Got $x $y))
(: inspect-colon (-> (Data Atom) (Data Atom)))
(= (inspect-colon (: $x $y)) (Got $x $y))
(: inspect-three (-> Number Number (Data Atom) (Data Atom)))
(= (inspect-three $s $n (: $x $y)) (Got $x $y))
(: first-part (-> (Data Atom) (Data Atom)))
(= (first-part (Pair $x $y)) $x)
(: run-first (-> (Data Atom) $r))
(= (run-first (Pair $x $y)) (eval $x))
(: shape (-> (Data Atom) (Data Atom)))
(= (shape (lam $v $b)) (Lambda $b))
(= (shape (Pair $x $y)) (Pair-of $x))

!(inspect-pair (Pair A B))
!(inspect-colon (: A B))
!(inspect-colon (: A $t))
!(inspect-three 0 1 (: A B))

; The parts stay held: an unevaluated call comes back as written, and only
; explicit evaluation runs it.
!(first-part (Pair (f nil) B))
!(size-atom (first-part (Pair (f nil) B)))
!(run-first (Pair (f nil) B))

; Several heads over the same held argument; each matches its own shape.
!(shape (lam z (f z)))
!(shape (Pair (f nil) B))

; No equation matches: the call is data, not an error.
!(inspect-pair (Triple A B C))

; A parameter that does not hold still evaluates its argument.
(= (plain-first (pair $x $y)) $x)
!(plain-first (f nil))
