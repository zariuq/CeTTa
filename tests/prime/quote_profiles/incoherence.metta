; One variable, one meaning.  `k` mentions its parameter twice, once outside
; a quotation and once inside.
;
; Under the quote-as-written bubble substitution enters the quotation.  If
; the quotation captured the argument's source while the other mention
; demanded its value, (k (+ 1 2)) would pair 3 with the code (+ 1 2), the
; same variable would denote two things in one body, and the answer would
; depend on the route (by let the quotation would capture 3).  That is the
; reading a sealing-as-written calculus is shown incoherent for
; (Mettapedia.TypeTheory.Calculi.SealedCode.asWritten_incoherent).  A
; variable denotes what its binding parameter's type says instead: `k`'s
; parameter is a value parameter, so both mentions denote the value 3, by
; call, by let and by case alike.  Source capture is asked for with a
; `Atom` parameter, which then denotes the syntax at both mentions.
;
; Under the default profile the quotation is sealed.  It holds the variable
; itself on both routes (renamed apart with the rule at a call), and only the
; mention outside it is bound.
(= (k $x) (Pair $x @$x))
(= (inside (Pair $v (quote $c))) (Pair $v (get-metatype $c)))

; By call: the quotation holds a variable (sealed) or the value 3.
!(inside (k (+ 1 2)))
!(== (k (+ 1 2)) (Pair 3 (quote (+ 1 2))))

; By let: the quotation holds the variable (sealed) or the value 3.
!(let $x (+ 1 2) (Pair $x @$x))
!(inside (let $x (+ 1 2) (Pair $x @$x)){$x})

; By case: as by let.
!(inside (case (+ 1 2) (($x (Pair $x @$x)))){$x})

; A `Atom` parameter denotes its argument's syntax at both mentions.
(: k-syntax (-> Atom Proc))
(= (k-syntax $x) (Pair $x @$x))
!(inside (k-syntax (+ 1 2)))
