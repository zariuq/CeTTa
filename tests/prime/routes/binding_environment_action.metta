; One environment action (rule 5 of the binding structure): every consumer
; reads its operands through the current environment, by capture-avoiding
; substitution of the store over the binding structure.  It stops at a
; lambda parameter, never enters a quotation in value position, and fills
; store names in pattern positions.

(= (h $x) (unify $x 5 ok no))
(= (hz $x) (unify $x z ok no))
(= (second $s $t) (let (d $s $t) (d a b) (Got $t)){$s $t})
(= (lazy-id $x) $x)
(= (first-of (lp $a $b)) $a)

; ---- A lambda's parameters are lexical: no outer binding or substitution
; of a same-spelled name reaches them; a captured name is still filled.
!((lam $x $x) 6)
!(let $x 5 ((lam $x $x) 6))
!(let $x 5 ((lam $z $z) 6))
!(let $r (h $x) ((lam $x $x) 6))
!(let $r (h $x) (Pair ((lam $x $x) 6) $x))
!(let $x 5 ((lam z (Pair z $x)) 6))
!(let $x (lam $x $x) ($x 7))

; ---- The substitution is capture-avoiding: a value naming z is not
; captured by the binder z, whoever substitutes it.
!(let $y z ((lam z (Pair z $y)) 6))
!(let $r (hz $y) ((lam z (Pair z $y)) 6))
!(let $f (lam z (Pair z $y)) (let $y z ($f 6)){$y})

; ---- let (8/07, unchanged): a repeated name refines; a conflict is no
; answer.
!(Pair (let $x 1 $x){$x} (let $x 2 $x){$x})
!(let $x $y (foo (let $x $z (bar $x $y $z)){$x}))
!(let $q (let (d $y) (d 5) ok){$y} (Pair $y $q))
!(let $r (second a $t) (Pair $t $r))
!(second c $t)
!(let* (($x a) ($x b)) (got $x)){$x}

; ---- A role belongs to an occurrence: a quotation in value position is
; sealed, and a quoted pattern elsewhere, here in a branch never taken, does
; not unseal it.  A quoted pattern's own names bind.
!(let $x 5 (if True @$x no))
!(let $x 5 (if True @$x (match &self @(never $x) no){$x}))
!(case @(f a) (((quote (f $y)) (Got $y))))
!(let $r (h $x) (Pair $x @$x))

; ---- Every consumer: stream forms, grounded operations, metatypes, types,
; sealing, printing, Need forcing and effects all read the environment.
!(let $r (h $y) (once (unify $y 6 yes no)))
!(let $r (h $y) (Pair $y (once (unify $y 6 yes no))))
!(let $r (h $y) (once (unify $y 6 (println! wrong) (println! right))))
!(let $r (h $y) (+ $y 1))
!(let $r (h $y) (get-metatype $y))
!(let $r (h $y) (collect (unify $y 6 yes no)))
!(let $r (h $y) (select 1 (unify $y 6 yes no)))
!(let $r (h $y) (collapse (unify $y 6 yes no)))
!(let $r (h $y) (superpose ((unify $y 5 a no) (unify $y 6 b no))))
!(let $r (h $y) (* $y $y))
!(let $r (h $y) (get-type $y))
!(let $r (h $y) (sealed () (q $y)))
!(let $r (h $y) (repr $y))
!(let $r (h $y) (assertEqual (+ $y 1) 6))
!(let $t (lazy-id (+ $y 1)) (let $r (h $y) $t))
!(let $p (lp (+ $y 1) 0) (let $r (h $y) (first-of $p)))
!(let $r (h $y) (once (let $u (println! (in-once $y)) (unify $y 6 yes no)){$u}))
!(let $r (h $y) (let $s (new-space) (let $u (add-atom $s (fact $y)) (get-atoms $s))))

; An unresolved computation keeps its demand after a later binding.
; Completed callable-shaped data remains inert until explicit eval.
; Mixed occurrences preserve order, duplicates, sharing, and effects once.

(= (brc:set $x) (unify $x 5 ok no))
(= (brc:identity $x) $x)
(= (brc:first (brc:box $x $ignored)) $x)
(= (brc:after-effect $x)
   (let $ignored (println! brc:effect) (+ $x 1)))

; The original open-source return, compared with direct source and a field.
!(let $t (brc:identity (+ $y 1)) (let $r (brc:set $y) $t))
!(let $t (+ $y 1) (let $r (brc:set $y) $t))
!(let $p (brc:box (+ $y 1) 0) (let $r (brc:set $y) (brc:first $p)))

; Constructed callable syntax must retain its earned data role.
!(let $t (cons-atom + ($y 1)) (let $r (brc:set $y) $t))
!(let $t (brc:identity (cons-atom + ($y 1))) (let $r (brc:set $y) $t))
!(let $t (cons-atom + ($y 1)) (let $r (brc:set $y) (eval $t)))

; A producer's earlier effect cannot be replayed by later refinement.
!(let $t (brc:after-effect $y) (let $r (brc:set $y) $t))
!(brc:identity (+ 2 1))

(= (brm:set $x) (unify $x 5 ok no))
(= (brm:identity $x) $x)
(= (brm:after-effect $x) (let $ignored (println! brm:once) (+ $x 1)))

!(let $t (superpose ((+ $y 1) (cons-atom + ($y 1)))) (let $r (brm:set $y) $t))
!(let $t (superpose ((cons-atom + ($y 1)) (+ $y 1))) (let $r (brm:set $y) $t))
!(let $t (superpose ((+ $y 1) (+ $y 1) (cons-atom + ($y 1)) (cons-atom + ($y 1)))) (let $r (brm:set $y) $t))
!(let $t (brm:identity (superpose ((+ $y 1) (cons-atom + ($y 1))))) (let $r (brm:set $y) $t))
!(let $t (+ $y 1) (let $r (brm:set $y) (Pair $t $t)))
!(let $t (cons-atom + ($y 1)) (let $r (brm:set $y) (Pair $t $t)))
!(let $t (brm:after-effect $y) (let $r (brm:set $y) (Pair $t $t)))
!(let $t (superpose ((+ $y 1) (cons-atom + ($y 1)))) (let $r (brm:set $y) (eval $t)))
