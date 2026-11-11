; A native's result keeps the identity of the variables it retained: a
; variable sealed in a capsule is the same variable at every unsealing, in
; every branch of a search, as the query's own.  Explicit instantiation
; still makes fresh variables: an equation's own variables at each use,
; and a copied term's against the original.
!(import! &self seal)
!(let $f (seal:new demo) (bind! &f (new-state $f)))
(= (S) (let (seal:facets $s $u) (get-state &f) $s))
(= (U) (let (seal:facets $s $u) (get-state &f) $u))
(= (mk) (foo $y))
; repeated unsealing: one variable, the query's own
!(let $c (seal:seal (S) (foo $x bar))
   (let $p (seal:unseal (U) $c) (let $q (seal:unseal (U) $c) ((== $p $q) (== $p (foo $x bar))))))
; an equation's variables are fresh at each use
!(let $a (mk) (let $b (mk) (== $a $b)))
; a copy's variables are not the original's
!(let $t (foo $x) (let $a (copy-term $t) (== $a $t)))
; in each branch of a search, the branch's own variable
!(collapse (let $v (superpose (1 2))
   (let $c (seal:seal (S) (pair $v $w)) (let $p (seal:unseal (U) $c) (== $p (pair $v $w))))))
