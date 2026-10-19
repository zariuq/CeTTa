; A definition that takes arguments as they are written is expanded where it
; is called in the prepared machine, as HE runs it: its Atom arguments in
; place, its other arguments evaluated once, and HE's unify with an Empty
; else a pattern bind.  A pattern that does not match answers nothing, and
; a variable already bound is matched, not shadowed.  A macro whose expansion
; does not end is left to the canonical evaluator.
(: my-let (-> Atom %Undefined% Atom %Undefined%))
(= (my-let $pattern $atom $template) (unify $atom $pattern $template Empty))
(: twice-of (-> Atom %Undefined%))
(= (twice-of $expr) (my-let $v $expr (+ $v $v)))
(= (loop $n $acc) (if (== $n 0) $acc (my-let $m (- $n 1) (loop $m (+ $acc 1)))))
(= (nested $x) (my-let $a (+ $x 1) (my-let $b (* $a 2) (my-let $a (+ $b $a) (- $a $x)))))
(= (pairs $x) (my-let (pair $a $b) (pair $x (+ $x 1)) (* $a $b)))
(= (nomatch $x) (my-let (pair $a $b) $x (+ $a $b)))
(: typed-use (-> Number Number))
(= (typed-use $x) (my-let $y (* $x 3) (+ $y 1)))
(= (shadow $x) (my-let $x (+ $x 10) $x))
(: countdown (-> Atom %Undefined%))
(= (countdown $n) (if (== $n 0) done (countdown (- $n 1))))
!(loop 500 0)
!(collapse (nested 3))
!(pairs 6)
!(collapse (nomatch (pair 1 2)))
!(collapse (nomatch 7))
!(typed-use 4)
!(twice-of (+ 2 3))
!(collapse (shadow 1))
!(my-let $q (+ 1 1) (my-let $r (+ $q 1) ($q $r)))
!(countdown 3)
