; cons-atom and union-atom meeting an expected list take their operands as
; they were written: an operand that is a variable holds a value, which is
; not called again, while an operand written as a call is computed first.
(= (p $x) BOOM)
(= (q $x $y) (q $y $x))
(= (held) (quote (p b)))
(= (pair) (a b))

; cons-atom: head and tail held by variables stay data.
!(let ($u) (let $k (quote (p a)) (cons-atom $k ())) (held-head $u))
!(let ($a $b) (let $t (quote ((p c))) (cons-atom x $t)) (held-tail $a $b))
!(let ($u $v) (let $k (held) (cons-atom $k (cons-atom $k ()))) (held-twice $u $v))
; A value whose head has a looping equation is never called.
!(let ($u) (let $k (quote (q a b)) (cons-atom $k ())) (held-loop $u))
; A call over a held value computes over the value.
!(let ($u) (let $k (quote (p a)) (cons-atom (car-atom (cdr-atom $k)) ())) (call-over-held $u))
; An operand written as a call is computed.
!(let ($u) (cons-atom (p a) ()) (written-call $u))

; union-atom: a call operand is computed before the lists join.
!(let ($x $y $z) (union-atom (pair) (c)) (joined-call $x $y $z))
!(let ($x $y) (let $k (quote ((p a))) (union-atom $k (c))) (joined-held $x $y))
!(let ($x $y) (union-atom (a) (cons-atom (p b) ())) (joined-cons $x $y))
; Unknown operands split a known list, every way.
!(collapse (let (a b c) (union-atom $x $y) ($x $y)))
!(collapse (let (a b) (union-atom $x (b)) $x))
