; One variable, two meanings.  `k` mentions its parameter twice, once outside
; a quotation and once inside.
;
; Under the quote-as-written bubble substitution enters the quotation.  The
; argument is passed unevaluated, so the first mention demands its value and
; the second captures its source: (k (+ 1 2)) pairs 3 with the code (+ 1 2),
; and the same variable denotes two things in one body.  Binding the same
; computation by let evaluates it first, and the quotation captures 3: the
; answer depends on the route.  This is the reading a sealing-as-written
; calculus is shown incoherent for
; (Mettapedia.TypeTheory.Calculi.SealedCode.asWritten_incoherent).
;
; Under the default profile the quotation is sealed.  It holds the variable
; itself on both routes (renamed apart with the rule at a call), and only the
; mention outside it is bound.
(= (k $x) (Pair $x @$x))
(= (inside (Pair $v (quote $c))) (Pair $v (get-metatype $c)))

; By call: the quotation holds a variable (sealed) or an expression (the
; argument's source, as written).
!(inside (k (+ 1 2)))
!(== (k (+ 1 2)) (Pair 3 (quote (+ 1 2))))

; By let: the quotation holds the variable (sealed) or the value 3.
!(let $x (+ 1 2) (Pair $x @$x))
!(inside (let $x (+ 1 2) (Pair $x @$x)))

; By case: as by let.
!(inside (case (+ 1 2) (($x (Pair $x @$x)))))
