; A let binds the full value of its source.  The pure call (f 1) reduces
; first to the weak-head form (c (g 1)); the bound value is still (c (s 1)),
; so operations that keep their operands as data (cons-atom, quote) see
; evaluated fields.  Whether the enclosing body is pure, or collapses a
; computation before or after the binding, does not change what is bound.
; A condition and a demanded argument receive the same value, and quoted
; syntax stays syntax.

(= (g $x) (s $x))
(= (f $x) (c (g $x)))
(= (nil) ())

; Pure bodies.
(= (k1 $x) (let $p (f $x) (let $e () (R (cons-atom $p $e)))))
!(k1 1)
(= (k2 $x) (let $p (f $x) (let $e (nil) (R (cons-atom $p $e)))))
!(k2 1)

; Bodies with a collapse, before or after the binding.
(= (k3 $x) (let $p (f $x) (let $e (collapse (empty)) (R $p))))
!(k3 1)
(= (k4 $x) (let $p (f $x) (let $e (collapse (empty)) (R $p $e))))
!(k4 1)
(= (k5 $x) (let $p (f $x) (let $e (collapse (superpose (1 2))) (R (cons-atom $p $e)))))
!(k5 1)
(= (k6 $x) (let $e (collapse (empty)) (let $p (f $x) (R (cons-atom $p $e)))))
!(k6 1)
(= (k7 $x) (let $p (f $x) (let $e (collapse (empty)) (R (cons-atom $e $p)))))
!(k7 1)
(= (k8 $x) (let $p (f $x) (let $e (collapse (empty)) (quote $p))))
!(k8 1)

; The same value reaches a demanded argument and a condition.
(= (h (c $y)) (cons-atom $y ()))
(= (k9 $x) (let $e (collapse (empty)) (h (f $x))))
!(k9 1)
(= (k10 $x) (let $e (collapse (empty)) (if (f $x) a b)))
!(k10 1)

; Quoted syntax stays syntax.
(= (k11 $x) (let $e (collapse (empty)) (let $p (quote (g $x)) (R (cons-atom $p $e)))))
!(k11 1)
