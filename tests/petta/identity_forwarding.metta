; Identity forms: (chain Source $v $v), binder operand 2, source operand 1;
; (let $v Source $v), binder operand 1, source operand 2.  Each case checks
; what an observer of the binder sees.
(= (g $x) (chain 3 $x $x))
(= (h $x) (let $x 3 $x))

; The enclosing continuation reads the binder.
!(let $r (chain 3 $x $x) (pair $r $x))
!(let $r (let $x 3 $x) (pair $r $x))
; The binder is a head parameter aliased to the caller's variable.
!(let $r (g $y) (pair $r $y))
!(let $r (h $y) (pair $r $y))
; An earlier let aliased the binder.
!(let $y $x (let $r (chain 3 $x $x) (pair $r $y)))
; A closure captured the binder.
!(let $f (|-> ($z) $x) (let $r (chain 3 $x $x) (pair $r ($f 0))))
; A hyperpose branch exports the binder to its caller; collapse exports nothing.
!(let $r (hyperpose ((chain 3 $x $x))) (pair $r $x))
!(let $r (hyperpose ((let $x 3 $x))) (pair $r $x))
!(let $r (collapse (chain 3 $x $x)) (pair $r $x))
; Delayed goals on the binder wake when it gets its value.
!(collapse (let $c (#> $x 2) (chain 1 $x $x)))
!(collapse (let $c (#> $x 2) (chain 5 $x $x)))
!(collapse (let $u (translatePredicate (dif $x 1)) (chain 1 $x $x)))
!(let $u (translatePredicate (freeze $x (Predicate (writeln woke)))) (let $d (chain (+ 0 1) $x $x) $d))
; A bound binder filters.
!(collapse (let $x 5 (let $r (let $x 3 $x) $r)))
!(collapse (let $x 5 (let $r (chain 3 $x $x) $r)))
; The binder occurs in the source, which sees it aliased to the destination.
!(collapse (chain (let $x 7 ok) $x $x))
!(collapse (let true (chain (is-var $x) $x $x) yes))
!(collapse (let false (chain (is-var $x) $x $x) yes))
!(collapse (let false (let $x (is-var $x) $x) yes))
!(collapse (let $d (chain (is-var $x) $x $x) $d))
; A constant output the destination rejects: the source never runs.
!(collapse (let (pair $a $b) (progn (println! direct-ran) 5) done))
!(collapse (let (pair $a $b) (chain (progn (println! through-chain-ran) 5) $v $v) done))
!(collapse (let 5 (chain (progn (println! accepted-ran) 5) $v $v) yes))
; A nondeterministic source keeps its multiplicity and order.
!(collapse (chain (superpose (1 2 2 3)) $v $v))
!(collapse (let $v (superpose (1 2 2 3)) $v))
; A query-level identity form.
!(chain 3 $x $x)

; Forwarding preserves an escaped exception's enclosing handler.
!(catch (chain (callPredicate (Predicate (throw identity-forward-fault))) $v $v))
!(catch (let $v (callPredicate (Predicate (throw identity-forward-fault))) $v))

; The source's cut keeps the same call boundary and answer multiplicity.
(= (forward-cut $x) (chain (let $u (cut) (superpose ($x $x))) $v $v))
(= (forward-cut $x) rejected)
!(collapse (forward-cut 7))
!(collapse (superpose ((forward-cut 7) (forward-cut 9))))
