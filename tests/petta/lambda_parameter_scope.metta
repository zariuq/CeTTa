; A lambda's parameters are its own: the reference compiles a lambda to a
; clause of its own, so a parameter named like a variable of the enclosing
; form is another variable, while the lambda's other variables are the
; form's.  A lambda that is data (quoted, stored by add-atom) keeps its
; variables.
(= (shadow1 $a $l) (maplist (|-> ($a) $a) $l))
(= (shadow2 $a $l) (maplist (|-> (($a $b)) ($a $b)) $l))
(= (shadow4 $a) ((|-> ($a) $a) 5))
(= (outer-after $x) (let $f (|-> ($x) (+ $x 1)) ($f 10)))
(= (free-shared $l) (let $k 3 (maplist (|-> ($x) (+ $x $k)) $l)))
!(shadow1 outer (1 2))
!(shadow2 outer ((1 2) (3 4)))
!(shadow4 outer)
!(outer-after 1)
!(let $a outer (maplist (|-> ($a) $a) (1 2)))
!(let $a outer ((|-> (($a $b)) $a) (1 2)))
!(let $a outer (eval (quote ((|-> ($a) $a) 5))))
!(let $x 1 (let $f (|-> ($x) $x) (($f 2) $x)))
!(let $a outer (let $_ (add-atom &self (holder (|-> ($a) $a))) (collapse (match &self (holder $h) $h))))
