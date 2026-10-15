; Under-application: a partial application is a value; applied further, it
; is the call with every argument.
(: plus (-> Number Number Number))
(= (plus $x $y) (+ $x $y))
!(plus 1)
!((plus 1) 2)
!(let $p (plus 1) ($p 2))
!(get-type (plus 1))
!(get-type ((plus 1) 2))
; Three arguments, applied one at a time.
(: add3 (-> Number Number Number Number))
(= (add3 $x $y $z) (+ $x (+ $y $z)))
!(((add3 1) 2) 3)
!((add3 1) 2 3)
!(get-type ((add3 1) 2))
; A partial application passed as an Atom, and at a function type.
(: apply-atom (-> Atom Number Number))
(= (apply-atom $f $x) ($f $x))
!(apply-atom (plus 10) 5)
(: apply-fn (-> (-> Number Number) Number Number))
(= (apply-fn $f $x) ($f $x))
!(apply-fn (plus 10) 5)
; A partial application returned by a function.
(: make-adder (-> Number (-> Number Number)))
(= (make-adder $n) (plus $n))
!((make-adder 3) 4)
!(make-adder 3 4)
; Over-application: the result is an arrow taking the rest.
(: twice-adder (-> Number Number (-> Number Number)))
(= (twice-adder $a $b) (plus (+ $a $b)))
!(twice-adder 1 2 3)
!(get-type (twice-adder 1 2 3))
; Explicitly curried declarations and nested-head equations, as written
; today, still read as written; the flat call now reads too.
(: curry-mul (-> Number (-> Number Number)))
(= ((curry-mul $x) $y) (* $x $y))
!((curry-mul 6) 7)
!(curry-mul 6 7)
; A grounded operation keeps its arity: fewer arguments are a value.
!(+ 1)
!((+ 1) 2)
!(let $inc (+ 1) ($inc 41))
; A partial application with an ill-typed argument is refused.
!(collapse (plus "x"))
!(collapse ((plus "x") 2))
; Over-application past a result that is not an arrow stays an arity error.
!(collapse (plus 1 2 3))
