; A PeTTa function is defined only by an equation whose left side applies a
; symbol, (= (F args...) body).  Any other equation is a fact of its space:
; it defines no call, captures no other name, and match reads it like any
; row.  A row in another space changes nothing here either.
(= ($h 4) 8)
!(double 4)
!(println! (printed (double 4)))
!(size-atom (a b c))
!(+ 1 2)
!(let $c (collapse (reduce (double 4))) (written-reduce $c))
!(let $call (cons-atom double (4)) (let $c (collapse (reduce $call)) (built-reduce $c)))
!(let $call (cons-atom double (4)) (let $c (collapse (eval $call)) (built-eval $c)))
!(let $c (collapse (triple 5)) (other-argument $c))
!(let $c (collapse (foo 4 4)) (other-arity $c))
!(collapse (match &self (= ($k 4) $r) (row $r)))

; A bare symbol on the left is a fact as well.
(= sym 5)
!(let $c (collapse (reduce (sym 1))) (bare-symbol $c))
!(collapse (match &self (= sym $v) (sym-row $v)))

; A variable-headed row in another space.
!(bind! &o (new-space))
!(add-atom &o (= ($g 7) 9))
!(collapse (seven 7))
!(collapse (plain (a (+ 1 2))))
!(collapse (match &o (= ($k 7) $r) (other-row $r)))
!(size-atom (collapse (get-atoms &o)))

; Positive control: a named head defines its function, here and after the
; facts above.
(= (twice $x) (* 2 $x))
!(twice 4)
!(let $c (collapse (reduce (twice 5))) (named-reduce $c))
!(let $call (cons-atom twice (6)) (let $c (collapse (reduce $call)) (named-built $c)))
