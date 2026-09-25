; Ordered witnesses. Each block is one space; queries are separate files' worth
; only if they share facts. This file is one space, so later queries see
; every fact above them.

(edge s a)
(edge s b)
(edge a t)
(edge b t)
(edge a b)
(edge b a)
(edge a a)
(hedge a (lbl p) b)
(node b)

!(match &self (, (edge $c $a) (edge $c $b)) (star $c $a $b))
!(match &self (, (edge $a $b) (edge $b $c) (edge $c $a)) (tri $a $b $c))
!(match &self (, (edge $s $a) (edge $s $b) (edge $a $t) (edge $b $t)) (dia $s $a $b $t))
!(match &self (, (edge $x $y) (edge $y $x)) (back $x $y))
!(match &self (edge $x $x) (loop $x))
!(match &self (, (edge $x $x) (edge $x $z)) (bad $x $z))
!(match &self (edge s a) (kept s a))
!(match &self (, (hedge $x $lab $y) (node $y)) (h $x $lab $y))
!(match &self (, (edge missing $y) (edge $y $z)) (bad missing $y $z))
!(match &self (, (edge a $y) (edge $y $z)) (proof a $y $z))
