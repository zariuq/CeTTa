; `eval` binds its operand, then takes each value the operand returns to its
; second stage.  A value that holds code (a completed value, a ready cell, a
; held value) runs as that code.  Any other value is the answer as it stands.

(: foo (-> Number Number))
(= (two) (superpose ((+ 1 2) (foo 5))))
(= (inc1 $n) (+ $n 1))

; A call no equation answers, a quotation and a lambda are answered as they
; stand: explicit evaluation does not take them to eval again.
!(eval (foo 5))
!(eval (quote (foo 5)))
!(eval (eval (quote (+ 1 2))))
!(eval (lam x x))
!(chain (foo 5) $v (eval $v))
!(eval (cons-atom foo ($x)))
!(eval (two))

; An open value reaches the second stage as written, whatever its form: it
; runs once as code, so eval agrees with eval of the same value bound by
; `let`, and code formed at run time runs inside the eval that demands it.
!(eval (cons-atom inc1 ($y)))
!(let $t (cons-atom inc1 ($y)) (eval $t))
!(collapse (eval (cons-atom superpose (((inc1 $z) (foo $z))))))

; The answer keeps its expression form: read through a binding after an
; equation for it has arrived, it is evaluated again.
!(let $v (eval (foo 11)) (let $u (add-atom &self (= (foo 11) 110)) $v))

; Negative: a value that holds code still runs as that code.
!(eval (car-atom ((+ 1 2))))
!(eval (cons-atom + (1 2)))
!(eval (unquote (quote (+ 1 2))))
!(eval (let $x (+ 1 2) $x))
!(eval (superpose (3 4)))
!(eval 5)
