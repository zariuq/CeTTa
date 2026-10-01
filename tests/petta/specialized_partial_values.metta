; A partial application passed to a higher-order function keeps its held
; arguments as values: the function specialized to it neither matches them
; as calls in its head nor runs them in its body.
(= (Fn $a $b) (+ $a $b))
(= (keep $k $a $x) (got $k $a $x))
(= (my-apply $f $a $x) ($f $a $x))
(= (map-list $f $l) (if (== $l ()) () (let* (($h (car-atom $l)) ($t (cdr-atom $l)) ($r (map-list $f $t))) (cons-atom ($f $h) $r))))
!(my-apply (keep Fn) 1 2)
!(my-apply (keep +) 1 2)
!(my-apply (keep (quote (+ 1 2))) a b)
!(my-apply (keep (quote (Q (+ 1 2)))) a b)
!(my-apply (keep (quote (* (P 1) (P 2)))) a b)
!(my-apply (keep NotFn) 1 2)
!(let $f (keep Fn) ($f 1 2))
!(map-list (keep (quote (+ 1 2)) a) (1 2))
!(my-apply (keep (quote (+ 1 2))) (+ 1 1) b)
; A specialization's held value reaches it however the call spells it: a
; list completed later through an open cell is the same list.
(= (step $tag $acc $x) (cons-atom ($tag $x) $acc))
(= (fold-l $f $init ()) $init)
(= (fold-l $f $init (cons $x $xs)) (fold-l $f ($f $init $x) $xs))
!(fold-l (step (a b)) () (1 2))
!(let $l (cons a $t) (let $t (b) (fold-l (step $l) () (1 2))))
!(let $l (cons a $t) (let $t (b) (fold-l (step (a b)) () (fold-l (step $l) () (1 2)))))
